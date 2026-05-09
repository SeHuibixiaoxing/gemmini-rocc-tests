# 20260509T153102Z - extend no-DMA SPM xlate release GDB ladder

## Summary

Updated the no-DMA SPM xlate GDB helper after the first F2 run proved that
`rr_release(cfg31)` and the post-release `rr_read_csr(CSR_RRCFG31)` readback
both return for the target `segment=1/local_stage=0/subbatch=3/mgr=6/cfg31`
window.

## Change

Modified:

- `pipeline-runtime/scripts/gdb/sbus64_no_dma_segment1_stage0_sxr_release.gdb`

The script now:

- avoids the optimized-out `scope` condition/print at the post-readback line;
- continues past the readback to `prt_spm_xlate_release_scope()` boundaries;
- adds temporary stops at `release-end`, `restore-begin`, `restore-end`, and
  `prt_gemmini_spm_xlate_flush` return.

## Reason

The previous helper returned `gdb_rc=1` after collecting useful evidence because
the source-line stop for `scope->valid = 0` no longer had a printable `scope`
variable in the selected frame. That prematurely ended the GDB session before
testing the next candidate instruction: `rr_restore_opcode_binding(3U,
prev_binding)`.

## Validation

Static validation only:

- inspected the updated GDB command file;
- confirmed the source line boundaries in `pipeline-runtime/src/prt_rerocc.c`;
- recorded the F2 evidence that motivated the change in:
  `pipeline-runtime/debug_records/20260509T153102Z_no_dma_sxr_release_readback_returned.md`.

No new F2 run was started for this helper patch.
