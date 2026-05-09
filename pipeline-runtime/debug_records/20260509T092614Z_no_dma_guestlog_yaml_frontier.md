# 20260509T092614Z no-DMA guest-log YAML frontier

## Context

Reran the dummy8x8/sbus64/cfg32 NIC noTrace F2 workflow after fixing runner
guest-log path propagation.

Environment:

- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
- `CAPTURE_PERIODIC_SYNC_ENABLE=1`
- `CAPTURE_PERIODIC_SYNC_SECONDS=5`
- `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=1`

Workflow command:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.25
```

Runtime config:

`sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`

HWDB:

`sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

AGFI: `agfi-077451484fe3b63c3`

Freshness checks passed:

- runner-script SHA256:
  `b03a1b13204df2dcfdcb1852f48487e8039118c594f2dfe920f5e16c0528e2fb`
- firemarshal env SHA256:
  `c11cd588d067908337b550b8b20ba42bd1e67776b02927599201e9bab3dcb754`
- runtime binary SHA256:
  `76a5b9ab3e679278cb343d931377dc3f03cf6c65e434da6fac9359e763a35c20`
- remote image SHA256:
  `0c4a3ea558025124766cea315b0772277cd8ff6200bb37e8ef32453ba683f5e3`

## Observation

The run entered Linux userland, launched the bertmini runner, and reached:

- `[prt-early] runtime_init done`
- `[prt-early] calling runtime_run`

Unlike the previous guest-log-zero run, live `debugfs` showed that sparse guest
logging was active:

- `/root/pipeline-runtime-debug/bertmini-batch8.status` size `1702`
- `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage` size `495`
- `/root/pipeline-runtime-debug/bertmini-batch8.runner-proc.stage` size `9336`
- `/root/pipeline-runtime-debug/bertmini-batch8.log` size `165433`

Two live samples about 75 seconds apart showed the sparse log size remained
`165433`. The last readable sparse log line was:

```text
[prt-progress] yaml pipeline parse line=1439 indent=6 text=vAccIdxList:
```

The runner proc snapshot showed the child process still present and running:

```text
Name: rerocc_pipeline
State: R (running)
cmdline=... --backend fpga ... --no-dma-compute --skip-model-bin-load --skip-input-load --skip-golden-check
```

No `BERTMINI_PIPELINE_RUNTIME_PASS` or `BERTMINI_PIPELINE_RUNTIME_FAIL` marker
was observed.

## Static Follow-Up

The corresponding YAML source line is normal:

```text
1439      vAccIdxList:
1440      - [0, 1, 2, 3, 4, 5, 6, 7]
1441      splitKind: oc
```

Earlier host no-DMA CPU dry-run for the same dummy8x8/sbus64/ours2 artifact
exited 0 after a clean host rebuild, so the current evidence does not identify
a deterministic YAML parser bug.

The more likely interpretation is that per-line `PRT_PROGRESS_LOG` to the guest
sparse log is too intrusive for this no-DMA F2 bisection, or that the last
visible line is a live-rootfs flush frontier rather than the actual program
counter. The next run should suppress YAML per-line progress logging and keep
only coarse runtime checkpoints.

## Result

This run confirms that the fixed guest-log plumbing works and that no-DMA
execution reaches `runtime_run`, but the current per-line YAML sparse logging is
not a reliable low-perturbation frontier for the no-DMA compute bisection.

The run farm was terminated with:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` reported exit code 0 and a follow-up EC2 query showed no
pending/running/stopping/stopped `f2.*` instances. The stale runworkload tmux
pane was manually closed after the instance had already terminated.

## Artifacts

Saved under:

`generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T092614Z_no_dma_yaml_parse_stall/`

Key files:

- `remote-debugfs-files/20260509T092614Z_no_dma_yaml_parse_stall_fixed/bertmini-batch8.log`
- `remote-debugfs-files/20260509T092614Z_no_dma_yaml_parse_stall_fixed/bertmini-batch8.status`
- `remote-debugfs-files/20260509T092614Z_no_dma_yaml_parse_stall_fixed/bertmini-batch8.runner.stage`
- `remote-debugfs-files/20260509T092614Z_no_dma_yaml_parse_stall_fixed/bertmini-batch8.runner-proc.stage`
- `remote-debugfs/20260509T092614Z_no_dma_yaml_parse_stall/uartlog`
- `local-logs/2026-05-09--09-14-59-runworkload-TT05HLC9OBRBBYUJ.log`
- `local-logs/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-091458.pane.log`
- `f2_instances_at_capture.txt`
