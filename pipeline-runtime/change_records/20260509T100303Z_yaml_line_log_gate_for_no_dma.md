# 20260509T100303Z YAML line log gate for no-DMA

## Change

- Added `PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE`.
- Defaulted per-line YAML parser progress logging to off.
- Kept coarse YAML parser milestones enabled: file read, parse begin/end,
  validation begin/end, stage-push, and load end.
- Propagated the variable through the sbus64 fixed env, effective guest env
  renderer, host-init generated `/firemarshal.env`, FireMarshal wrapper, and
  bertmini runner.
- Realigned the checked-in workload overlay wrapper with the source wrapper so
  the already-existing no-DMA pass-through is present in both copies.

## Why

The `20260509T092614Z` no-DMA F2 run reached `runtime_run` and produced sparse
guest logs, but the last visible line was a per-line YAML parser message:

```text
[prt-progress] yaml pipeline parse line=1439 indent=6 text=vAccIdxList:
```

Static inspection of the YAML and host no-DMA dry-run did not indicate a parser
bug. For the next F2 bisection, the per-line parser log is too intrusive and
may only show the rootfs flush frontier. The next run should keep sparse coarse
progress while suppressing per-line YAML text.

## Validation

- `bash -n` passed for:
  - `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_fixed_env.sh`
  - `pipeline-runtime/scripts/render_pairdummy_guest_env.sh`
  - `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
  - `pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh`
  - `pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
  - `rerocc-linux-tests-coupleddma/workload/host-init.sh`
  - `rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
- `sh -n` passed for both FireMarshal wrapper copies.
- `git diff --check` passed.
- Source runner/wrapper and overlay copies are byte-identical after the update.
- Effective sbus64 no-DMA guest env render includes:
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
  - `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
  - `PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE=0`
  - `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=1`
  - `CAPTURE_PERIODIC_SYNC_ENABLE=1`
  - `CAPTURE_PERIODIC_SYNC_SECONDS=5`
- Workflow `debug-preflight` passed for that env with probe tier `2`.
- Host clean build passed:

```sh
make -C pipeline-runtime clean all \
  PIPELINE_RUNTIME_PROGRESS=1 \
  PIPELINE_RUNTIME_MLOCKALL_MODE=0
```

- Host CPU no-DMA dry-run for dummy8x8/sbus64/ours2, batch 8, pair-manager
  mode, skipped model/input/golden, and `PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE=0`
  exited 0 with:
  - `line_logs=0`
  - `stage_push=40`
  - `parse_end=1`
  - `runtime_end=1`
- Target fixed-env `HOST_INIT_CHECK_ONLY=1` passed.

## Next

Rebuild or patch the FireMarshal image, rerun `infrasetup`, then launch the
same F2 no-DMA compute bisection with YAML line logging disabled. This change
does not provide DMA-completion evidence and does not use doneflag polling.
