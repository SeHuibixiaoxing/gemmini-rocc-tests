# 20260509T142854Z artifact file read log stride

## Change

Reduced default artifact read progress logging in
`pipeline-runtime/src/prt_gemmini_artifacts.c`.

The generic artifact file loader still reads with the existing 8 KiB default
chunk size, but chunk begin/end progress lines are now emitted only:

- at offset 0;
- when crossing the configured stride boundary;
- for the final chunk.

The default stride is 1 MiB and can be overridden with:

```text
PIPELINE_RUNTIME_ARTIFACT_FILE_READ_LOG_STRIDE_BYTES
```

Setting the stride to `0` disables chunk begin/end progress lines.

Chunk begin/end lines also no longer repeat the full artifact path. The file
path remains in the coarse `artifacts file read begin/end` lines.

## Reason

The previous no-DMA F2 bisection reached the 8 KiB artifact read path but then
stopped with a partial sparse-log line inside a long `chunk-end` path string.
That shape points to guest file logging self-interference, not to the 8 KiB
`read()` itself. The fix keeps enough coarse read progress to locate file-read
frontiers without writing two long `O_APPEND` log lines for every 8 KiB chunk.

## Validation

Forced host rebuild:

```sh
make -B -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all \
  PIPELINE_RUNTIME_PROGRESS=1 PIPELINE_RUNTIME_MLOCKALL_MODE=0
```

Result: passed.

Host CPU no-DMA dry-run with mapping cache enabled:

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

Result: exited `0`.

Key log evidence:

```text
artifacts file read begin ... bytes=3908768 chunk_limit=8192 log_stride=1048576
artifacts file read chunk-begin off=0 chunk=8192 total=3908768
artifacts file read chunk-end off=8192 read=8192 total=3908768
artifacts file read chunk-begin off=1048576 chunk=8192 total=3908768
artifacts file read chunk-end off=1056768 read=8192 total=3908768
artifacts file read chunk-begin off=2097152 chunk=8192 total=3908768
artifacts file read chunk-end off=2105344 read=8192 total=3908768
artifacts file read chunk-begin off=3145728 chunk=8192 total=3908768
artifacts file read chunk-end off=3153920 read=8192 total=3908768
artifacts file read chunk-begin off=3907584 chunk=1184 total=3908768
artifacts file read chunk-end off=3908768 read=1184 total=3908768
artifacts mapping cache load end ... entries=16848
runtime end run_rc=0 rc=0
```

The mapping cache file is `3908768` bytes. With 8 KiB chunks this would be
about 478 reads; the default stride emits only 5 begin and 5 end chunk lines.

