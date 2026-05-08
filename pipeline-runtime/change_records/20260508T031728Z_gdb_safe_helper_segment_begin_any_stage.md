# 2026-05-08 03:17 UTC: make segment-begin GDB marker filter stage-agnostic

## Context

The safe gdbserver helper fix that waits for `prt_gdb_marker_init_from_env()`
worked: the live F2 run hit the temporary breakpoint, finished runtime marker
env initialization, then programmed `g_prt_gdb_marker_filter`.

However, the helper still did not stop at `segment-begin-env` while guest logs
showed the runtime had already entered segment 0 worker execution. Static review
showed the segment-begin marker has no local-stage identity; it is emitted with
stage fields set to `PRT_DEBUG_U32_NONE` / marker "any" semantics. The helper
was filtering `local_stage_id=0`, so the marker could not match.

## Change

- Updated `scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh`
  so the first `segment-begin` filter uses `local_stage_id=ANY` instead of `0`.
- Later worker/DMA filters remain stage-specific where those markers carry
  stage identity.

## Expected effect

The next run should hit `segment-begin-env` after runtime marker env init, then
advance to the stage-specific worker and DMA markers.

## Verification planned

- `bash -n` on the helper.
- Re-run the gdbserver helper on a fresh workload; the current live run is not
  valid marker-chain evidence because it used the stale local-stage filter.
