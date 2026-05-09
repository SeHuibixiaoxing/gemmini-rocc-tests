# 20260509T064128Z - sbus64 live GDB scheduler-locking misarm

## Goal

Start the next live interactive GDB round after the
`stage_prepare_exec_views()` SPM xlate flush frontier was documented, then move
from `worker-before-build-stage-task` to the code after
`runtime_flush_stage_spm_xlate()` returns.

This round is recorded as an invalid debug attempt. It should not be used as
target-code evidence.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host:
  - Instance: `i-0e15e2587888a53db`
  - Private IP: `192.168.1.154`
  - Instance type: `f2.6xlarge`
  - FireSim cluster tag: `pairbertb8d12s64gdbcfg32nicnt`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--06-25-41-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- GDB tunnel:
  `prt-gdb-tunnel-sbus64-postflush`, local `:32366` to guest
  `172.16.0.2:2345`
- GDB tmux:
  `prt-gdb-sbus64-postflush`
- Transcript directory:
  `tmp/firesim-aws-f2/gdb-live/20260509-0634-sbus64-postflush/`

The guest image and remote image matched. The active marker env was:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=4
```

UART confirmed:

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

GDB symbol sanity was correct:

```text
&g_prt_gdb_marker_state = 0x58360
prt_gdb_marker_stop = 0x1457a
```

## What Went Wrong

The GDB session set:

```gdb
set scheduler-locking on
```

while the inferior was still stopped in the dynamic loader:

```text
0x0000003ff7fec386 in ?? () from ld-linux-riscv64-lp64d.so.1
```

It then set `break prt_gdb_marker_stop` and continued. The intended marker did
not hit in the observed window. Two Ctrl-C attempts only echoed `^C` and did not
return a prompt:

```text
(gdb) continue
Continuing.
^C^C
```

Because scheduler locking was enabled before the process reached the runtime
marker and before the target worker thread was selected, this run may have
artificially constrained scheduling to the initially selected loader/main
thread. Therefore the missing marker hit is not meaningful evidence about the
pipeline-runtime hang window.

## Interpretation

Do not classify this as a new frontier. The previous valid frontier remains:

```text
stage_prepare_exec_views()
  after runtime_flush_stage_spm_xlate() returns at prt_runtime.c:2319
```

This invalid round only adds an SOP correction:

- do not enable `set scheduler-locking on` before the marker breakpoint has
  stopped in the target worker thread;
- use normal all-stop `continue` to reach `prt_gdb_marker_stop`;
- after the marker hit, select/confirm the worker thread and then enable
  scheduler locking for stepping through the local call ladder.

## Cleanup

The run farm was terminated with:

```text
pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` targeted `i-0e15e2587888a53db` and returned exit code 0. A
follow-up AWS query confirmed the instance reached `terminated` before the next
run was started.
