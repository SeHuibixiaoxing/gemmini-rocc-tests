# 20260505T173746Z - Contract microtests while cfg32 NIC bitstreams build

## Goal

Keep making software-side progress while the two `12p4c128sbus32cfg + current NIC`
F2 bitstream builds continue. This checkpoint verifies the parts of the
pipeline-runtime contract that do not require a new AGFI:

- mapper artifact SPM/tensor bounds at the intended 1024-byte SPM page size;
- 4 CPU core / 12 Gemmini manager / 12 DMA manager init contract;
- `spm_xlate_enable=1` conservative runtime init path;
- fail-fast behavior for wrong SPM page size and wrong DMA manager count.

## Commands

From `/home/ubuntu/chipyard`:

```sh
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all

for method in ours2 gemini2 tangram2; do
  python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
    --pipeline-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.${method}.yaml \
    --hardware-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
    --model-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
    --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 \
    --page-size-bytes 1024

  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
    --hw-validate-only \
    --backend cpu \
    --num-cores 4 \
    --num-gemmini-mgrs 12 \
    --num-dma-mgrs 12 \
    --pair-manager-mode 1 \
    --pages-per-acc 1024 \
    --spm-page-bytes 1024 \
    --spm-xlate-enable 1
done

python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
  --pipeline-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml \
  --hardware-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
  --model-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
  --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 \
  --page-size-bytes 4096

generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --hw-validate-only \
  --backend cpu \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 11 \
  --pair-manager-mode 1 \
  --pages-per-acc 1024 \
  --spm-page-bytes 1024 \
  --spm-xlate-enable 1
```

## Result

Positive checks:

- `ours2`, `gemini2`, and `tangram2` pass artifact audit with
  `--page-size-bytes 1024`.
- `--hw-validate-only` passes for all three methods with:
  `num_cores=4`, `num_gemmini_mgrs=12`, `num_dma_mgrs=12`,
  `pair_manager_mode=1`, `pages_per_acc=1024`, `spm_page_bytes=1024`,
  `spm_xlate_enable=1`.

Negative checks:

- `--page-size-bytes 4096` fails as expected:
  `pages_per_acc=1024 * page_size=4096 != shared_spad_local_size_bytes=1048576`.
- `num_dma_mgrs=11` fails as expected:
  `pair_manager_mode requires num_dma_mgrs (11) to match num_gemmini_mgrs (12)`.

## Interpretation

This does not prove DMA/Gemmini hardware execution. It does prove the current
software tree still catches the two config mistakes most likely to hide the
real FPGA blocker: wrong SPM page granularity and mismatched pair-manager
domains.

The next FPGA-side init log should still be checked for:

```text
cores=4 gemmini=12 dma=12 spm_mgrs=12 pages_per_acc=1024 page_bytes=1024
```

## Concurrent bitstream state

At the time of this checkpoint:

- `pairdummy-cfg32-nic-mainline-20260505T132956Z` is still alive on build host
  `i-0479b74dd4de8e428` / `192.168.0.60`, still in Vivado parallel synthesis.
- `pairdummy-cfg32-nic-notrace-20260505T171453Z` is alive and still in
  GoldenGate; `FireSim-generated.sv` has not appeared yet.
