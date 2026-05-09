# 20260509T113650Z no-DMA chunk8k sparse-log write stall

## Context

Reran the dummy8x8/sbus64/cfg32 NIC noTrace F2 no-DMA workflow after reducing
`prt_gemmini_artifacts.c` generic artifact file read chunking from 1 MiB to
8 KiB.

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

- instance: `i-03ce2c06e6b01d953`
- private IP: `192.168.1.202`

Software state:

- rocc-tests: `174fedce16ddf53a4fe8c2b534fc610cf722ef1c`
- gemmini: `8530dae64a3a1f2206766f10e407f6e8ae5a5065`
- top: `b00f45b64370fde108a64f2bd420e6000c464558`
- runtime binary SHA256 in image preflight: `152443db8982e3b056f8e417861efa10f4c850d3cc3ad74d685a9047631f46c6`
- remote/local image SHA256 after `infrasetup`: `03d2f79b3f57cec82a4dae21c8bb2438a004fb2ff9e27138d8a6250216b6ee4f`

## Observation

The rebuilt image entered runtime and confirmed the intended artifact path:

```text
[prt-progress] runtime begin ... no_dma_compute=1 ...
[prt-progress] artifacts file read begin ... bytes=3908768 chunk_limit=8192
```

The 8 KiB chunk setting was active and the runtime advanced through many small
reads. The final saved sparse log stayed at exactly `114688` bytes. The stable
frontier was not a `read chunk-begin`; it was a partial `chunk-end` line:

```text
[prt-progress] artifacts file read chunk-end path=/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globaln
```

Immediately before the partial line, the complete sparse log showed:

```text
[prt-progress] artifacts file read chunk-end ... off=1024000 read=8192
[prt-progress] artifacts file read chunk-begin ... off=1024000 chunk=8192
```

This means the 8 KiB `read()` at offset `1024000` had already returned. The
next visible operation was appending a long progress line to the guest sparse
log, and that line itself stopped mid-string.

Host watchdog state also stayed stable:

- `guest_sparse=114688`
- `uart=21191`
- `guest_status=1725`
- `guest_runner=495`
- `guest_runner_proc=9336`
- heartbeat remained header-only
- no `BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL`

`rerocc_pipeline` remained present and was recorded as `R (running)` in the
captured run-host process snapshot.

## Interpretation

The 8 KiB chunk-size change moved past the previous second-1MiB-read hypothesis,
but the new evidence points at logging self-interference rather than artifact
read data movement:

- the last complete `chunk-begin` was followed by a partial `chunk-end`, so the
  read completed before the final observable stall;
- the partial line stopped inside the repeated long artifact path string;
- local documentation already warns not to add high-frequency `write(O_APPEND)`
  text logs on the F2 guest block path.

Therefore the next local-first fix should reduce this observability load before
any further F2 rerun. The planned change is to keep the 8 KiB read chunking but
log only coarse read progress by default, with an environment override for local
or one-off target bisection.

This run is still before no-DMA compute; it is not evidence for Gemmini/SPM
or DMA completion behavior. It also does not use `doneflag` as completion
evidence.

## Run-Farm Cleanup

The run farm was terminated with the managed workflow:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` exited 0 and terminated `i-03ce2c06e6b01d953`. A follow-up
EC2 query for `f2.*` instances in `pending`, `running`, `stopping`, or `stopped`
returned an empty list.

## Next Step

Do not immediately launch another F2 run. First resolve locally/static:

1. throttle artifact read progress logging so the default F2 sparse log no
   longer emits two long `O_APPEND` writes per 8 KiB chunk;
2. validate with host build and CPU no-DMA dry-run that the reduced log still
   records `chunk_limit=8192`, `log_stride`, final artifact load completion,
   and `runtime end`;
3. record the logging lesson in `docs/constraints/lessons_learned.md`;
4. only then schedule a single F2 confirmation run if the local evidence is clean.

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T113650Z_no_dma_chunk8k_sparse_log_write_stall/`

Key files:

- `remote-debugfs-files/bertmini-batch8.log`
- `remote-debugfs-files/bertmini-batch8.status`
- `remote-debugfs-files/bertmini-batch8.runner-proc.stage`
- `remote-sim-slot/uartlog`
- `remote-sim-slot/heartbeat.csv`
- `remote-sim-slot/ps.txt`
- `manager/host-watchdog.log`
- `manager/runworkload.pane.log`
- `manager/runworkload-manager.log`
- `manager/terminaterunfarm.pane.log`
- `manager/ec2-after-terminate.yaml`
