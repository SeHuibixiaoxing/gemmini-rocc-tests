# 20260509T142854Z - no-DMA artifact log-stride local pass

## Goal

Resume the no-DMA compute bisection locally before spending another F2 run.
The previous F2 no-DMA frontier was still before compute: the runtime stalled
while appending high-frequency artifact read progress lines to the sparse guest
log. This round validates the local fix that throttles those chunk progress
lines.

## Context From Prior No-DMA Records

The no-DMA sequence had already established:

- `20260509T083421Z_no_dma_runtime_run_rcu_stall.md`: no-DMA still stalled
  after entering `runtime_run`, so the issue was not simply a real DMA copy
  never completing.
- `20260509T092614Z_no_dma_guestlog_yaml_frontier.md`: per-line YAML logging
  was too intrusive and not a reliable frontier.
- `20260509T102347Z_no_dma_artifact_pread_frontier.md`: with YAML line logging
  disabled, the next blocker was layer-mapping artifact loading with mapping
  cache disabled.
- `20260509T104719Z_no_dma_mapping_cache_entry2048_stall.md`: enabling the
  mapping cache avoided the 12 MB YAML read, but per-entry `pread` stalled
  around entry 2048; this led to the bulk-read cache loader.
- `20260509T111143Z_no_dma_bulk_read_chunk1m_stall.md`: bulk-read avoided
  per-entry `pread`, but 1 MiB chunks were too large for the guest block path.
- `20260509T113650Z_no_dma_chunk8k_sparse_log_write_stall.md`: 8 KiB chunks
  moved past the 1 MiB read frontier; the last evidence was a partial sparse-log
  line in a long `chunk-end` path string, indicating logging self-interference.

## Local Change Under Test

`pipeline-runtime/src/prt_gemmini_artifacts.c` now keeps 8 KiB read chunks but
adds a default 1 MiB progress stride:

```text
PIPELINE_RUNTIME_ARTIFACT_FILE_READ_LOG_STRIDE_BYTES=1048576
```

The chunk progress line no longer includes the long path string. The path is
still present in coarse file read begin/end logs.

## Validation

Forced host rebuild:

```sh
make -B -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all \
  PIPELINE_RUNTIME_PROGRESS=1 PIPELINE_RUNTIME_MLOCKALL_MODE=0
```

Result: passed.

Host binary SHA256 after rebuild:

```text
ed30b965022a0d6e2b8229a51ac0413bb50d824ce7bc35d5e380dd7be16e969e
```

Host CPU no-DMA dry-run:

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

Key log lines:

```text
artifacts mapping cache read-mode ... mode=bulk-read
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

Summary:

```text
chunk-begin count: 5
chunk-end count: 5
long-path chunk lines: 0
```

## Interpretation

The local no-DMA path now gets through artifact cache loading, artifact
validation, all host-serial no-DMA scheduling, and final runtime shutdown.

This does not prove F2 no-DMA compute yet. It proves that the next F2 no-DMA
run should no longer stop before compute merely because of high-frequency
artifact chunk logging. The next F2 run can be a single confirmation run with:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
PAIRDUMMY_SBUS64_YAML_LINE_LOG_ENABLE=0
PAIRDUMMY_SBUS64_GUEST_LOG_ENABLE=1
PAIRDUMMY_SBUS64_GUEST_DEEP_LOG_ENABLE=0
PAIRDUMMY_SBUS64_PERIODIC_SYNC_ENABLE=1
```

Expected result:

- if F2 advances through `artifacts mapping cache load end`, then no-DMA
  bisection can finally inspect no-DMA compute/control-flow frontiers;
- if it stalls before that line, the remaining blocker is still artifact
  loading or guest file/logging, not Gemmini or DMA completion.

No F2 run was launched for this local validation.

