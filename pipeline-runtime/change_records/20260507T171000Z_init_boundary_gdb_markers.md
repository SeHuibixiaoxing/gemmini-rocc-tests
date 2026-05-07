# 20260507T171000Z init-boundary GDB markers

## Context

The `20260507T164900Z` token548 marker run did not reach DMA evidence. GDB
attached and set `prt_gdb_marker_stop`, but after `continue` no stop reply or
Ctrl-C stack could be recovered. The visible sparse log frontier was still in
artifact mapping parse, while previous stack sampling showed the same early
window can progress into synthetic model prefault before GDB control becomes
unreliable.

## Change

Added source marker sites that split runtime initialization before worker/DMA
execution:

- `artifact-mapping-parse-done`
- `artifact-validate-done`
- `synthetic-model-prefault-begin`
- `synthetic-model-prefault-end`
- `synthetic-model-ready`

The sites use the existing `prt_gdb_marker_note()` / `prt_gdb_marker_stop()`
path. They are inert unless `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1` and the site
filter matches.

## Verification

Built the staged RISC-V Linux runtime ELF:

```bash
CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc \
make -C generators/gemmini/software/gemmini-rocc-tests/build rerocc-linux-tests -j1
```

Result: build passed.

New staged ELF:

```text
generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
sha256=cbd843406a6875d7cceb0e995747bf7f55ea5a9e79f9292b07db043d7b0df2f5
```

Symbols confirmed:

```text
prt_gdb_marker_stop
prt_gdb_marker_note
g_prt_gdb_marker_state
```

## Next use

Run the next gdbserver probe with one early marker at a time. Start with
`artifact-mapping-parse-done`, then `artifact-validate-done`,
`synthetic-model-prefault-begin`, `synthetic-model-prefault-end`,
`synthetic-model-ready`, and finally `runtime-ready`.
