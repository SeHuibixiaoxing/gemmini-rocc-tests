# 20260509T091000Z no-DMA guest-log observability fix

## Change

- Propagated `PIPELINE_RUNTIME_GUEST_LOG_PATH`,
  `PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH`, `PIPELINE_RUNTIME_AUDIT_LOG_PATH`,
  and `PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH` from the bertmini runner into the
  runtime binary environment for both direct and gdbserver launch paths.
- Kept the checked-in runner and workload overlay runner copies aligned.
- Added sbus64 fixed-profile overrides for:
  - `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE`
  - `CAPTURE_PERIODIC_SYNC_ENABLE`
  - `CAPTURE_PERIODIC_SYNC_SECONDS`

## Why

The `20260509T084710Z` no-DMA F2 rerun enabled guest logs, but live debugfs still
showed a zero-byte `bertmini-batch8.log`. Static inspection showed that the
wrapper set the guest log paths for the runner, while the runner did not pass
those paths to `rerocc_pipeline_runtime-linux`. With UART progress logging off,
runtime progress logs had no output fd.

## Validation

- `sh -n` passed for both runner copies.
- `bash -n` passed for `pairdummy_sbus64_dummy8x8_fixed_env.sh`.
- Workflow `show` with
  `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1 CAPTURE_PERIODIC_SYNC_ENABLE=1 PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=1`
  reports `guest_log_enable=1` and `periodic_sync_enable=1`.

No F2 run has been launched for this fix yet.
