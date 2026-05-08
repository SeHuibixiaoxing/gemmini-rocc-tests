# 2026-05-08T22:43Z sbus64 dummy8x8 before-build-stage-task marker hit

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.132`
- Instance: `i-03179cf1353631dbd`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T223902Z-192_168_1_132-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

The image freshness check confirmed the active guest environment had:

- guest log, deep log, audit log, checkpoint log: disabled
- breadcrumb and debug-trigger probes: disabled
- DMA export/fixed-load probes: disabled
- `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1`
- `EXPORT_DMA_TIMEOUT_MS=5000`
- gdbserver enabled on `0.0.0.0:2345`

The host waited for UART `[gdbserver] phase=listening`; no `nc`, `curl`, or other guest-port probe was used before GDB connected.

## Result

The marker hit successfully:

```text
site_id=23
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=4
tensor_id=4294967295
page_idx=4294967295
token_id=4294967295
rc=0
aux0=4
aux1=4
line=4416
```

Interpretation of marker payload:

- `site_id=23` is `worker-before-build-stage-task`.
- `aux0=4` is `task.tile_count`.
- `aux1=4` is `task.manager_ids[0]`.
- `manager_id=4` is the selected DMA manager for local stage 1.

The current backtrace stopped at:

```text
prt_gdb_marker_stop()
prt_gdb_marker_note(... site_id=23 ...)
prt_runtime_gdb_marker(... line=4416 ...)
stage_worker_main() at prt_runtime.c:4409
```

This proves that segment 2 / global stage 3 / local stage 1 / subbatch 0 reaches the build-task preparation point after `stage_wait_exports_ready()` returned successfully.

## Concurrent State At Marker Stop

At the marker stop, the other worker threads were still in the same broad shape as the previous before/after-exports probes:

- Thread 4 was blocked in `prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000)` from `stage_worker_main()` around `prt_runtime.c:4315`.
- Thread 3 was in the stage0 C1 fixed-load path:
  `prt_host_virt_to_phys()` -> `dma_copy_host_to_spm_pages_linux()` -> `prt_dma_copy_dram_to_spm_pages_prefix()` -> `prt_process_c1()` -> `stage_worker_main()`.
- Thread 1 was sleeping in `prt_runtime_run()` around `prt_runtime.c:5408`.

This repeated stack shape means stage2 is still waiting for an input pipe buffer while another worker is actively doing C1 load work. The marker thread itself is no longer before `build_stage_task_desc()`.

## Updated Frontier

Ruled out by the last three GDB runs:

- Permanent failure to reach `worker-before-exports-ready` for segment2/globalStage3/localStage1/subbatch0.
- Permanent block inside `stage_wait_exports_ready()` for this stage/subbatch.
- Permanent failure to reach `worker-after-exports-ready`.
- Permanent block in the small region between after-exports and before-build-stage-task.

Still open:

- Whether `build_stage_task_desc()` returns for this stage/subbatch.
- Whether `worker-after-build-stage-task` is reachable.
- Whether the worker reaches `worker-gemm-run`.
- Whether the eventual hang is inside stage1 compute, after stage1 compute, or caused by the concurrent stage0 C1 fixed-load path starving downstream stages.

## Next Probe

Use the marker site itself again rather than a source-line follow:

```text
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

If that hits with `rc=0`, the next low-disturbance target is `worker-gemm-run` for the same segment/stage/subbatch. If it misses, the focus should move to `build_stage_task_desc()` static audit and a very small command-file probe around task construction, not to guest logging.

## Cleanup

The run farm was terminated immediately after the GDB evidence was collected. At the time this record was written, instance `i-03179cf1353631dbd` had been observed in `shutting-down` and still needed final `terminated` confirmation.
