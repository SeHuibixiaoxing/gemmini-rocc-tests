# 20260505T185000Z GDB first-breakpoint symbol check

## Context

While the cfg32 NIC bitstreams are still building, I checked that the first live
GDB breakpoint set in the gdbserver SOP matches the current target ELF symbols.
This avoids wasting the first `gdbserver --once` connection on stale or missing
breakpoint names.

## Command

```bash
BIN=generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
NM=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-nm
"$NM" -an "$BIN" | rg ' (prt_runtime_run|prt_action_bind_topology|stage_prepare_exec_views|dma_blocking_wait|hw_dma_fence|prt_gemmini_spm_xlate_program|prt_gemmini_spm_xlate_flush|prt_rr_release_scope|prt_gemm_conv_run|prt_gemm_fence|prt_dma_submit|prt_dma_wait)$'
```

## Result

Present symbols:

- `dma_blocking_wait`
- `prt_dma_submit`
- `prt_dma_wait`
- `prt_gemm_conv_run`
- `prt_gemm_fence`
- `prt_rr_release_scope`
- `prt_gemmini_spm_xlate_program`
- `prt_gemmini_spm_xlate_flush`
- `stage_prepare_exec_views`
- `prt_runtime_run`
- `prt_action_bind_topology`

Not present as a stable function symbol:

- `hw_dma_fence`

Reason: `hw_dma_fence` is currently a `static inline` helper in
`pipeline-runtime/src/prt_dma.c`.

## Action

Updated the gdbserver SOP so the first breakpoint set uses `dma_blocking_wait`
instead of `break hw_dma_fence`. If line-level inspection is needed, stop at
`dma_blocking_wait` and step to the `hw_dma_fence()` call site in `prt_dma.c`
around line `3481`.

