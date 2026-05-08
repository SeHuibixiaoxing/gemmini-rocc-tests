# 2026-05-08 06:27 UTC: dummy8x8 sbus64 gdbserver image freshness repair

## Context

Target run:

- AGFI/AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Config: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Marker:
  `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`,
  `PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin`

## Failure

`infrasetup` was stopped by the image freshness guard before driver deployment.
The image still contained an older file-only wrapper:

```text
[image-freshness] FAIL workload-run-entrypoint src_sha=f2c1d7f4d2ca280d7b7ab01af662ecbd335682443791e6e53dc8ee1436e19fe2 img_sha=ab9fe062aab64432727797750e4281e2ad5a4d614b0b432cb1f176e2270ca659
[image-freshness] FAIL fileonly-wrapper src_sha=f2c1d7f4d2ca280d7b7ab01af662ecbd335682443791e6e53dc8ee1436e19fe2 img_sha=ab9fe062aab64432727797750e4281e2ad5a4d614b0b432cb1f176e2270ca659
```

The runner script, runtime binary, ptrace probe, `/firemarshal.env`, and
gdbserver presence checks were already OK.

## Repair

No `marshal clean` was run because repository policy requires explicit user
confirmation before cleaning directories.

Instead, the current source wrapper was written directly into the existing
local FireMarshal ext4 image at both freshness-checked guest paths:

- `/firemarshal.sh`
- `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`

Both were set back to mode `0755`.

## Verification

Command:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin \
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh local-freshness
```

Result:

```text
[image-freshness] OK workload-run-entrypoint sha256=f2c1d7f4d2ca280d7b7ab01af662ecbd335682443791e6e53dc8ee1436e19fe2
[image-freshness] OK fileonly-wrapper sha256=f2c1d7f4d2ca280d7b7ab01af662ecbd335682443791e6e53dc8ee1436e19fe2
[image-freshness] OK runner-script sha256=d3718e802140bd1d9d11ef6392ecaba35cad0e2c06e75c020188caf598a37cc7
[image-freshness] OK runtime-binary sha256=62aa6489325dcec067e94ad04401f2b26f544afc73c1bb57bb1aabdc74414f42
[image-freshness] OK ptrace-probe sha256=c9a82c2619e49725439022395eef453f7124c04225bfe3a86b5b79eab7ec5015
[image-freshness] OK firemarshal-env sha256=62cade1834c02aa9fedf8983e0bace1ebf708a3feabff2c85b48625ea7775ec9
[image-freshness] OK gdbserver present in image
[image-freshness] PASS workload=rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver
```

The effective `/firemarshal.env` includes:

```text
export PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE='0'
export PIPELINE_RUNTIME_GDBSERVER_ENABLE='1'
export PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR='0.0.0.0'
export PIPELINE_RUNTIME_GDBSERVER_PORT='2345'
export PIPELINE_RUNTIME_GDB_MARKER_ENABLE='1'
export PIPELINE_RUNTIME_GDB_MARKER_SITE='synthetic-model-prefault-begin'
```

doneflag remains known-bad and was not used as a completion signal or pass
criterion.
