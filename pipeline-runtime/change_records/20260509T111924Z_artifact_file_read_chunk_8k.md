# 20260509T111924Z artifact file read chunk 8K

## Problem

The F2 no-DMA validation of the mapping-cache bulk-read loader avoided the
previous per-entry `pread` frontier, but then stalled in the generic artifact
file loader:

```text
artifacts file read chunk-end ... off=1048576 read=1048576
artifacts file read chunk-begin ... off=1048576 chunk=1048576
```

The guest runtime was still before no-DMA compute. The block device reported a
small request limit during boot, so a 1 MiB userspace read is a poor bisection
point for this target.

## Change

Updated `src/prt_gemmini_artifacts.c`:

- added `PIPELINE_RUNTIME_ARTIFACT_FILE_READ_CHUNK_BYTES`
- changed the default generic artifact read chunk from 1 MiB to 8 KiB
- capped the override at 1 MiB and treated `0` as the default
- added `chunk_limit=<bytes>` to the `artifacts file read begin` progress line

This keeps the loader sequential but makes the guest block-image request size
explicit and easy to bisect.

## Validation

Host build passed:

```sh
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all \
  PIPELINE_RUNTIME_PROGRESS=1 PIPELINE_RUNTIME_MLOCKALL_MODE=0
```

Host no-DMA CPU dry-run passed with mapping cache enabled:

```sh
PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=0 \
PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE=0 \
timeout 120 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --backend cpu \
  --model-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
  --layer-mapping-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml \
  --pipeline-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.ours2.yaml \
  --batch 8 --num-cores 4 --num-gemmini-mgrs 12 --num-dma-mgrs 12 \
  --pages-per-acc 1024 --spm-page-bytes 1024 \
  --gemmini-base-id 0 --dma-base-id 0 --pair-manager-mode 1 \
  --watchdog-ms 300000 --no-dma-compute \
  --skip-model-bin-load --skip-input-load --skip-golden-check
```

Key log lines:

```text
artifacts file read begin ... bytes=3908768 chunk_limit=8192
artifacts file read chunk-end ... off=3908768 read=1184
artifacts mapping cache load end ... entries=16848
runtime end run_rc=0 rc=0
```

F2 validation is pending. The next F2 run should rebuild/reinstall the
FireMarshal image and rerun `infrasetup` before `runworkload`.

## Artifacts

Local dry-run log:

`pipeline-runtime/debug_records/artifacts/20260509T111924Z_artifact_read_chunk_8k_local/local-host-no-dma-small-chunk.log`
