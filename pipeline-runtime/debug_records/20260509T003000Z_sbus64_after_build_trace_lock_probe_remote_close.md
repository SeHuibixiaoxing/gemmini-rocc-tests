# 2026-05-09T00:30Z sbus64 after-build to trace-lock probe ended before marker

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Post-marker GDB file: `pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_after_build_to_trace_lock.gdb`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.59`
- Instance: `i-02812e33fbb0090f4`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T235905Z-192_168_1_59-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`
- Runworkload pane log: `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-235056.pane.log`
- Watchdog capture prefix: `tmp/firesim-aws-f2/captures/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-235056-192.168.1.59-host-watchdog-20260509T001805Z.*`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PRT_GDB_MARKER_TIMEOUT=1200
PRT_GDB_POST_MARKER_GDB_FILE=pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_after_build_to_trace_lock.gdb
```

The image freshness checks passed before `runworkload`; the local and remote image hashes matched. The guest environment had sparse log, deep log, audit log, checkpoint log, breadcrumb, debug-trigger, DMA export probe, and DMA fixed-load probe disabled. The wrapper waited for UART `[gdbserver] phase=listening`; GDB was the first TCP client to `172.16.0.2:2345`.

## Intended Probe

This run was meant to reuse the known-good `worker-after-build-stage-task` marker frontier for:

1. hit `worker-after-build-stage-task` at segment 2 / global stage 3 / local stage 1 / subbatch 0;
2. delete the marker breakpoint;
3. set a temporary address breakpoint at `0x3054e`, the tested ELF address for the `pthread_mutex_lock(&rt->state_lock)` call in `prt_trace_on_gemm_issue()`;
4. continue for 120 seconds and either hit the trace-lock address or interrupt and collect backtraces/registers/debug state.

The post-marker script did not execute because the initial marker did not hit in this run.

## Result

GDB connected and set the `prt_gdb_marker_stop` breakpoint:

```text
0x0000003ff7fec386 in ?? () from .../ld-linux-riscv64-lp64d.so.1
Breakpoint 1 at 0x14482: file .../pipeline-runtime/src/prt_debug_state.c, line 396.
```

No marker state, backtrace, or post-marker output was printed. The transcript ended at the initial `continue` command:

```text
pairdummy-cfg32-marker-stop.gdb:10: Error in sourced command file:
Remote connection closed
[pairdummy-gdb-marker] gdb_rc=1
```

The runworkload pane shows the host watchdog fired at `2026-05-09T00:18:05Z`:

```text
[prt-host-watchdog] hb='18275568779, 965' idle=1095s hb_idle=626s uart=20824
[prt-host-watchdog] host idle timeout reached after 1095s (hb_idle=626s)
[prt-host-watchdog] calling terminaterunfarm --forceterminate
```

Therefore the `Remote connection closed` line is most likely a consequence of the watchdog terminating the run farm while GDB was still waiting in the initial `continue`, not an independent guest-side GDB failure.

## Guest State From Watchdog Capture

The UART capture reached the gdbserver inferior phase:

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

The captured status still reported:

```text
state=running
dummy_gemmini_mode=1
dma_force_direct_enable=1
gdbserver_enable=1
```

The wrapper post-stage file showed only wrapper setup and child spawn:

```text
[firemarshal-stage] wrapper-enter ...
[firemarshal-stage] after-log-preamble ...
[firemarshal-stage] before-child-spawn runner=/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh ...
[firemarshal-stage] after-child-spawn pid=143 ...
```

There was no guest `uartlog` copyback in `results-workload`; the watchdog capture is the usable run artifact for this failure.

## Interpretation

This run is inconclusive for the after-build-to-trace-lock source window. It did not prove that control fails to reach `prt_trace_on_gemm_issue()`, because the `worker-after-build-stage-task` marker itself was not reached before the watchdog terminated the run.

The important new observation is that the marker frontier is not perfectly stable across runs:

- Earlier runs hit `worker-before-build-stage-task` and `worker-after-build-stage-task` for the same segment/stage/subbatch.
- This run, with the same marker filter and an extra post-marker command file that should only run after the marker hit, did not reach the same after-build marker inside the host watchdog window.

Possible explanations to keep separated:

- real run-to-run nondeterminism before the after-build marker;
- a long pre-marker execution window that exceeds the host watchdog when no UART/progress output is enabled;
- target execution under GDB changing timing enough to expose the long window;
- watchdog policy closing the run before the GDB evidence can be collected.

Do not treat this as evidence for a lock at `rt->state_lock`. The post-marker breakpoint at `0x3054e` was never armed.

## Cleanup

The watchdog called `terminaterunfarm --forceterminate` for `i-02812e33fbb0090f4`. A follow-up AWS check showed no active `f2.*` instances. The stale local runworkload tmux session and two residual `firesim runworkload` Python processes were killed after the instance was already gone. No local GDB or SSH tunnel process remained.

## Next Step

The marker helper needs an initial-continue timeout, not only a post-marker timeout. The next run should use a GDB command file that:

1. sets the marker breakpoint;
2. starts a 120-180 second timer before the first `continue`;
3. if the marker hits, proceeds to the trace-lock address breakpoint;
4. if the timer fires before the marker, interrupts immediately and collects `thread apply all bt`, registers, `g_prt_debug_state`, `g_prt_debug_tls_state`, and `g_prt_gdb_marker_state`;
5. detaches cleanly before the host watchdog has a chance to terminate the run.

This will distinguish "slow or stuck before after-build marker" from "post after-build trace/progress path issue" without relying on guest file logs.
