# 2026-05-08 02:35 UTC: let the safe GDB helper enable marker filters

## Context

The fixed pairdummy profiles intentionally keep
`PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0` by default to avoid marker overhead in
ordinary runs. The current gdbserver safe-chain helper previously assumed the
guest image had marker filtering enabled before launch, which would require a
new image rebuild or an environment overlay.

## Change

- Updated `run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh` so
  after `target remote` it sets:
  - `g_prt_gdb_marker_filter.initialized = 1`
  - `g_prt_gdb_marker_filter.enabled = 1`
- The helper now installs the initial segment-begin filter through GDB before
  setting `break prt_gdb_marker_stop`.
- The usage text now documents that the fixed guest image may keep markers
  disabled by default.

## Verification

- `bash -n` passed for the helper.
- `git diff --check` passed for the helper.

## Runtime note

This does not probe guest port `2345`; the first TCP connection to gdbserver is
still `target remote` from GDB.
