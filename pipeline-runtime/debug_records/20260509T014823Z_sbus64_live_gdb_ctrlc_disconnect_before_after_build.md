# 2026-05-09T01:48Z sbus64 live GDB Ctrl-C disconnected before after-build marker

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `192.168.1.184`
- Instance: `i-06ac3492c903ed7d2`
- ELF:
  `generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- ELF sha256:
  `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Result dir:
  `sims/firesim/deploy/results-workload/2026-05-09--01-27-39-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Local evidence:
  `tmp/firesim-aws-f2/pipeline-runtime-gdb/live-20260509-0135/`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PIPELINE_RUNTIME_GUEST_LOG_ENABLE=0
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0
PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0
PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0
PIPELINE_RUNTIME_BREADCRUMB_ENABLE=0
CAPTURE_PERIODIC_SYNC_ENABLE=0
PIPELINE_RUNTIME_CHILD_PROC_DIAG_ENABLE=0
```

UART confirmed the guest runner received the marker filter:

```text
[bertmini] runner-gdb-marker-config enable=1 site=worker-after-build-stage-task segment=2 global_stage=3 local_stage=1 subbatch=0 ...
```

## GDB Session

GDB was the first TCP client to `gdbserver --once`; no `nc`, telnet, curl, or port probe was used.

Connection path:

```bash
ssh -f -N -L 32361:172.16.0.2:2345 ubuntu@192.168.1.184
riscv64-unknown-linux-gnu-gdb -q .../rerocc_pipeline_runtime-linux
(gdb) target remote :32361
```

The target stopped in the dynamic loader, so the software breakpoint was set before the program ran:

```text
Remote debugging using :32361
0x0000003ff7fec386 in ?? () from ld-linux-riscv64-lp64d.so.1
Breakpoint 1 at 0x14482: file .../pipeline-runtime/src/prt_debug_state.c, line 396.
Continuing.
```

## Result

The `worker-after-build-stage-task` marker did **not** hit within roughly 300 seconds of live `continue`.
This is different from the earlier expect/PTTY run that hit the same after-build marker. The difference is
not yet explained; it may be nondeterminism, timing/observability movement, or a real earlier blocker in
this low-log live-GDB run.

When Ctrl-C was sent through the live GDB TTY, GDB did not return a stop prompt for about 70 seconds. A
second Ctrl-C ended the remote session:

```text
^CDisconnected from target.
```

This run therefore did **not** produce a user-space stack for the hang point. It proves only:

- remote gdbserver first attach still works for this bitstream/config;
- the selected after-build marker was not reached in this run before the interrupt attempt;
- this hang/frontier is not reliably interruptible through remote GDB Ctrl-C;
- after the disconnect, this `gdbserver --once` run was consumed and could not be used for further live GDB
  commands.

## Host-Side State

At capture time:

- `gdbserver` had reached:
  - `phase=listening ... pid=202`
  - `phase=inferior ... pid=215`
- FireSim heartbeat was still alive:

```text
Target Cycle (fastest), Seconds Since Start
18266606780, 964
```

The run-host process list still showed `FireSim-f2` running before termination. No guest `/proc` state was
available from the host by default; this workflow only exposed UART/run-host files plus the gdbserver TCP
path. Treating guest `/proc/<pid>/...` as a post-hoc side channel would be incorrect unless a guest shell,
guest SSH, or a preinstalled guest-side sampler has been arranged.

## Interpretation

This run invalidates the assumption that the best available strategy is always "wait until hang, then
Ctrl-C and read the stack". That works only if the target hart remains interruptible through gdbserver. In
this case the first interrupt did not return a prompt, and the second interrupt disconnected the target.

The current noTrace bitstream also cannot retrospectively expose Rocket internal PC, last retired PC, or an
unretired stalled instruction. FireSim FPGA execution does not provide arbitrary internal signal peeking
after the bitstream is built. If software GDB cannot interrupt the target, PC/retire visibility requires a
prebuilt hardware observation path such as TracerV, AutoCounter, or small custom counters/snapshots on
Rocket/RoCC/DMA/NoC handshakes.

## Current Frontier

The current reliable frontier should be stated conservatively:

```text
gdbserver attach works
program starts under GDB
worker-after-build-stage-task was not reached in this run
GDB Ctrl-C did not recover a stack and eventually disconnected
```

Do not claim that the after-build-to-gemm-run window is still the only blocker based on this run. The
earlier after-build hit remains valid evidence for one run, but this run moved the observed live-GDB
frontier earlier or exposed nondeterminism. The next test should use earlier staged markers or breakpoints:

- `runtime-ready`
- `segment-begin` for segment 2
- `worker-entry` for segment 2 / global stage 3 / local stage 1 / subbatch 0
- `worker-before-build-stage-task`
- then `worker-after-build-stage-task`

This avoids relying on a late marker plus a hard-to-interrupt long `continue`.

## Cleanup

The FireSim run farm was terminated via:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

Termination log:

```text
terminaterunfarm ... --forceterminate
Instances terminated: i-06ac3492c903ed7d2
```

A follow-up AWS query showed no active `f2.*` instances. Stale local runworkload wrapper processes were
killed after the instance was terminated.
