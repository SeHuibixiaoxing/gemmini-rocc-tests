# 20260509T073935Z no-dma compute debug mode

## Change
- Added debug-only `--no-dma-compute` / `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`.
- The mode skips real DMA copy/submit/wait instructions while preserving allocation,
  SPM xlate bind/flush, Gemmini issue/fence, pipe buffer state, ring state, and
  subbatch progression.
- Golden compare is not allowed as evidence in this mode: the runner forces
  `--skip-golden-check`, and direct CLI use fails if `--golden` is present without
  `--skip-golden-check`.

## Runtime Paths
- Fixed tensor DRAM-to-SPM loads now validate/bind pages but skip the hardware DMA copy.
- Scheduler C1/C2/C3/C5/C6 transports advance as successful no-DMA transfers.
- Export alias SPM-page copies and deep SPM digest reads are skipped under no-DMA.
- Async export progress fails fast if no-DMA mode ever observes a live DMA token.

## Workflow Plumbing
- Propagated `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE` through the guest runner,
  file-only FireMarshal wrapper, host-init generated env, pairdummy workflow display,
  image freshness summaries, and `scripts/firesim-tmux-run.sh`.
- Added header dependencies to the host `pipeline-runtime/Makefile`; this avoids
  stale object files after public runtime struct changes.

## Validation
- `bash -n` / `sh -n` passed for the touched runner, wrapper, workflow,
  env-render, host-init, and FireSim tmux scripts.
- `make clean` then `make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all`
  completed successfully.
- `./pipeline_runtime --hw-validate-only --backend cpu --no-dma-compute`
  printed `HW_VALIDATE_ONLY_PASS`.
- Direct CLI use with `--no-dma-compute --golden ...` and no
  `--skip-golden-check` exited 2 with the expected diagnostic.
- Host CPU dry-run for `dummy8x8/sbus64/ours2`, batch 8, pair-manager mode,
  skipped model/input/golden, and `--no-dma-compute` exited 0.
- RISC-V Linux `rerocc_pipeline_runtime-linux` cross-build completed under
  `generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests`.
- `strings` on that Linux binary confirms `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE`
  and `--no-dma-compute` are present.
- Static artifact audit passed for
  `rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64`
  with `--page-size-bytes 1024`.
- No F2 run was launched for this change record.
