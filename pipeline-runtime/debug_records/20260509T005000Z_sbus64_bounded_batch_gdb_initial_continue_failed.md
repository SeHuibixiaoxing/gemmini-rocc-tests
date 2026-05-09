# 2026-05-09T00:50Z sbus64 bounded batch-GDB initial continue failed to sample

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- GDB helper: `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh`
- Post-marker GDB file: `pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_after_build_to_trace_lock.gdb`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.160`
- Instance: `i-009dd6e71d049046f`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260509T003749Z-192_168_1_160-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`
- Generated GDB command file: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260509T003749Z-192_168_1_160-172_16_0_2/pairdummy-cfg32-marker-stop.gdb`
- Runworkload pane log: `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-002940.pane.log`
- Results dir: `sims/firesim/deploy/results-workload/2026-05-09--00-29-41-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PRT_GDB_INITIAL_CONTINUE_TIMEOUT=240
PRT_GDB_MARKER_TIMEOUT=900
PRT_GDB_POST_MARKER_GDB_FILE=pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_after_build_to_trace_lock.gdb
```

Image freshness and remote image freshness both passed. The local and remote image hashes matched:

```text
local_sha256=c9bd137bba7f868354dbfe218b0f33b9c82139cea4a15126a77f683f5275b143
remote_sha256=c9bd137bba7f868354dbfe218b0f33b9c82139cea4a15126a77f683f5275b143
```

The guest environment had sparse log, deep log, audit log, checkpoint log, breadcrumb, debug-trigger, DMA export probe, and DMA fixed-load probe disabled. GDB was the first TCP client to the guest `gdbserver --once`; no `nc`/`curl`/TCP probe was used.

## Result

GDB connected and set the marker breakpoint:

```text
0x0000003ff7fec386 in ?? () from .../ld-linux-riscv64-lp64d.so.1
Breakpoint 1 at 0x14482: file .../pipeline-runtime/src/prt_debug_state.c, line 396.
```

The `worker-after-build-stage-task` marker did not print before the 240 second initial-continue window. The new batch-GDB Python timer did not return control to the command file while `continue` was active. An external `SIGINT` sent to the GDB process also did not produce a guest stack; the transcript ended at the `continue` command:

```text
pairdummy-cfg32-marker-stop.gdb:44: Error in sourced command file:
Disconnected from target.
[pairdummy-gdb-marker] gdb_rc=1
```

The generated command file shows line 44 is the initial `continue`, before any marker-state or post-marker commands:

```text
44 continue
45 if $prt_initial_continue_timeout
...
73 printf "\n--- post-marker gdb command file ---\n"
74 source .../sbus64_segment2_stage1_after_build_to_trace_lock.gdb
```

Therefore this run is also inconclusive for the after-build-to-trace-lock window. It does not prove the marker is unreachable and does not prove a `rt->state_lock` problem. It proves the batch-GDB timer approach is not reliable enough for runtime control on this target.

## Termination And Artifacts

After GDB returned `rc=1`, the wrapper called `terminaterunfarm --forceterminate`, which returned 0. A later AWS check showed no active `f2.*` instances and no local GDB/tunnel/runworkload process remained.

The FireSim runworkload pane later reported the simulation job completed/killed and then failed during result copyback and switch cleanup because the instance SSH endpoint was already going away:

```text
Slot 0, Job ... completed!
Warning: local() encountered an error (return code 255) while executing 'rsync ... uartlog ...'
paramiko.ssh_exception.SSHException: Error reading SSH protocol banner
Fatal error: One or more hosts failed while executing task 'kill_switch_wrapper'
```

The result directory contains only `HW_CFG_SUMMARY`, `sim-run.sh`, `.monitoring-dir/...`, and `switch0/switchlog`. It does not contain a copied-back guest `uartlog`.

## Interpretation

The new helper option `PRT_GDB_INITIAL_CONTINUE_TIMEOUT` was conceptually correct but the implementation is wrong for this environment:

- GDB Python `gdb.post_event()` did not run the desired `interrupt` command while batch GDB was blocked in remote `continue`.
- Sending Unix `SIGINT` to the GDB process from outside caused a remote disconnect rather than a clean target stop and prompt.
- Because the target was not stopped cleanly, no `thread apply all bt`, registers, or debug-state output was captured.

The practical correction is to use a PTY/`expect`-driven GDB session for dynamic debugging. Existing expect helpers already send `\003` to the GDB terminal and can collect stacks after a controlled stop. The next helper should use the same pattern for the **initial marker wait**:

1. connect GDB as the first TCP client;
2. set `break prt_gdb_marker_stop`;
3. send `continue`;
4. after a bounded marker wait, send Ctrl-C through the PTY;
5. collect `info threads`, `thread apply all bt full`, registers, PC window, `g_prt_debug_state`, `g_prt_debug_tls_state`, and `g_prt_gdb_marker_state`;
6. detach and quit cleanly.

This should replace the batch-GDB Python timer before any further hardware run that aims to diagnose a missed marker.
