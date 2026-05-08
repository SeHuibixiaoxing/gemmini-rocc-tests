# DMA wait path breakpoints, 2026-05-07

This note records the exact static breakpoints used to split the current
`dma_blocking_wait()` card point after the token-546 frontier hit.

## Scope

The addresses below are for the current host-side ELF:

```text
generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

`readelf -h` reports `Type: EXEC`, not PIE, so these addresses are directly
usable by remote GDB as `break *0x...`.

The current symbol map contains:

```text
0x1552e dma_blocking_wait
0x29682 prt_rr_fence_scope
```

The helper script now supports:

```bash
PRT_GDB_POST_HIT_MODE=path_trace
```

with the default address set:

```text
before_decision=0x15670
poll_env_check=0x15bf4
poll_emit_start=0x15c24
poll_noemit_start=0x15f56
poll_loop_first=0x15ce8:temp
poll_done=0x15de4
poll_ok_exit=0x15e50
poll_timeout=0x15e64
hw_dma_fence=0x15674
after_wait_refresh=0x15688
shared_rr_fence=0x15882
release_scope=0x15af0
token_complete=0x159ea
return=0x15ad4
```

## Interpretation

The first split is `0x15670`, the compiled branch for:

```c
if (timeout_ns != 0ULL && dma_blocking_wait_poll_timeout_enabled()) {
```

Historical next stops from the accidental doneflag-poll build:

- `0x15bf4`: the function took the timeout/poll-enable branch and is checking
  `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE`.
- `0x15674`: poll was not used and the runtime is entering the inlined
  `hw_dma_fence()` path.
- `0x15c24` or `0x15f56`: done-flag polling is starting. `0x15c24` is the
  progress/logging path; `0x15f56` is the no-progress path.
- `0x15ce8`: the first poll-loop iteration reached `prt_now_ns()`.
- `0x15de4`: the completion flag became visible.
- `0x15e64`: the done-flag poll reached the timeout branch.
- `0x15e50`: poll returned `PRT_OK` and `used_doneflag_poll` is being set.
- `0x15688`: execution is past the wait mechanism and refreshing completion
  state before token cleanup.
- `0x15882`: external ReRoCC shared-scope fence is being called.
- `0x15af0`: non-external scope release path; this should not be the token-546
  path if `tok->rr_scope_external == 1`.
- `0x159ea`: token completion mutex path.
- `0x15ad4`: function epilogue.

Correction on 2026-05-08: done-flag polling is no longer an allowed DMA
completion path. Current binaries must not take the poll branch; if this path
trace is repeated, the expected wait-side proof is entry to `hw_dma_fence()` and
post-fence refresh/cleanup, not any `dma_blocking_wait_poll_doneflag()` address.

## Current run command template

For the batch2 reproducer on the current dummy8x8/sbus64/cfg32 NIC AGFI:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_POST_HIT_MODE=path_trace \
PRT_GDB_PATH_TRACE_TIMEOUT=90 \
PRT_GDB_PATH_TRACE_MAX_STOPS=12 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

This still respects the `gdbserver --once` first-client rule: the helper opens
only the SSH tunnel before `target remote`; it does not probe guest port 2345
with `nc`, telnet, curl, or similar tools.

## What this should answer

The previous run proved entry to `dma_blocking_wait()` for token 546 but then
lost interruptability after a blind 8 second `continue`. This path trace should
identify which of these cases is true:

- the runtime enters `hw_dma_fence()` / blocking wait;
- `hw_dma_fence()` returns and the later card point is shared-scope fencing or
  token cleanup;
- any done-flag poll breakpoint is hit, which is now a regression;
- execution does not reach the first branch after entry, which would point at
  early logging/breadcrumb/completion refresh side effects.
