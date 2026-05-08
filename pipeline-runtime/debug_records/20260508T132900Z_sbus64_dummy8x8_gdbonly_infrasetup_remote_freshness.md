# 2026-05-08 13:29Z - sbus64 dummy8x8 GDB-only infrasetup and remote freshness

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`
- Instance: `i-0c1e160390ced75bc`, private IP `192.168.1.212`

## Commands

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh remote-freshness 192.168.1.212
```

## Result

`launchrunfarm` passed and booted instance `i-0c1e160390ced75bc`.

`infrasetup` passed:

- target generated DTS audit found both blkdev and NIC
- FPGA slot 0 flashed with `agfi-077451484fe3b63c3`
- FireSim driver readiness preflight passed
- full log: `sims/firesim/deploy/logs/2026-05-08--13-24-43-infrasetup-QPFZP67DGQGLBEOR.log`

`remote-freshness` passed:

- local image: `software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img`
- remote image: `/home/ubuntu/sim_slot_0/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver0-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img`
- local sha256: `eb0fc78ccc55e303c8d10cb9b74c9858cf3a5f396e74dd067a268f1542ba4692`
- remote sha256: `eb0fc78ccc55e303c8d10cb9b74c9858cf3a5f396e74dd067a268f1542ba4692`

Remote `/firemarshal.env` confirms the next run is GDB-only:

```sh
export PIPELINE_RUNTIME_PROFILE_ID='pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly'
export PIPELINE_RUNTIME_GUEST_LOG_ENABLE='0'
export PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'
export PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'
export PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'
export PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='uart'
export PIPELINE_RUNTIME_BREADCRUMB_ENABLE='0'
export PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE='1'
export PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE='0'
export PIPELINE_RUNTIME_GDBSERVER_ENABLE='1'
export PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR='0.0.0.0'
export PIPELINE_RUNTIME_GDBSERVER_PORT='2345'
export PIPELINE_RUNTIME_GDB_MARKER_ENABLE='0'
```

## Next Step

Start `runworkload`, let `gdbserver --once` listen, connect GDB as the first TCP client, and move the pipeline-runtime frontier using live breakpoints and GDB state inspection only.
