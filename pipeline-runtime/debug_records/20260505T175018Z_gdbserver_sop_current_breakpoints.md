# 20260505T175018Z - gdbserver SOP breakpoint refresh

## Goal

Make the live `cfg32_nic` gdbserver SOP match the current pipeline-runtime
symbol names before a new AGFI becomes available.

## Finding

The SOP still suggested several older breakpoint names:

- `prt_schedule_action_submit`
- `prt_dma_submit_tokenized`
- `prt_rr_release_and_fence`

These are not current symbols in the 2026-05-05 source tree and would waste time
in the first `gdbserver --once` live session.

## Change

Updated `docs/testing/gdbserver_integration_sop.md` to use current symbols:

- `prt_runtime_run`
- `prt_action_bind_topology`
- `stage_prepare_exec_views`
- `prt_dma_submit`
- `prt_dma_wait`
- `dma_blocking_wait`
- `hw_dma_fence`
- `prt_gemmini_spm_xlate_program`
- `prt_gemmini_spm_xlate_flush`
- `prt_rr_release_scope`
- `prt_gemm_conv_run`
- `prt_gemm_fence`

Also added the minimal first-pass `cfg32_nic` breakpoint set and the key token
fields to inspect when stopped in DMA wait.

## Validation

Used `rg` over `pipeline-runtime/src` and `pipeline-runtime/include` to confirm
the replacement symbols exist in the current source tree.

## Limitations

Static documentation update only. It does not exercise gdbserver or the target
binary yet.
