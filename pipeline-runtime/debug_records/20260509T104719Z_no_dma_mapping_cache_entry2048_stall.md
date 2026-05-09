# 20260509T104719Z no-DMA mapping-cache entry2048 stall

## Context

Reran the dummy8x8/sbus64/cfg32 NIC noTrace F2 workflow with mapping cache
enabled to bypass the previous large YAML layer-mapping read frontier.

Environment highlights:

- `PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0`
- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
- `PAIRDUMMY_SBUS64_GUEST_LOG_ENABLE=1`
- `PAIRDUMMY_SBUS64_GUEST_DEEP_LOG_ENABLE=0`
- `PAIRDUMMY_SBUS64_YAML_LINE_LOG_ENABLE=0`
- `PAIRDUMMY_SBUS64_PERIODIC_SYNC_ENABLE=1`
- `PAIRDUMMY_SBUS64_PERIODIC_SYNC_SECONDS=5`
- `PAIRDUMMY_SBUS64_RUNNER_STAGE_SYNC_ENABLE=1`

Runtime config:

`sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`

HWDB:

`sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

AGFI: `agfi-077451484fe3b63c3`

Run host:

- instance: `i-0cae2541a905134e2`
- private IP: `192.168.1.164`

Freshness:

- effective guest env SHA256:
  `9da9f9c4a85f122efca984d1cdeb229a3cb570ec8912ba4bdcbdbc29d80829c7`
- local/remote image SHA256:
  `228f426de3bf871d31cf904cc99c33c90e2c4265b4ca07ae52a2fd7657426411`
- runtime binary SHA256:
  `7d4ae9cdb5c183a5a2468fc4448f0e52a3d8524a8e1982c78ef2b471e0654875`
- mapping cache SHA256:
  `2ec177ee5f1484a368eff59c69378509f80f59b830166f7b9d4ade9a5cc8ef8f`

## Observation

The run reached `runtime_run` and passed the earlier large-YAML read frontier.
The saved sparse log confirms the runtime selected the `.cache.bin` path:

```text
[prt-progress] artifacts mapping cache load begin cache=/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml.cache.bin
[prt-progress] artifacts mapping cache read-mode cache=/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml.cache.bin mode=pread
[prt-progress] artifacts mapping cache progress cache=/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml.cache.bin entries=2048 elapsed_ms=57
```

After that point, the observable state stayed stable:

- `bertmini-batch8.log` size `51123`
- `uartlog` size `21191`
- `bertmini-batch8.status` size `1725`
- `bertmini-batch8.runner-proc.stage` size `9340`
- heartbeat remained header-only
- host watchdog reached `idle=188s`, `hb_idle=250s`

The runner process was still present and reported as `R (running)`. No
`BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL` marker was
observed.

## Interpretation

Enabling mapping cache correctly avoided the 12 MB YAML layer-mapping read.
The new no-DMA frontier is the cache loader itself, specifically after 2048
small per-entry `pread` calls. The code path reads the cache header, then issues
one `pread` per `mapping_cache_entry_t`; entry 2048 starts around byte offset
`475168`, well before the end of the 3.9 MB cache file.

This is still before real Gemmini/no-DMA compute. The most likely fix is to
avoid thousands of small `pread` calls on the guest block device by reading the
cache file sequentially in bulk and decoding entries from memory.

## Action

Implemented a targeted static fix in `pipeline-runtime/src/prt_gemmini_artifacts.c`:

- changed the generic artifact file loader from offset `pread` chunks to
  sequential `read` chunks after seeking back to the start
- changed `load_mapping_cache_file()` from per-entry `pread` to bulk-read of the
  whole `.cache.bin`, followed by in-memory header validation and entry decode
- changed the cache read-mode progress label from `mode=pread` to
  `mode=bulk-read`

## Validation

Host build:

```sh
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all \
  PIPELINE_RUNTIME_PROGRESS=1 PIPELINE_RUNTIME_MLOCKALL_MODE=0
```

Result: passed.

Host no-DMA CPU dry-run:

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

Result: exited 0. The log shows `mode=bulk-read`, cache progress through all
`16848` entries, `artifacts mapping cache load end`, and
`runtime end run_rc=0 rc=0`.

## Run-Farm Cleanup

The run farm was terminated with:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` exited 0. The first follow-up EC2 query showed
`i-0cae2541a905134e2` in `shutting-down`; a later check should confirm no
active F2 instances remain.

## Next Step

Rebuild the FireMarshal image with the bulk-read runtime binary, rerun
`infrasetup`, and repeat the same no-DMA F2 workflow with
`PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0`.

Expected next signal:

- if fixed, sparse log should show `artifacts mapping cache load end` and then
  advance into no-DMA host/compute scheduling
- if it stalls again, the last `mode=bulk-read` file-read chunk will identify
  whether the remaining issue is generic block image sequential read or later
  in-memory mapping validation

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T104719Z_no_dma_mapping_cache_entry2048_stall/`

Key files:

- `remote-debugfs-files/bertmini-batch8.log`
- `remote-debugfs-files/bertmini-batch8.status`
- `remote-debugfs-files/bertmini-batch8.runner-proc.stage`
- `remote-sim-slot/uartlog`
- `remote-sim-slot/heartbeat.csv`
- `remote-sim-slot/sim-slot-and-host-state.txt`
- `local-logs/2026-05-09--10-35-05-runworkload-NMUPEMTDWOFBPGKB.log`
- `local-logs/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-103504.host-watchdog.log`
- `local-host-no-dma-cache-bulk-read.log`
