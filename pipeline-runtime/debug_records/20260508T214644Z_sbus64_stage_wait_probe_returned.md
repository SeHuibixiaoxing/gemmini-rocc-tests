# 2026-05-08T21:46Z sbus64 dummy8x8 stage-wait probe: stage1 export readiness returns

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.85`
- GDB script: `pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_stage_wait_probe.gdb`
- GDB transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T214644Z-192_168_1_85-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Guest environment

Low-disturbance GDB-only run:

- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=uart`

GDB was attached only after UART printed `[gdbserver] phase=listening`.

## Marker hit

The run again hit the expected pre-export-ready marker:

```text
site_id=21
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=4
rc=0
aux0=1 export_count
aux1=1 shared_pair_count
line=4352
```

## Stage-wait probe result

The probe entered `stage_wait_exports_ready()` on the marker thread. Due to optimization, the first stop at the call-site alias did not expose arguments, but the next source-line stop at `prt_runtime.c:3592` had usable values:

```text
stage_id=1
subbatch=0
export_count=1
shared_pair_count=1
b=0x96c30
b->buffer_id=7
b->tensor_id=6
b->stage_idx=1
b->segment_idx=2
b->kind=PRT_BUF_C4_SHARED_NO_RING_PAIR
b->in_use_idx=0
b->full[0]=0
b->full[1]=0
b->subbatch_offset=0
b->shared_tag[0]=0
b->shared_tag[1]=0
shared_pairs[0]=0x6b140
```

The next stop was `prt_runtime.c:4360` in `stage_worker_main()` with `rc=0`:

```text
stage_worker_main at prt_runtime.c:4360
rc = 0
export_bufs = {0x96c30, ...}
shared_pairs = {0x6b140, ...}
```

Temporary breakpoints on `prt_process_c4`, `prt_runtime.c:3609`, `prt_runtime.c:3615`, and `prt_runtime.c:3621` did not fire before the return stop. Therefore this run proves that segment2/globalStage3/localStage1/subbatch0 does **not** block in `stage_wait_exports_ready()`. The export buffer is already empty, so the C4 drain path is bypassed and the worker reaches the `worker-after-exports-ready` marker point.

## Concurrent state at the return stop

At the same stop:

- Thread 2, the marker thread, is at `stage_worker_main()` line 4360 and has returned from `stage_wait_exports_ready()` with `rc=0`.
- Thread 4 is blocked in `prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000)` from `stage_worker_main()` line 4315.
- Thread 3 is still in the stage0 C1 load path:
  `prt_dma_token_cleanup()` -> `dma_submit_wait_annotated_scoped()` -> `dma_copy_host_to_spm_pages_linux()` -> `prt_process_c1()` -> `stage_worker_main()`.

This shifts the active frontier away from stage1 export-readiness. The live dependency shape is:

1. stage1 can reach and pass export-readiness;
2. stage2 still waits for a downstream entry buffer, likely the tensor6 C4 shared entry at `buf=0x96f48`;
3. stage0 is still issuing or cleaning up C1 DMA work at the sampled moment.

## Current interpretation

The previous hypothesis "stage1 stuck in `stage_wait_exports_ready()`" is rejected.

The next useful probes are:

- `worker-after-exports-ready` for the same segment/stage/subbatch, to confirm the marker itself fires after line 4360;
- `worker-before-build-stage-task` / `worker-after-build-stage-task`, to see whether stage1 proceeds into task construction;
- `worker-gemm-run`, to test whether the next stable frontier is before or inside stage1 compute;
- if stage1 reaches compute, add a short probe around `prt_gemm_conv_run()` return and `worker-export-sync`.

The C4 propagation question remains open, but the specific C4 **pre-compute drain** at `stage_wait_exports_ready()` is not the blocker in this run.

## Cleanup

GDB detached cleanly with `gdb_rc=0`. The run farm was terminated. Local stale `runworkload`, GDB, and SSH tunnel processes were cleaned up. Instance `i-0e7a50cf624c92d05` was observed in `shutting-down`.
