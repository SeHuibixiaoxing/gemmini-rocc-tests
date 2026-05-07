# 2026-05-07T15:21Z dummy8x8/sbus64 gdb marker infrasetup and remote freshness

## Goal

Verify that the marker-enabled `dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO`
image is installed on the FireSim run host before starting the live
remote-gdbserver marker test.

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host: `i-012e34a444b22df61`
- Run host private IP: `192.168.1.67`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB config:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

## Commands

```bash
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=runtime-ready \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch

PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=runtime-ready \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup

PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=runtime-ready \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh remote-freshness 192.168.1.67
```

## Result

Pass.

Launch:

- tmux session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-launchrunfarm-20260507-151523`
- AWS instance:
  `i-012e34a444b22df61`
- private IP:
  `192.168.1.67`

Infrasetup:

- tmux session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-infrasetup-20260507-151608`
- flashed AGFI:
  `agfi-077451484fe3b63c3`
- FireSim driver readiness preflight:
  pass

Remote freshness:

- local image:
  `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img`
- remote image:
  `/home/ubuntu/sim_slot_0/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver0-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img`
- local sha256:
  `4b9db2dad6c546ded495644aff6f2eecf786f1cbb42eec311aed9877ac9e9ed2`
- remote sha256:
  `4b9db2dad6c546ded495644aff6f2eecf786f1cbb42eec311aed9877ac9e9ed2`

Remote `/firemarshal.env` confirmed:

```text
export PIPELINE_RUNTIME_GDBSERVER_ENABLE='1'
export PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR='0.0.0.0'
export PIPELINE_RUNTIME_GDBSERVER_PORT='2345'
export PIPELINE_RUNTIME_GDB_MARKER_ENABLE='1'
export PIPELINE_RUNTIME_GDB_MARKER_SITE='runtime-ready'
```

## Interpretation

The run host is using the same marker-enabled image as the local freshness
check. A failure to hit `prt_gdb_marker_stop` in the next live test should not
be attributed to stale image deployment.

## Next Step

Start `runworkload`, wait for guest `gdbserver` to announce the endpoint, then
attach cross-gdb as the first TCP client and break on `prt_gdb_marker_stop`.
