# 20260505T172458Z host static dry-run after cfg32 docs

## Goal

Re-run a narrow software-only checkpoint after documenting the parallel cfg32 NIC bitstream builds. This
does not validate FPGA, DMA custom instructions, Gemmini custom instructions, NIC, or `gdbserver`; it only
checks that the current software tree still builds and that the intended cfg32 pair-manager artifact contract
is internally consistent while the bitstreams continue building.

## Active bitstream context

- Mainline cfg32 NIC build: `pairdummy-cfg32-nic-mainline-20260505T132956Z`.
- No-TraceIO cfg32 NIC build: `pairdummy-cfg32-nic-notrace-20260505T171453Z`.
- Both builds had no AGFI/AFI at the start of this host-side checkpoint.

## Commands

Host rebuild:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -B all
```

Artifact audit for `ours2`, `gemini2`, and `tangram2`:

```bash
AUD=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py
ART_DIR=generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
for method in ours2 gemini2 tangram2; do
  python3 "$AUD" \
    --pipeline-yaml "$ART_DIR/pipeline_mapping.${TARGET}.${method}.yaml" \
    --hardware-yaml "$ART_DIR/hardware_target.${TARGET}.yaml" \
    --model-yaml "$ART_DIR/model.layers.yaml" \
    --expect-target-key "$TARGET" \
    --page-size-bytes 1024
done
```

Batch8 CPU dry-run for `ours2`:

```bash
BIN=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime
TRACE_DIR=/tmp/pipeline-runtime-host-traces-20260505T1720
timeout 240 "$BIN" \
  --backend cpu \
  --model-yaml "$ART_DIR/model.layers.yaml" \
  --layer-mapping-yaml "$ART_DIR/gemmini_layer_mapping.${TARGET}.yaml" \
  --skip-model-bin-load \
  --pipeline-yaml "$ART_DIR/pipeline_mapping.${TARGET}.ours2.yaml" \
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
  --batch 8 \
  --trace "$TRACE_DIR/ours2.batch8.cpu.dry.trace"
```

Pair-manager init checks:

```bash
"$BIN" --hw-validate-only --backend cpu --num-cores 4 --num-gemmini-mgrs 12 --num-dma-mgrs 12 \
  --pair-manager-mode 1 --pages-per-acc 1024 --spm-page-bytes 1024 --spm-xlate-enable 1 \
  --watchdog-ms 300000

"$BIN" --hw-validate-only --backend cpu --num-cores 4 --num-gemmini-mgrs 12 --num-dma-mgrs 12 \
  --pair-manager-mode 1 --pages-per-acc 1024 --spm-page-bytes 1024 --spm-xlate-enable 0 \
  --watchdog-ms 300000

"$BIN" --hw-validate-only --backend cpu --num-cores 4 --num-gemmini-mgrs 12 --num-dma-mgrs 11 \
  --pair-manager-mode 1 --pages-per-acc 1024 --spm-page-bytes 1024 --spm-xlate-enable 1 \
  --watchdog-ms 300000
```

## Results

- Host rebuild passed and produced `pipeline-runtime/pipeline_runtime`.
- Artifact audit passed for `ours2`, `gemini2`, and `tangram2`.
- `ours2` batch8 CPU dry-run exited 0 within the 240 second timeout.
- `--hw-validate-only` passed for the intended `4 core / 12 Gemmini / 12 DMA` pair-manager layout with
  `spm_xlate_enable=1`.
- `--hw-validate-only` passed for the same layout with `spm_xlate_enable=0`.
- The negative `num_dma_mgrs=11` pair-manager layout failed as expected:

```text
runtime_init: pair_manager_mode requires num_dma_mgrs (11) to match num_gemmini_mgrs (12)
runtime_init failed: invalid (-2)
NEGATIVE_TEST_EXPECTED_FAIL
```

Final marker:

```text
HOST_STATIC_AND_DRYRUN_PASS trace_dir=/tmp/pipeline-runtime-host-traces-20260505T1720
```

## Interpretation

This checkpoint preserves confidence that the current software side still accepts the intended
`12p4c128sbus32cfg` runtime contract and rejects a mismatched DMA/Gemmini pair-manager configuration.
It also keeps the mapper artifacts under the 1024-byte SPM page interpretation. It does not prove any FPGA
behavior; the next decisive checkpoint remains the cfg32 NIC bitstream result and remote `gdbserver` run.
