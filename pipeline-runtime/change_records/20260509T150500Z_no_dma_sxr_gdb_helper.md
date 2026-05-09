# 20260509T150500Z - no-DMA SPM xlate release GDB helper

## Summary

Added a narrow GDB helper for the no-DMA compute frontier found on
2026-05-09. This is a debugging workflow change only; it does not change
pipeline-runtime execution behavior.

## Files

- `pipeline-runtime/scripts/gdb/sbus64_no_dma_segment1_stage0_sxr_release.gdb`
  - Starts after the existing `worker-before-build-stage-task` marker.
  - Locks the selected worker thread.
  - Breaks only on
    `prt_rr_release_scope(scope={manager_id=6,cfg_id=31,opcode_id=3})`.
  - Uses temporary source-line breakpoints to distinguish:
    - return from `rr_release(cfg31)`;
    - entry to `rr_read_csr(CSR_RRCFG31)`;
    - return from the readback.
- `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_sxr_release.sh`
  - Wraps the existing marker-stop helper.
  - Documents the required guest environment:
    `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`,
    `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`,
    and the segment1/stage0/subbatch3 marker filter.

## Validation

Local validation only:

```text
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_sxr_release.sh --help
```

The helper has not yet been exercised on F2. The last F2 no-DMA run is recorded
in:

```text
pipeline-runtime/debug_records/20260509T145959Z_no_dma_spm_xlate_release_frontier.md
```
