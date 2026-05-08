# 2026-05-08 09:22Z dummy8x8 sbus64 GDB fixed-load page63 hit scheduler C1; helper aborted on optimized local

## Scope

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Run host: `i-0068e4974af4cc317`, private IP `192.168.1.178`
- GDB helper:
  `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh`
- Artifact directory:
  `pipeline-runtime/debug_records/artifacts/20260508T092216Z_dummy8x8_sbus64_gdb_fixed_page63_hit_scheduler_c1_helper_abort/`

## Guest Marker Configuration

The image was patched and freshness-checked with:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-fixed-load-submitwait-begin
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_PAGE=63
```

`PIPELINE_RUNTIME_GDB_MARKER_TENSOR` and `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH`
were intentionally left unset to avoid repeating the previous too-narrow marker
miss. This was too broad for page63.

`PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`; this run did not use
DMA doneflag polling as completion evidence.

## GDB Result

GDB was the first TCP client to the guest `gdbserver --once` session. No
`nc`/`curl`/`telnet` probe was used.

The marker hit quickly:

```text
site_id=27
segment_idx=0 global_stage_id=0 local_stage_id=0 subbatch_id=0
manager_id=0 tensor_id=0 page_idx=63 token_id=4294967295 line=865
```

The backtrace proves this was not the intended `stage_prepare_exec_views()`
fixed-load tensor. It was the scheduler C1 path:

```text
dma_copy_host_to_spm_pages_linux(... tensor_id=0 ...) at prt_dma.c:863
prt_dma_copy_dram_to_spm_pages_prefix(...) at prt_dma.c:2834
prt_process_c1(...) at prt_scheduler.c:294
stage_worker_main(...) at prt_runtime.c:4236
```

The helper then reached:

- `dma_submit_wait_annotated_scoped()`
- `prt_dma_submit()`
- `dma_blocking_wait()`
- before `hw_dma_fence()`
- after `hw_dma_fence()`, at `dma_completion_flag_refresh()`

At the after-fence stop, the generated GDB command file attempted:

```gdb
print fence_status
```

but the current frame was an inlined `dma_completion_flag_value()` frame and
`fence_status` was optimized out / not in scope. Batch GDB aborted with:

```text
No symbol "fence_status" in current context.
gdb_rc=1
```

## Interpretation

- The new helper line fixes for `hw_dma_fence()` are valid: GDB stopped before
  and after the hardware fence.
- The broad page63 filter is invalid for the intended frontier because it first
  catches the scheduler C1 DMA page63 copy, where marker `tensor_id=0`.
- The next run must set `PIPELINE_RUNTIME_GDB_MARKER_TENSOR=1000001` to target
  `stage_prepare_exec_views()` fixed-load tensor 1000001.
- The helper must not rely on optimized local symbols such as `fence_status`.
  It should print token fields and instruction windows only, or tolerate absent
  optimized locals.

## Post-Abort Guest State

After GDB aborted, the inferior continued far past the C1 page63 wait. Guest
sparse log reached segment0 SPM xlate flush:

```text
[prt-progress] segment=0 flush-spm-xlate begin
...
[prt-progress] spm-xlate-release mgr=7 cfg=31 phase=restore-end prev_opc3=0x0
[prt-progress] segment=0 flush-spm-xlate end
[prt-progress] segment=0 begin stages=1 sinks=1 subbatch_size=1 target_batch=8 target_subbatch=8
```

So this run does not locate the terminal stall. It only identifies the marker
selection bug and a helper robustness bug.

## Artifacts

- GDB transcript:
  `artifacts/20260508T092216Z_dummy8x8_sbus64_gdb_fixed_page63_hit_scheduler_c1_helper_abort/gdb/pairdummy-cfg32-marker-stop.gdb.log`
- Generated post-marker command file:
  `artifacts/20260508T092216Z_dummy8x8_sbus64_gdb_fixed_page63_hit_scheduler_c1_helper_abort/gdb/fixed-load-page-frontier.gdb`
- Guest sparse log:
  `artifacts/20260508T092216Z_dummy8x8_sbus64_gdb_fixed_page63_hit_scheduler_c1_helper_abort/guest/bertmini-batch8.log`
- Breadcrumb decode:
  `artifacts/20260508T092216Z_dummy8x8_sbus64_gdb_fixed_page63_hit_scheduler_c1_helper_abort/guest/bertmini-batch8.breadcrumb.decoded.txt`
- Manager pane log:
  `artifacts/20260508T092216Z_dummy8x8_sbus64_gdb_fixed_page63_hit_scheduler_c1_helper_abort/manager/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-091224.pane.log`

The F2 run farm was terminated after artifact capture, and AWS showed no live
F2 instances.

## Next Step

1. Fix the fixed-load GDB helper to avoid `print fence_status`.
2. Rebuild only the guest env/image state, not the bitstream, with
   `PIPELINE_RUNTIME_GDB_MARKER_TENSOR=1000001`.
3. Rerun the same AGFI and stop at tensor1000001 page63, then continue through:
   `dma_gdb_marker_wait_return`, fixed-load page accounted, scope release,
   return to `stage_prepare_exec_views()`, and the
   `stage-fixed-load-sparse phase=end` call site.
