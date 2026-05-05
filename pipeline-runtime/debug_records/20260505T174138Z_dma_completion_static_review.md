# 20260505T174138Z - DMA completion static review

## Goal

Use bitstream wait time to pin down exactly what current software and hardware
mean by DMA completion before the next `cfg32_nic` remote gdbserver run.

## Files read

- `pipeline-runtime/src/prt_dma.c`
- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`

## Findings

- Software uses a runtime-level completion pool, not token stack addresses.
  The pool is page-aligned, prefaulted, `mlock()`ed, translated to PA per slot,
  and then reused by token acquire/release.
- The blocking wait path reads the completion flag, executes `hw_dma_fence()`,
  then reads the flag again.
- Hardware writes the completion flag after the copy FSM reaches the end of the
  request. The flag write is a TileLink Put to `curCompletionAddr`, which is the
  host PA supplied by software. It is not an SPM alias address.
- `FUNCT_CHECK_COMPLETION` is ready only when `!dmaBusy`; current
  `hw_dma_fence()` is therefore a per-DMA-manager idle wait, not a token-id wait.
- This is acceptable for the current blocking path because it uses one
  outstanding token per manager. It is not sufficient as a per-token completion
  primitive if future code allows multiple outstanding copies on one DMA manager.

## Documentation

Added:

- `docs/testing/dma_completion_static_note_20260505.md`

Updated:

- `docs/testing/pipeline_runtime_optimization_actions_20260505_draft.md`

## gdbserver implication

If the first `cfg32_nic` gdbserver run stops in DMA wait:

- stuck before or inside `hw_dma_fence()` points at DMA FSM, TileLink,
  SPM xlate/PTW, ReRoCC scope, or manager ownership;
- fence returns but `tok->hw_done_flag == 0` points at completion flag PA,
  flag write, or coherence/visibility;
- fence returns and flag is 1 but execution still hangs points past DMA wait.

## Limitations

This checkpoint is static only. It does not prove F2 DMA behavior and does not
replace the upcoming AGFI/gdbserver test.
