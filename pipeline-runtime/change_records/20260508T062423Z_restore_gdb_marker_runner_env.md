# 2026-05-08 06:24 UTC: restore runner propagation of GDB marker env

## Problem

The next planned FPGA run depends on:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin
```

being visible inside `rerocc_pipeline_runtime-linux`. A dirty runner diff had
removed the `PIPELINE_RUNTIME_GDB_MARKER_*` defaults, status logging, and
environment propagation from
`run_rerocc_pipeline_runtime_bertmini.sh`.

With that state, `render_pairdummy_guest_env.sh` could still render the marker
settings into `/firemarshal.env`, but the runner would not pass them to the
gdbserver-launched inferior. The target would initialize the marker filter with
its default disabled state, so `prt_gdb_marker_stop()` would never be reached
for the synthetic prefault frontier.

## Change

- Restored `PIPELINE_RUNTIME_GDB_MARKER_*` default reads in the runner.
- Restored runner and console logging of the marker filter.
- Restored marker environment propagation in both the gdbserver and direct
  binary launch paths.

## Verification

Host-side checks:

```sh
bash -n rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
rg -n "PIPELINE_RUNTIME_GDB_MARKER_ENABLE|runner-gdb-marker-config|gdb-marker enable" rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
```

This is a control-plane fix only. It does not change DMA completion logic;
doneflag remains known-bad and must not be used as a completion/pass signal.
