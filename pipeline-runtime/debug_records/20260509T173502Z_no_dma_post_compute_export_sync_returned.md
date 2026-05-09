# 2026-05-09 no-DMA post-compute export-sync returned

## Context

- Purpose: continue the no-DMA compute bisection after proving manager 7 compute returns.
- Target: `segment=1`, `global_stage=1`, `local_stage=0`, `subbatch=3`.
- Frontier: `worker-export-sync` marker, then `sync_stage_export_aliases()`.
- AGFI: `agfi-077451484fe3b63c3`.
- F2 instance: `i-04209f91a7a674c67`, private IP `192.168.1.13`.
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- rocc-tests SHA before this record: `00faaf6`.
- gemmini SHA before this record: `a5fb8d2`.
- chipyard SHA before this record: `6b7eba13`.

## Command

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=2400 \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_post_compute_thin.sh \
  192.168.1.13 172.16.0.2:2345 32345
```

## Evidence

- GDB/expect completed with `expect_rc=0` and `PASS`.
- Marker hit:
  - `site_id=6` (`worker-export-sync`)
  - `segment_idx=1`
  - `global_stage_id=1`
  - `local_stage_id=0`
  - `subbatch_id=3`
  - `rc=0`
  - marker call line: `prt_runtime.c:4599`
- `sync_stage_export_aliases()` entry hit for:
  - `rt=0x3fffffde28`
  - `segment_idx=1`
  - `stage_id=0`
  - `global_stage_id=1`
  - `subbatch_id=3`
- Temporary breakpoint after the call stopped at `prt_runtime.c:4604`.
- Register/local state at the stop:
  - `a0=0`
  - local `rc=0`
  - disassembly showed `jal sync_stage_export_aliases`, then `mv s1,a0`, then the stop before `prt_log_gate_clear_context()`.

## Conclusion

For this no-DMA window, the card is not inside `sync_stage_export_aliases()`.
The next frontier is immediately after `prt_runtime.c:4604`, especially:

- `prt_log_gate_clear_context()`
- `wrk-postcmp`
- `compute-done`
- entry pipebuf release loop `wrk-p1-b/e`
- export pipebuf publish/process loop `wrk-p2-b/e`
- next subbatch handoff

Static read confirms `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE` is not used by
`prt_gemmini_artifacts.c`, so no-DMA itself should not affect artifact file
reading. Artifact read chunk behavior is controlled by
`PIPELINE_RUNTIME_ARTIFACT_FILE_READ_CHUNK_BYTES` and
`PIPELINE_RUNTIME_ARTIFACT_FILE_READ_LOG_STRIDE_BYTES`.

## Artifacts

- Main artifact directory:
  `pipeline-runtime/debug_records/artifacts/20260509T173502Z_no_dma_post_compute_export_sync_returned/`
- GDB transcript:
  `pipeline-runtime/debug_records/artifacts/20260509T173502Z_no_dma_post_compute_export_sync_returned/gdb/pairdummy-cfg32-marker-frontier.expect.log`
- Run host capture:
  `pipeline-runtime/debug_records/artifacts/20260509T173502Z_no_dma_post_compute_export_sync_returned/runhost/uartlog`
- Heartbeat:
  `pipeline-runtime/debug_records/artifacts/20260509T173502Z_no_dma_post_compute_export_sync_returned/runhost/heartbeat.csv`
- Manager log:
  `pipeline-runtime/debug_records/artifacts/20260509T173502Z_no_dma_post_compute_export_sync_returned/manager/2026-05-09--17-22-42-runworkload-C8NTYKJULXSYXIII.log`
- Termination log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-09--17-39-33-terminaterunfarm-0G53PCVJSFEY6YCM.log`

## Limitations

- This is a frontier result, not a whole-workload pass.
- The run was stopped by GDB after `sync_stage_export_aliases()` returned; it does not prove later pipebuf release/publish or final runtime completion.
- `heartbeat.csv` only captured the header plus one target-cycle sample during the gdbserver session.
- No DMA completion claim is made in this no-DMA run.
