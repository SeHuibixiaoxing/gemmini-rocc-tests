# 2026-05-08 02:20 UTC: disable doneflag polling as DMA completion path

## Context

The current dummy8x8/sbus64 cfg32 NIC gdbserver runs were still exporting
`PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1` through the fixed
pairdummy environment. That made `dma_blocking_wait()` call
`dma_blocking_wait_poll_doneflag()` and skip `hw_dma_fence()` when the flag was
observed set.

This violates the project guardrail that the Linux DMA completion semantics
must use the blocking wait/fence path. The completion flag is only an auxiliary
observation point.

## Change

- Changed the fixed pairdummy profile default to:
  `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`.
- Bumped profile ids so freshness checks can distinguish rebuilt images:
  - `pairdummy-sbus128-fixed-v27`
  - `pairdummy-sbus64-dummy8x8-fixed-v3`
- Hardened `dma_blocking_wait_poll_timeout_enabled()` to return `0`
  unconditionally, so an accidental environment override cannot make doneflag
  polling the main completion path again.

## Expected behavior

`dma_blocking_wait()` should now always enter the `hw_dma_fence()` wait path on
Linux. Doneflag state may still be refreshed/logged after the fence, but it no
longer determines DMA completion or bypasses the wait method.

## Static/build verification

Completed on 2026-05-08:

- `bash -n` passed for the fixed env scripts and the active sbus64 gdbserver
  workflow wrapper.
- Sourcing `pairdummy_sbus64_dummy8x8_fixed_env.sh` reports:
  - `PIPELINE_RUNTIME_PROFILE_ID=pairdummy-sbus64-dummy8x8-fixed-v3`
  - `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`
- `git diff --check` passed for the changed files.
- Host build passed:
  `make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all`
- RISC-V Linux target build passed:
  `make -C generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests -f .../rerocc-linux-tests/Makefile abs_top_srcdir=... src_dir=... rerocc_pipeline_runtime-linux`

## Dynamic verification still required

The next runtime image rebuild and gdbserver run must confirm:

- guest environment reports
  `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`;
- sparse logs do not contain `dma-wait-doneflag-poll phase=begin`;
- GDB/breadcrumb frontier reaches `hw_dma_fence()` / post-fence markers instead
  of doneflag-poll markers.
