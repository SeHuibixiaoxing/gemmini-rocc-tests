# 2026-05-09T01:15Z sbus64 expect marker hit but trace-lock frontier misarmed

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- GDB helper: `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_frontier_expect.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.228`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-frontier-expect-20260509T005921Z-192_168_1_228-172_16_0_2/pairdummy-cfg32-marker-frontier.expect.log`
- Frontier file: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-frontier-expect-20260509T005921Z-192_168_1_228-172_16_0_2/frontier.gdb`
- Runworkload pane log: `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-005112.pane.log`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PRT_GDB_MARKER_TIMEOUT=300
PRT_GDB_FRONTIER_TIMEOUT=120
```

GDB was the first TCP client to the guest `gdbserver --once`; no TCP port probe was used. Guest file logs, deep logs, audit logs, checkpoint logs, breadcrumbs, debug triggers, and DMA probes were disabled.

## Result

The new expect/PTTY helper successfully connected and hit `worker-after-build-stage-task`:

```text
Thread 2 "rerocc_pipeline" hit Breakpoint 1, prt_gdb_marker_stop ()
    at .../pipeline-runtime/src/prt_debug_state.c:396
```

Marker state:

```text
site_id = 24
segment_idx = 2
global_stage_id = 3
local_stage_id = 1
subbatch_id = 0
manager_id = 4
rc = 0
aux0 = 1
aux1 = 4
line = 4430
```

This restores the previously proven frontier and corrects the previous two misleading runs:

- the earlier batch-GDB `Remote connection closed` run did not prove a pre-after-build hang;
- the bounded batch-GDB timer run did not prove a pre-after-build hang;
- with expect/PTTY control, the same after-build marker is reachable again.

## Thread State At Marker

At the after-build marker:

- Thread 2 is stopped in `prt_gdb_marker_stop()` called from `stage_worker_main()` at `prt_runtime.c:4423`.
- Thread 3 is in the C1/DMA path:
  `prt_process_c1()` -> `prt_dma_copy_dram_to_spm_pages_prefix()` -> `dma_copy_host_to_spm_pages_linux()` -> `dma_submit_wait_annotated_scoped()` -> `dma_blocking_submit()` -> `dma_should_checkpoint_submit_wait_tok()`.
- Thread 4 is waiting for pipebuf full in `prt_pipebuf_wait_full()`.
- Thread 1 is in `prt_runtime_run()` sleeping.

These stacks are useful context, but the important frontier is still thread 2 after the after-build marker.

## Frontier Failure

The intended next step was to arm a temporary breakpoint at `0x3054e`, the tested ELF address for `pthread_mutex_lock(&rt->state_lock)` inside `prt_trace_on_gemm_issue()`.

The generated `frontier.gdb` was malformed:

```text
1 printf "
2 --- arming trace-lock tbreak ---
3 "
4 tbreak *0x3054e
```

GDB reported:

```text
frontier.gdb:1: Error in sourced command file:
Bad format string, non-terminated '"'.
```

Because the helper did not treat `source frontier.gdb` errors as fatal, it continued without the `tbreak *0x3054e` frontier being armed. The later timeout and Ctrl-C behavior therefore cannot be used to locate the post-after-build blocker.

## Current Frontier

The real runtime frontier has **not** moved:

```text
worker-after-build-stage-task hit
worker-gemm-run not yet reached
```

The current narrowed source window remains the small region in `stage_worker_main()` after `worker-after-build-stage-task` and before `worker-gemm-run`, including:

- log gate context/deep-log checks;
- `tensor_log_stage_entry_inputs()`;
- disabled debug-trigger note;
- `PRT_PROGRESS_LOG("worker ... begin ...")`;
- `prt_now_ns()`;
- `prt_trace_on_gemm_issue(rt)`;
- `prt_trace_log_event(... PRT_TRACE_EVT_GEMM_ISSUE ...)`;
- `prt_debug_state_set_worker(... PRT_DEBUG_PHASE_GEMM_RUN)`;
- `worker-gemm-run` marker.

This run does not prove a `rt->state_lock` block because the trace-lock breakpoint was not installed.

## Cleanup

The wrapper called `terminaterunfarm --forceterminate`; a follow-up AWS check showed no active `f2.*` instances. Stale local `runworkload` Python/tmux wrapper processes from this already-terminated run were killed. No local GDB or SSH tunnel remained.

## Next Step

Fix the helper so a frontier `source` error is fatal, then rerun with a minimal frontier command:

```text
tbreak *0x3054e
```

Do not include a GDB `printf` command in `PRT_GDB_FRONTIER_GDB_CMDS` unless the helper stops expanding backslash escapes inside command strings.
