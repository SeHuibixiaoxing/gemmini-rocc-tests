# 2026-05-08 08:18Z DMA GDB marker context fix

## Motivation

The next narrow GDB run needs to stop at the export DMA path for:

- segment `1`
- global stage `1`
- local stage `0`
- subbatch `7`
- manager `0`
- tensor `3`
- page around `15`

The existing `dma-export-page-submit-*`, `dma-wait-enter`, and
`dma-wait-return` markers carried local stage / manager / tensor / page / token,
but wrote segment, global stage, and subbatch as `PRT_DEBUG_U32_NONE`.
Therefore a guest env that filtered DMA markers by segment or subbatch would
never match, while omitting those fields could hit an earlier same tensor/page
context.

## Change

- Updated `pipeline-runtime/src/prt_dma.c` so DMA GDB markers include the current
  thread-local runtime debug context:
  - `g_prt_debug_tls_state.segment_idx`
  - `g_prt_debug_tls_state.global_stage_id`
  - `g_prt_debug_tls_state.subbatch_id`
- Kept the explicit local stage from the DMA call path as the marker local stage.
- Reused the same context-carrying path for:
  - export page submit begin/end
  - DMA submitwait cleanup
  - DMA wait enter/return
  - fixed-load DMA page markers
- Updated `docs/testing/gdbserver_integration_sop.md` to document the old-binary
  caveat and the new-binary requirement.
- Updated `run_pairdummy_cfg32_gdbserver_export_page_frontier.sh` usage defaults
  to the current tensor3/page15 frontier.

## Non-change

- No DMA control-flow change.
- No manager allocation-policy change.
- No change to `hw_dma_fence()`, RR acquire/fence/release, or token cleanup.
- No doneflag completion path was added or enabled. `doneflag` remains invalid
  as completion evidence.

## Local verification

Static check:

```sh
git diff --check
```

Local RISC-V Linux rebuild:

```sh
source /home/ubuntu/chipyard/env.sh
source /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_fixed_env.sh
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests \
  -f /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile \
  abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests \
  src_dir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests \
  XLEN=64 \
  CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc \
  PIPELINE_RUNTIME_PROGRESS=1 \
  PIPELINE_RUNTIME_PROGRESS_RAW=0 \
  PIPELINE_RUNTIME_PROGRESS_HOT=0 \
  PIPELINE_RUNTIME_GEMMINI_PHASE=0 \
  PIPELINE_RUNTIME_ONLY_MARKER=0 \
  PIPELINE_RUNTIME_CRITICAL_UART_PROBE=0 \
  PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST=0 \
  PIPELINE_RUNTIME_PROGRESS_PAD_BURST=0 \
  PIPELINE_RUNTIME_MLOCKALL_MODE=2 \
  rerocc_pipeline_runtime-linux rerocc_ptrace_peek-linux
```

Built ELF:

```text
build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
sha256=4fe6439b04996c1a5b0d064d048c1525b0bfda560df969255222c67b31402eeb
```

## Next test

Rebuild/install or patch the FireMarshal image, run local and remote freshness,
then start the same AGFI `agfi-077451484fe3b63c3` with:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-export-page-submit-begin
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=7
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=3
PIPELINE_RUNTIME_GDB_MARKER_PAGE=15
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0
```

Then attach with `run_pairdummy_cfg32_gdbserver_export_page_frontier.sh` as the
first TCP client to `gdbserver --once`.
