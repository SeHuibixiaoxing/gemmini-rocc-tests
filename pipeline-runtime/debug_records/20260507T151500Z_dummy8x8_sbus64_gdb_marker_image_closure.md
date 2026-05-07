# 2026-05-07T15:15Z dummy8x8/sbus64 gdb marker image closure

## Goal

Prepare the current `pipeline-runtime` GDB marker build for the
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` remote-gdbserver FPGA test.
This checkpoint verifies that the marker-enabled runtime binary and guest
environment are present in the FireMarshal image before launching a run farm.

## Target

- AGFI expected by HWDB: `agfi-077451484fe3b63c3`
- AFI expected by HWDB: `afi-07989ce9ce725a690`
- Workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB config:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

## Command

```bash
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=runtime-ready \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh image-closure
```

## Result

Pass.

FireMarshal stages:

- clean session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-clean-20260507-151315`
- build session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-build-20260507-151320`
- install session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-install-20260507-151430`

Local image freshness passed for:

- workload run entrypoint
- fileonly wrapper
- guest runner script
- `rerocc_pipeline_runtime-linux`
- `rerocc_ptrace_peek-linux`
- `/firemarshal.env`
- guest `gdbserver`

Freshness hashes reported:

- effective guest env sha256:
  `0c8a64ae91bfc1039b3c6b267caddafc6ff7ac24e0d5f1a7e5dcf3e4f410f110`
- runner script sha256:
  `d3718e802140bd1d9d11ef6392ecaba35cad0e2c06e75c020188caf598a37cc7`
- runtime binary sha256:
  `6f59324bde25e49b2ad80afbf6e9d21fee0270699e4adb38cd29a74c612a5622`
- ptrace probe sha256:
  `c9a82c2619e49725439022395eef453f7124c04225bfe3a86b5b79eab7ec5015`

The image freshness dump confirmed the marker env reached `/firemarshal.env`:

```text
export PIPELINE_RUNTIME_GDB_MARKER_ENABLE='1'
export PIPELINE_RUNTIME_GDB_MARKER_SITE='runtime-ready'
```

## Interpretation

The next FPGA run can be treated as a real marker test. If GDB fails to stop at
`prt_gdb_marker_stop`, the likely issue is no longer a missing runtime symbol or
missing guest environment variable. The next suspects would be the run not
using this image, `gdbserver --once` session handling, or the program failing
before `prt_runtime_init()` reaches the `runtime-ready` marker.

## Next Step

Launch the run farm, run `infrasetup`, verify remote image freshness, prepare
networked gdbserver, then attach cross-gdb with:

```gdb
break prt_gdb_marker_stop
continue
print g_prt_gdb_marker_state
thread apply all bt
```
