# 2026-05-08 02:45 UTC: make safe GDB marker helper wait for runtime marker init

## Context

The safe gdbserver helper connected successfully and set `g_prt_gdb_marker_filter`
immediately after `target remote`, but the target did not stop at the expected
`segment-begin` marker. Guest logs showed the runtime had already progressed
past segment and worker activity, so the failure was in the helper's marker
arming path rather than proof that the runtime had not reached the marker.

Static review found that `prt_runtime_init()` calls `prt_gdb_marker_init_from_env()`
after the helper's early GDB writes. With the fixed guest profile keeping
`PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0`, that runtime init overwrote the helper's
`enabled=1` filter setting.

## Change

- Updated `scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh`
  to set a temporary breakpoint on `prt_gdb_marker_init_from_env()`.
- The helper now continues until that runtime env init is hit, finishes the
  function, and only then writes `g_prt_gdb_marker_filter` and arms
  `prt_gdb_marker_stop`.

## Expected effect

The helper should work with fixed guest images that keep
`PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0`, without requiring a guest image rebuild.
The first real marker wait should no longer be disabled by runtime env init.

## Verification planned

- `bash -n` on the helper script.
- Next gdbserver run should show:
  - `PRT_SAFE_WAIT_RUNTIME_MARKER_INIT`
  - `PRT_SAFE_HIT_RUNTIME_MARKER_INIT`
  - `PRT_SAFE_AFTER_RUNTIME_MARKER_INIT`
  - then a hit on `segment-begin-env` or a captured timeout after the filter is
    known to be armed.
