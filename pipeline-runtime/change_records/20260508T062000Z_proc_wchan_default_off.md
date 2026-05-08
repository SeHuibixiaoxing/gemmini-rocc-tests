# 2026-05-08 06:20 UTC: make proc wchan diagnostics opt-in

## Problem

The `20260508T061508Z` prefault-marker gdbserver run showed a control-plane
hazard: gdbserver had created the inferior and was listening on port 2345, but
runner telemetry stopped before `after-bin-spawn`. The captured
`runner-proc.stage` ended in the middle of a diagnostic record near:

```text
wchan=
```

That made the live run look as if gdbserver had not started, even though
`bertmini-batch8.gdbserver.log` already contained:

```text
Process /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux created; pid = 235
Listening on port 2345
```

Process diagnostics must not block or obscure the gdbserver control path.

## Change

- Added `PIPELINE_RUNTIME_RUNNER_PROC_WCHAN_ENABLE`, default `0`, to
  `run_rerocc_pipeline_runtime_bertmini.sh`.
- Added `PIPELINE_RUNTIME_WRAPPER_PROC_WCHAN_ENABLE`, default `0`, to the
  file-only wrapper capture scripts.
- When the corresponding flag is `0`, the diagnostic record writes
  `wchan=skipped` instead of reading `/proc/<pid>/wchan`.
- The existing `/proc/<pid>/status` and cmdline diagnostics remain enabled.

## Verification

Host-side syntax and whitespace checks:

```sh
bash -n rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
bash -n rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh
bash -n rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh
git diff --check -- <changed files>
```

## Operational Note

For the active FireMarshal path, the generated ignored
`software/firemarshal/boards/firechip/distros/br/overlay/firemarshal.sh` was
also patched locally before the next run so the immediate image uses the same
`wchan=skipped` behavior. The tracked source copies above are the durable
change.

doneflag remains known-bad as a completion signal and is unrelated to this
change.
