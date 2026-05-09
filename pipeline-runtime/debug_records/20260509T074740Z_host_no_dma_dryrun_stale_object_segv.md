# 20260509T074740Z host no-DMA dry-run stale-object segv

## Context

While validating the new no-DMA compute mode locally, the first host CPU dry-run
segfaulted in `runtime_release_topology()` before any F2 run was attempted.

## Observation

The host `pipeline-runtime/Makefile` did not track headers as object
dependencies. After adding a field to `prt_runtime_cfg_t`, an incremental host
build left some objects compiled with the old `prt_runtime_t` layout. GDB showed
`prt_runtime_current_action()` returning an invalid `action=0x1` at
`runtime_release_topology()` because an old object read the wrong offset for
`rt->active_action`.

## Action

- Reworked the change record implementation to include a host Makefile header
  dependency fix.
- Ran `make clean` before the host rebuild.

## Result

After the clean rebuild, the same `dummy8x8/sbus64/ours2` host CPU dry-run with
`--no-dma-compute`, skipped model/input/golden, batch 8, pair-manager mode, and
`--spm-page-bytes 1024 --pages-per-acc 1024` exited 0.

No F2 instance was launched in this debugging round.
