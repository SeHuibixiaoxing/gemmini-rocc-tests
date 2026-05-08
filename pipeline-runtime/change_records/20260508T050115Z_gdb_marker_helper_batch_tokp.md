# 20260508T050115Z GDB marker helper batch/tokp fix

## Why

The page57 fixed-load marker run hit the intended runtime marker and reached
`dma_blocking_wait()`, but the generated post-marker GDB commands stopped on
the inline `hw_dma_fence()` frame. At that point `tok` was only visible in the
caller frame, so the script failed with:

```text
No symbol "tok" in current context.
```

GDB still exited with status 0 because the helper used normal command-file
mode rather than batch mode. That made the host-side helper verdict weaker than
the actual guest result.

## Changes

- `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh`
  now saves `tok` as the GDB convenience pointer `$tokp` at
  `dma_blocking_wait()` entry.
- Later post-marker GDB sections print `$tokp->...` so they remain valid when
  the selected frame is the inline `hw_dma_fence()` helper.
- `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh` now
  runs GDB with `-batch -x`. Sourced command-file errors now produce nonzero
  GDB status instead of silently returning success after an error prompt.

## Validation

- `bash -n` passed for both modified helper scripts.
- The installed cross-GDB was checked to confirm `-batch` returns nonzero for
  a command error.
- The fix has not yet been rerun against a fresh `gdbserver --once` workload;
  the previous run's guest artifacts already show `BERTMINI_PIPELINE_RUNTIME_PASS`.

## Constraints

- The helper still must be the first TCP client to guest `gdbserver --once`.
- Do not probe `172.16.0.2:2345` with `nc`, `curl`, or `telnet`.
- Do not use doneflag polling as DMA completion logic. doneflag is known-bad
  for completion decisions and remains telemetry only.
