# 20260505T185500Z pipeline-runtime debug symbols

## Context

The cfg32 NIC gdbserver path is intended to debug `pipeline-runtime` hangs, not
only prove TCP attach. The current RISC-V target binary was `not stripped` but
had no DWARF sections, which would limit live GDB to function symbols,
registers and disassembly.

## Change

Added default debug flags for `pipeline-runtime` binaries:

- `-g3`
- `-fno-omit-frame-pointer`

The default optimization level stays `-O2` to avoid changing runtime behavior
more than necessary before the first cfg32 NIC GDB attach.

## Files changed

- `pipeline-runtime/Makefile`
- `rerocc-linux-tests/Makefile`

## Validation

Rebuilt the RISC-V target binary:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests \
  -f /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile \
  abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests \
  src_dir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests \
  CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc \
  -B rerocc_pipeline_runtime-linux -j1
```

Result:

- `rerocc_pipeline_runtime-linux`: `with debug_info, not stripped`
- Size changed from about `288K` to about `1.6M`
- `readelf -S` shows `.debug_info`, `.debug_abbrev`, `.debug_line`,
  `.debug_str`, and `.debug_line_str`

Rebuilt the host binary:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all -j1
```

Result:

- host `pipeline_runtime`: `with debug_info, not stripped`
- `pipeline_runtime --help` prints usage successfully

## Limitations

The binary still uses `-O2`. Source lines and types are now available, but some
local variables may be optimized out. If the first live GDB session needs full
local-variable fidelity, a later workload-local debug profile can override
`PIPELINE_RUNTIME_DEBUG_CFLAGS` to include `-Og`; do not change that before the
first cfg32 NIC attach unless the observed failure requires it.
