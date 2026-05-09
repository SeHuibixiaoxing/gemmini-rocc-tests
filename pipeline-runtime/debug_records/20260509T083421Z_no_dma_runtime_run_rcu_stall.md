# 20260509T083421Z no-DMA runtime_run RCU stall

## Context

Ran the dummy8x8/sbus64/cfg32 NIC noTrace F2 workflow with real DMA disabled
in the pipeline runtime:

- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
- runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- hwdb:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- AGFI: `agfi-077451484fe3b63c3`

The runtime binary in the refreshed image had SHA256:
`76a5b9ab3e679278cb343d931377dc3f03cf6c65e434da6fac9359e763a35c20`.

## Observation

The guest entered the user runtime and completed `runtime_init`, then stopped
making useful progress after entering `runtime_run`.

UART evidence:

- `[bertmini] runner-dummy-config dummy=1 skip_model=1 skip_input=1 skip_golden=1 no_dma_compute=1`
- `[prt-early] runtime_init done`
- `[prt-early] calling runtime_run`
- Linux later reported an RCU stall with
  `task:rerocc_pipeline state:R running task pid:205`

No `BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL` marker
was observed. Guest status remained `state=running`.

The host heartbeat reached `18335886153, 965`, then stayed alive without guest
runtime log growth. This run used the low-disturbance sbus64 profile, so
`guest_log_enable=0`, `guest_deep_log_enable=0`, and `breadcrumb_enable=0`.

## Result

No-DMA mode ruled out a simple "real DMA transfer never completes" explanation:
the run still stalls after `runtime_init` with DMA copy/submit/wait instructions
skipped by the runtime. The next useful split point is inside `runtime_run`,
using low-volume guest logs or targeted runtime markers rather than more
interactive gdbserver stepping.

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T083421Z_no_dma_runtime_run_rcu_stall/`

Key files:

- `remote/uartlog.txt`
- `remote/heartbeat.csv`
- `remote/guest-debug-files.txt`
- `local/host-watchdog.log`
- `local/runworkload.pane.log`
- `local/manager-runworkload.log`
- `local/ec2-before-terminate.json`

The F2 run farm was terminated after artifact collection, and a follow-up EC2
query showed no pending/running/stopping/stopped `f2.*` instances.
