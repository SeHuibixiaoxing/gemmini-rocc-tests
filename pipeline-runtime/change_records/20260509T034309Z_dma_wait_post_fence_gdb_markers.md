# 20260509T034309Z - DMA wait post-fence GDB markers

## Context

The 2026-05-09 sbus64 F2 live GDB run showed that observed cfg0 and cfg2
`prt_rr_fence_scope()` instances returned through both `rr_swap_csr()` and the
ordinary RISC-V `fence`, but the run later stopped responding cleanly to Ctrl-C
and disconnected from `gdbserver`.

The existing DMA GDB marker sites only covered `dma-wait-enter` and
`dma-wait-return`, which was too coarse to distinguish a later shared-fence
hang from post-wait cleanup or pipebuf publication.

## Change

Added low-output GDB marker sites in the DMA wait post-fence path:

- `dma-wait-after-fence`
- `dma-wait-before-shared-fence`
- `dma-wait-after-shared-fence`
- `dma-wait-after-release`
- `dma-wait-after-complete`
- `dma-wait-after-trace-complete`

These reuse `prt_gdb_marker_note()` and therefore only stop when selected by
`PIPELINE_RUNTIME_GDB_MARKER_*`. They do not add guest file logging, UART
logging, breadcrumb writes, or doneflag polling.

## Verification

Built the RISC-V Linux target:

```sh
source env.sh >/dev/null 2>&1
make -C generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests \
  abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests \
  CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc \
  rerocc_pipeline_runtime-linux
```

Result: compile and link passed.

## Constraints

- DMA completion must stay on the wait/fence path.
- Doneflag polling is known-bad and must not be reintroduced as completion
  logic or pass/fail evidence.
- The next F2 run should use these marker sites through interactive GDB so that
  guest logs do not move the hang.
