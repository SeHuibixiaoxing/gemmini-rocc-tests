# 20260509T102347Z no-DMA artifact pread frontier

## Context

Reran the dummy8x8/sbus64/cfg32 NIC noTrace F2 workflow after gating YAML
per-line sparse logging with `PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE=0`.

Environment highlights:

- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
- `PAIRDUMMY_SBUS64_GUEST_LOG_ENABLE=1`
- `PAIRDUMMY_SBUS64_GUEST_DEEP_LOG_ENABLE=0`
- `PAIRDUMMY_SBUS64_YAML_LINE_LOG_ENABLE=0`
- `PAIRDUMMY_SBUS64_PERIODIC_SYNC_ENABLE=1`
- `PAIRDUMMY_SBUS64_PERIODIC_SYNC_SECONDS=5`
- `PAIRDUMMY_SBUS64_RUNNER_STAGE_SYNC_ENABLE=1`

Workflow:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.127
```

Runtime config:

`sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`

HWDB:

`sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

AGFI: `agfi-077451484fe3b63c3`

Freshness values from the pre-run checks:

- local/remote image SHA256:
  `08b67dec373cb9b745d9882fe704a6cca7742984c6cf00229ac026e7beed9982`
- guest env SHA256:
  `7278c51499e8542fb5ba28d20d0a25d9eddd8e3065b81df6a0a6334e6e33b7ce`
- runtime binary SHA256:
  `7d4ae9cdb5c183a5a2468fc4448f0e52a3d8524a8e1982c78ef2b471e0654875`

## Observation

UART reached the runtime:

- `[prt-early] runtime_init done`
- `[prt-early] calling runtime_run`

The run advanced beyond the earlier YAML line 1439 sparse-log frontier. Live
inspection showed coarse YAML checkpoints had completed:

- `yaml_line_log_enable=0`
- `yaml pipeline parse end`
- `yaml pipeline validate end`
- `yaml pipeline load end`

The next observed live frontier was the layer-mapping artifact read path:

```text
[prt-progress] artifacts file pread chunk-begin path=/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml off=1048576 chunk=1048576
```

The host watchdog then saw no file or heartbeat progress:

- first armed at `2026-05-09T10:19:42Z`
- `uart` grew from `20701` to `21191`, then stayed stable
- `guest_sparse` grew from `0` to `53496`, then stayed stable
- `guest_status` stayed `1725`
- `heartbeat.csv` stayed at the header only
- final saved sample showed `idle=188s` and `hb_idle=250s`

No `BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL` marker
was observed.

## Evidence Quality

The saved evidence confirms the stall shape and stable file sizes, but the
debugfs dump of guest files failed because the scripted `debugfs` invocation did
not open the filesystem correctly. The files under
`remote-debugfs-files/*.stderr` all contain:

```text
debugfs 1.47.0 (5-Feb-2023)
cat: Filesystem not open
```

Therefore the `pread chunk-begin` line above is a live observation from the
debugging session, not a line currently recoverable from the saved guest sparse
log artifact. The host-watchdog and UART evidence are file-backed.

## Static Follow-Up

Static inspection found that the layer-mapping cache path already exists:

- host-init generates `${gemmini_layer_mapping_yaml}.cache.bin`
- the current overlay contains the sbus64 cache:
  `rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml.cache.bin`
- cache SHA256:
  `2ec177ee5f1484a368eff59c69378509f80f59b830166f7b9d4ade9a5cc8ef8f`

However the fixed pairdummy profile inherits:

```sh
export PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE="1"
```

and the rendered guest env for this run also had:

```sh
export PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='1'
```

This forces the runtime to fall back to reading and parsing the 12 MB YAML layer
mapping. The next no-DMA bisection should keep DMA disabled but set:

```sh
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
```

then rerun `infrasetup` and `run` through the same workflow. If cache loading
passes, the bisection can move on to the first no-DMA compute frontier. If it
stalls in cache loading, the smaller `.cache.bin` path is the next static target.

## Result

The YAML per-line logging gate worked: no-DMA F2 execution moved past the
previous YAML parse frontier. The current blocker is not yet Gemmini compute or
DMA completion; it is the artifact layer-mapping load path with mapping cache
disabled.

The run farm termination was started after live capture. Follow-up EC2 checks
showed instance `i-0e3edebdabf8e333e` in `shutting-down`; a later check should
confirm it has disappeared from active F2 states.

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T102347Z_no_dma_artifact_pread_stall/`

Key files:

- `remote-sim-slot/uartlog`
- `remote-sim-slot/heartbeat.csv`
- `remote-sim-slot/host-ps.txt`
- `remote-sim-slot/sim-slot-stat.txt`
- `local-logs/2026-05-09--10-12-23-runworkload-LRDEUTGUUBWIAXNI.log`
- `local-logs/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-101222.host-watchdog.log`
- `local-logs/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-101222.pane.log`
- `remote-debugfs-files/*.stderr` documents the failed debugfs dump
