Debug category: pipeline-runtime / HybridMapper / dummy8x8 sbus64 / gdbserver prep

# 20260507T080000Z - dummy8x8 sbus64 mapping alignment

## Goal

Prepare pipeline-runtime for the current F2 debug AGFI:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- FireSim target:
  `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`

The previous pairdummy gdbserver workflow selected the dummy8x8/sbus64 AGFI but
inherited the old `pairdummy_sbus128_fixed_env.sh`, so guest `TARGET_KEY`
remained `dummy16x16 / mac256 / sbus128`. That would make any pipeline-runtime
hang classification suspect.

## Changes

- Added HybridMapper hardware target
  `rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64`.
- Added `pairdummy_sbus64_dummy8x8_fixed_env.sh`.
- Changed `pairdummy_sbus128_gdbserver_workflow.sh` to source
  `PAIRDUMMY_FIXED_ENV_SCRIPT` when set.
- Changed `pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
  to pin `PAIRDUMMY_FIXED_ENV_SCRIPT` to the dummy8x8/sbus64 fixed env.

## Artifact Generation

```sh
./.conda-env/bin/python conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py \
  --model bertmini \
  --target-keys rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64 \
  --methods ours2,gemini2,tangram2 \
  --mode fresh
```

Generated files are under:

```text
conference/HybridMapper/output/pipeline_runtime/bertmini/
```

These artifacts are ignored/generated collateral; they must be regenerated or
copied into the FireMarshal image before F2 testing.

## Verification

Static artifact audit passed for `ours2`, `gemini2`, and `tangram2`:

```text
target=rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64
page_size_bytes=1024
ours2: segments=15
gemini2: segments=19
tangram2: segments=19
```

Workflow show now reports:

```text
profile_id=pairdummy-sbus64-dummy8x8-fixed-v1
target_key=rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64
methods=ours2
num_cores=4
num_gemmini=12
num_dma=12
pair_manager_mode=1
gdbserver_enable=1
```

CPU backend dry-run passed for `ours2`, `gemini2`, and `tangram2` with:

```text
--backend cpu
--skip-model-bin-load
--skip-input-load
--skip-golden-check
--num-cores 4
--num-gemmini-mgrs 12
--num-dma-mgrs 12
--pair-manager-mode 1
--pages-per-acc 1024
--spm-page-bytes 1024
--batch 1
```

## Next Step

Rebuild/install the FireMarshal workload image so `host-init.sh` copies the new
target artifacts into the guest, then run the dummy8x8/sbus64 gdbserver F2
workflow and use remote GDB to classify the first real pipeline-runtime hang.
