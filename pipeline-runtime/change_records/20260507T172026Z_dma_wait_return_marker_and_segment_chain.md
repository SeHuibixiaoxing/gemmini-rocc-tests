# 20260507T172026Z DMA wait-return marker and segment-chain GDB helper

## Context

The `20260507T171343Z` init marker-chain run proved that GDB can control the
current dummy8x8/sbus64 AGFI through `runtime-ready`. The next frontier is
post-init segment/worker/export/DMA execution.

During static review of the post-init marker path, `dma_blocking_wait()` had an
observability hole: when `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE`
is enabled and the done-flag poll returns `PRT_ERR_TIMEOUT`, the function
returned immediately without emitting `PRT_GDB_MARKER_SITE_DMA_WAIT_RETURN`.
That could make a GDB run waiting for `dma-wait-return` look like an
unrecoverable wait hang even when the function already returned with timeout.

## Changes

- Added `dma_gdb_marker_wait_return()` in `pipeline-runtime/src/prt_dma.c`.
- Reused it for the normal `PRT_OK` wait return marker.
- Emitted the same `dma-wait-return` marker with `rc=PRT_ERR_TIMEOUT` before
  the done-flag poll timeout return.
- Added
  `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain.sh`.
  The helper starts from a `segment-begin` source marker, then advances
  `g_prt_gdb_marker_filter` in the same GDB session through:
  `worker-create`, `worker-entry`, first stage0 DMA wait enter/return,
  `worker-gemm-run`, `worker-export-sync`, `export-sync-tensor`,
  `export-alias-target-begin`, `dma-export-page-submit-begin`,
  export DMA wait enter/return, `dma-export-page-submit-end`, and
  `export-alias-target-end`.

## Verification

Built the Linux runtime after the edit:

```bash
CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc \
make -C generators/gemmini/software/gemmini-rocc-tests/build rerocc-linux-tests -j1
```

The build completed successfully.

The new GDB helper was syntax-checked via:

```bash
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain.sh --help
```

## Expected next test

Build or patch the guest image with:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=segment-begin
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0
```

Then run the new helper against a fresh `gdbserver --once` workload. If it
times out, the last printed section names the narrowed post-init frontier.
