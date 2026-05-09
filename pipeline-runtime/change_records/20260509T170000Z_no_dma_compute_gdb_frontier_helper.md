# 20260509T170000Z - no-DMA compute GDB frontier helper

## Context

The no-DMA bisection has ruled out artifact/YAML/rootfs reading, fixed-load
DMA, export DMA, and the segment 1 local stage 0 SPM xlate
release/restore/flush loop for subbatch 3. The next frontier is the compute
path after the `worker-gemm-run` marker:

```text
stage_worker_main()
  prt_gemm_conv_run()
    gemm_issue_task()
      run_conv_oc_split()
        conv_call_for_manager_sync_strided()
          pointwise matmul issue
          rr_fence / gemmini_fence / drain / rr_release
```

Static review of the no-DMA switch shows it skips DMA transport and export
paths, but it does not skip YAML/artifact parsing or model/source slice reads.
That matches the latest GDB evidence: no-DMA reached
`worker-before-build-stage-task` and returned through SPM xlate flush.

## Change

Added:

- `pipeline-runtime/scripts/gdb/sbus64_no_dma_segment1_stage0_compute_frontier.gdb`
- `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_compute_frontier.sh`

The wrapper expects a run prepared with:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3
```

It reuses `run_pairdummy_cfg32_gdbserver_marker_frontier_expect.sh`, keeping
GDB as the first TCP client and using a bounded frontier continue. If the
frontier times out, the expect wrapper sends Ctrl-C and records thread stacks,
registers, and marker/debug state instead of only killing GDB.

## Intended Evidence

The helper logs milestones through:

- `prt_gemm_conv_run`, `gemm_async_conv_run`, `gemm_issue_task`
- `gemm_issue_conv_task`, `run_conv_oc_split`
- `conv_call_for_manager_sync_strided` / `conv_call_for_manager_sync`
- pointwise inner `tiled_matmul_nn_stride_auto()` call and return
- ReRoCC acquire/fence/release entry and return-side split points
- `flush_scope_after_drain()` entry, with outer call/return boundaries around
  the drain

It stops at `prt_runtime.c:4545`, immediately after `prt_gemm_conv_run()`
returns. Hitting that breakpoint proves the no-DMA compute path returned for
the selected segment/stage/subbatch. A timeout stack should identify the
current stuck instruction or function.

## Limitations

This helper only prepares the next F2 round. It does not itself prove no-DMA
completion or DMA completion. `doneflag` is not used.

## Local Validation

Validated locally before launching another F2 round:

```bash
bash -n pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_compute_frontier.sh \
  pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_frontier_expect.sh
```

The GDB command file was also sourced against the host-side RISC-V ELF
`build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
(`ed592ca0530050c8bec12c4dbecf47879989f63a91286cb6ae92f336a3479850`) after
temporarily omitting `set scheduler-locking on`, which requires a live remote
target. GDB resolved the intended symbol/source breakpoints without command
file syntax errors.
