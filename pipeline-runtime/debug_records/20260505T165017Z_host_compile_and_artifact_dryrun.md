# Host compile and artifact dry-run checkpoint

Time: `2026-05-05 16:50 UTC`

## Context

The `12p4c128sbus32cfg + optimized DMA + current NIC` F2 build is still running in
tmux session `pairdummy-cfg32-nic-mainline-20260505T132956Z`. While waiting for
the bitstream, I ran software-only checks on the current `pipeline-runtime` state
so we do not carry an unbuildable runtime into the next FireSim image.

## Initial failure

Command:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -B all
```

Initial result: fail.

Observed failures:

- `src/prt_dma.c`: C11 host compile rejected bare `asm volatile(...)`.
- `src/prt_dma.c`: many RISC-V/debug-only static helpers were unused in the
  default host build and tripped `-Werror=unused-*`.
- `src/prt_gemmini_artifacts.c`, `src/prt_page_table.c`, `src/prt_rerocc.c`,
  `src/prt_runtime.c`, `src/prt_schedule_action.c`, and `src/prt_scheduler.c`
  had smaller unused-helper or log-disabled local-variable warnings under the
  default `PIPELINE_RUNTIME_PROGRESS=0` build.

## Fix

Narrow buildability fixes:

- Converted pipeline-runtime inline assembly spelling from `asm volatile` to
  `__asm__ volatile` where needed for strict C11 builds.
- Suppressed only platform/debug-only unused helper warnings in `prt_dma.c`.
- Marked RISC-V-only or optional debug helper functions as unused where they
  are intentionally inactive in host builds.
- Added `(void)` uses for variables that are only consumed by compiled-out log
  macros.

These changes do not alter the active bitstream build input; they only repair
the local/guest runtime software build.

## Verification

Host compile:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -B all
```

Result: pass, produced `pipeline-runtime/pipeline_runtime`.

Artifact audit:

```bash
AUD=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py
DIR=generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
for method in ours2 gemini2 tangram2; do
  python "$AUD" \
    --pipeline-yaml "$DIR/pipeline_mapping.${TARGET}.${method}.yaml" \
    --hardware-yaml "$DIR/hardware_target.${TARGET}.yaml" \
    --model-yaml "$DIR/model.layers.yaml" \
    --expect-target-key "$TARGET" \
    --page-size-bytes 1024
done
```

Result: pass for `ours2`, `gemini2`, and `tangram2`.

CPU dry-run:

```bash
BIN=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime
DIR=generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
for method in ours2 gemini2 tangram2; do
  timeout 180 "$BIN" \
    --backend cpu \
    --model-yaml "$DIR/model.layers.yaml" \
    --layer-mapping-yaml "$DIR/gemmini_layer_mapping.${TARGET}.yaml" \
    --skip-model-bin-load \
    --pipeline-yaml "$DIR/pipeline_mapping.${TARGET}.${method}.yaml" \
    --skip-input-load \
    --skip-golden-check \
    --num-cores 4 \
    --num-gemmini-mgrs 12 \
    --num-dma-mgrs 12 \
    --gemmini-base-id 0 \
    --dma-base-id 0 \
    --pair-manager-mode 1 \
    --pages-per-acc 1024 \
    --spm-page-bytes 1024 \
    --watchdog-ms 300000 \
    --batch 1 \
    --trace "/tmp/pipeline-runtime-host-traces/${method}.cpu.dry.trace"
done
```

Result: pass for `ours2`, `gemini2`, and `tangram2`.

## Residual limits

- This is not an FPGA validation and does not prove DMA/Gemmini custom
  instruction behavior.
- The dry-run uses `--skip-model-bin-load`, `--skip-input-load`, and
  `--skip-golden-check`; it validates artifact parsing, manager/SPM contract
  setup, and CPU backend control flow, not numerical correctness.
- The active cfg32 NIC bitstream has no AGFI yet at this checkpoint.
