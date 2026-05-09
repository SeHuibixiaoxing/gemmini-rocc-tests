# 20260509T084710Z no-DMA guest-log zero after runtime_run

## Context

Reran the dummy8x8/sbus64/cfg32 NIC noTrace F2 workflow after enabling the
sbus64 guest-log override:

- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
- runtime config:
  `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- hwdb:
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- AGFI: `agfi-077451484fe3b63c3`

Image and remote freshness both passed. The remote image SHA256 matched the
local image:
`d1d633aeff2278fcd51f9260e3b8d27aa573b5f1ed929a9e883dbbc4ed049689`.

## Observation

The run again entered the runtime and stopped after:

- `[prt-early] runtime_init done`
- `[prt-early] calling runtime_run`

Host watchdog armed on guest status at `2026-05-09T08:54:30Z` and then reported
stable no-progress samples with `uart=21061`, `guest_log=0`, `guest_status=1702`,
and heartbeat still at the CSV header.

Live `debugfs` extraction before terminating the run showed:

- `state=running`
- `guest_log_enable=1`
- `no_dma_compute_enable=1`
- `gdbserver_enable=0`
- `/root/pipeline-runtime-debug/bertmini-batch8.log` size `0`
- runner stage/proc stage files size `0`

No `BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL` marker
was observed before manual termination.

## Static Follow-Up

The zero-byte guest log was not valid evidence that `runtime_run` stalled before
the first coarse runtime log. Static inspection found that the file-only wrapper
passed `PIPELINE_RUNTIME_GUEST_LOG_PATH` to the runner, but the runner did not
propagate `PIPELINE_RUNTIME_GUEST_LOG_PATH`, deep-log path, audit-log path, or
checkpoint-log path into the runtime binary environment.

With `PIPELINE_RUNTIME_UART_LOG_ENABLE=0`, this leaves `PRT_PROGRESS_LOG`
without a guest log fd and without UART fallback. The low-noise sbus64 profile
also had periodic sync disabled, so even corrected guest file writes need a
temporary sync override to be visible during a live hang.

## Result

This rerun confirmed the same no-DMA `runtime_run` stall shape, but the guest log
data was inconclusive because of observability plumbing. Next rerun should use
the fixed runner env propagation plus:

- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
- `CAPTURE_PERIODIC_SYNC_ENABLE=1`
- optionally `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=1`

The run farm was force-terminated at `2026-05-09T09:00:35Z`; a follow-up EC2
query showed no pending/running/stopping/stopped `f2.*` instances.

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T084710Z_no_dma_guestlog_zero_pre_coarse_log/`

Available local files:

- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-084710.host-watchdog.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-084710.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-infrasetup-20260509-084231.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-terminaterunfarm-20260509-090032.pane.log`
- `local/ec2-after-terminate.json`
- `results/HW_CFG_SUMMARY`
- `results/sim-run.sh`
