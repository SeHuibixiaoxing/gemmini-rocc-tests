# 2026-05-08T23:05Z sbus64 dummy8x8 after-build-stage-task marker hit

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.253`
- Instance: `i-0e0a31b5f42b7452e`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T230038Z-192_168_1_253-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

The image freshness check confirmed the active guest environment had guest logs, deep logs, audit logs, checkpoint logs, breadcrumbs, debug triggers, and DMA probes disabled. The host waited for UART `[gdbserver] phase=listening`; GDB was the first TCP client to the guest gdbserver endpoint.

## Result

The marker hit successfully:

```text
site_id=24
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=4
tensor_id=4294967295
page_idx=4294967295
token_id=4294967295
rc=0
aux0=1
aux1=4
line=4430
```

Interpretation of marker payload:

- `site_id=24` is `worker-after-build-stage-task`.
- `rc=0` proves `build_stage_task_desc()` returned successfully for segment2/globalStage3/localStage1/subbatch0.
- `aux0=1` is `task.op_kind`.
- `aux1=4` is `task.tile_count`.
- `manager_id=4` is the selected DMA manager for local stage 1.

The current backtrace stopped at:

```text
prt_gdb_marker_stop()
prt_gdb_marker_note(... site_id=24 ...)
prt_runtime_gdb_marker(... line=4430 ...)
stage_worker_main() at prt_runtime.c:4423
```

## Concurrent State At Marker Stop

At the marker stop:

- Thread 4 was still blocked in `prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000)` from `stage_worker_main()` around `prt_runtime.c:4315`.
- Thread 3 was in stage0 C1 fixed-load DMA wait:
  `hw_dma_fence()` -> `dma_blocking_wait()` -> `prt_dma_wait()` -> `dma_submit_wait_annotated_scoped()` -> `dma_copy_host_to_spm_pages_linux()` -> `prt_process_c1()`.
- The DMA wait context had `stage_idx=0`, `tensor_id=0`, and `debug_page_idx=17`.
- Thread 1 was sleeping in `prt_runtime_run()` around `prt_runtime.c:5408`.

This reinforces the repeated pattern: while the segment2/stage1 worker reaches compute preparation, another worker is still actively issuing or waiting on stage0 C1 fixed-load DMA, and the later worker remains blocked on an input pipe buffer.

## Updated Frontier

Ruled out:

- Permanent failure before `worker-before-build-stage-task`.
- Permanent failure inside `build_stage_task_desc()` for segment2/globalStage3/localStage1/subbatch0.

Still open:

- Whether `worker-gemm-run` is reached for the same segment/stage/subbatch.
- Whether the stall is inside stage1 compute, after stage1 compute/export, or caused by the stage0 C1 fixed-load DMA path delaying downstream readiness.
- Whether the repeated stage0 C1 wait at page 17 is a normal long DMA wait or an actual contributing blocker.

## Next Probe

The next low-disturbance run should target:

```text
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

If `worker-gemm-run` hits, the frontier moves into or beyond stage1 compute. If it misses again, the focus should be on the short region after task construction and before compute launch, plus the concurrent stage0 C1 DMA wait.

## Cleanup

The run farm was terminated after GDB evidence collection. Instance `i-0e0a31b5f42b7452e` was confirmed `terminated`.
