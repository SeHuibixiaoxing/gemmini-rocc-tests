# 20260509T111143Z no-DMA bulk-read chunk1m stall

## Context

Reran the dummy8x8/sbus64/cfg32 NIC noTrace F2 workflow after the mapping cache
loader was changed from per-entry `pread` to sequential bulk read.

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

- instance: `i-007db2f231b4d9942`
- private IP: `192.168.1.43`

Freshness before run:

- effective guest env SHA256:
  `9da9f9c4a85f122efca984d1cdeb229a3cb570ec8912ba4bdcbdbc29d80829c7`
- local/remote image SHA256 before boot:
  `1cf7584c4f73ce179af506f633d104991772fddcbf9cdec1d801f48039ad427e`
- runtime binary SHA256 in local image:
  `0783d9bef041f7cb55dd231dee68a66abc236dad5f58a8068b1764129c1cd836`

## Observation

The rebuilt image entered `runtime_run` and confirmed the new cache read mode:

```text
[prt-progress] artifacts mapping cache read-mode ... mode=bulk-read
```

It advanced past the previous entry-by-entry cache frontier. The last stable
sparse-log lines were:

```text
[prt-progress] artifacts file read chunk-begin ... off=0 chunk=1048576
[prt-progress] artifacts file read chunk-end ... off=1048576 read=1048576
[prt-progress] artifacts file read chunk-begin ... off=1048576 chunk=1048576
```

After that point, the observable state stayed stable:

- `bertmini-batch8.log` size `53433`
- `uartlog` size `21192`
- `bertmini-batch8.status` size `1725`
- `bertmini-batch8.runner-proc.stage` size `9342`
- heartbeat remained header-only
- host watchdog reached at least `idle=156s`, `hb_idle=219s`
- `rerocc_pipeline` was still present and reported as `R (running)`

No `BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL` marker
was observed.

## Interpretation

The bulk-read fix was effective for the old `entries=2048` per-entry `pread`
frontier, but the next no-DMA frontier is still before compute. The runtime is
now blocked inside the generic artifact file loader while reading the second
1 MiB chunk of the 3.9 MB mapping cache file from the guest block image.

This points away from mapping-cache entry decode and toward the block-image
read path or read request sizing. The next static action should reduce the
artifact read chunk size and make it configurable, with progress logged at the
smaller chunk boundary.

## Run-Farm Cleanup

The run farm was terminated with:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` exited 0. A follow-up EC2 query found no F2 instances in
`pending`, `running`, `stopping`, or `stopped`; the target instance was only
observed in the AWS transitional `shutting-down` state.

## Next Step

Implement a targeted static change in `src/prt_gemmini_artifacts.c`:

- replace the hard-coded 1 MiB artifact read chunk with a smaller default
  suitable for the guest block path, likely 8 KiB or 64 KiB
- optionally allow an environment override for future bisection
- keep the `chunk-begin`/`chunk-end` telemetry so the next F2 run identifies
  whether the stall is total bytes read, per-request size, or a later decode
  stage

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T111143Z_no_dma_bulk_read_chunk1m_stall/`

Key files:

- `remote-debugfs-files/bertmini-batch8.log`
- `remote-debugfs-files/bertmini-batch8.status`
- `remote-debugfs-files/bertmini-batch8.runner-proc.stage`
- `remote-sim-slot/uartlog`
- `remote-sim-slot/heartbeat.csv`
- `remote-sim-slot/ps.txt`
- `remote-sim-slot/fpga-describe.txt`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-110143.host-watchdog.log`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-110143.pane.log`
- `2026-05-09--11-01-44-runworkload-DFGU9WY29BCWQZOW.log`
- `f2_active_after_terminate.json`
