# 2026-05-07 17:43Z - dummy8x8 sbus64 segment0 first-DMA marker chain

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-088b71fc139de7161`, private IP `192.168.1.185`, cluster `pairbertb8d12s64gdbcfg32nicnt`
- ELF:
  `generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- ELF sha256: `82f66cbc2e012277f0e98e4dd14ab5e8a5af8e79bea6411ae1c0c19bfb8844eb`

## Command

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=2400 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain.sh \
  192.168.1.185 172.16.0.2:2345 32345
```

The guest image was configured with:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=segment-begin
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1
```

## Result

Partial pass, then manual interrupt while waiting for the next `worker-gemm-run` marker.

The same GDB session successfully hit these markers:

- `site_id=2`: `segment-begin`, `segment_idx=0`, `aux0=1`, `aux1=1`
- `site_id=3`: `worker-create`, `segment_idx=0`, `global_stage_id=0`, `local_stage_id=0`, `manager_id=0`, `aux1=8`
- `site_id=4`: `worker-entry`, `segment_idx=0`, `global_stage_id=0`, `local_stage_id=0`, `manager_id=0`, `aux0=1`, `aux1=1`
- `site_id=12`: first `dma-wait-enter`, `tensor_id=0`, `token_id=1`, `manager_id=0`, `subbatch_id=0`
- `site_id=13`: first `dma-wait-return`, same token, `rc=0`

The first stage0 input DMA therefore completed successfully. The unresolved window is after the first `dma-wait-return` from `prt_process_c1()` and before `worker-gemm-run`.

The current helper is too coarse for this gap because it switches the marker filter directly from `dma-wait-return` to `worker-gemm-run`. Existing code between those two points includes:

- returning from `prt_process_c1()`
- `prt_pipebuf_wait_full()`
- `stage_wait_exports_ready()`, including `export-empty`
- `build_stage_task_desc()`
- descriptor/log preparation before `prt_runtime_gdb_marker(PRT_GDB_MARKER_SITE_WORKER_GEMM_RUN, ...)`

## Notes

I attempted to interrupt the long wait to sample a stack. Sending SIGINT to the GDB process group caused GDB to report `Disconnected from target` instead of printing an intermediate target stack. Treat the target state after this run as disturbed; use a fresh run for the next test.

Guest sparse log and breadcrumb files lagged behind the GDB transcript. The transcript is the stronger evidence for the segment0/worker/DMA markers.

## Artifacts

`pipeline-runtime/debug_records/artifacts/20260507T174300Z_dummy8x8_sbus64_segment0_first_dma_pass_gemm_pre_gap/`

- `gdb/pairdummy-cfg32-marker-stop.gdb`
- `gdb/pairdummy-cfg32-marker-stop.gdb.log`
- `guest/bertmini-batch8.log`
- `guest/bertmini-batch8.breadcrumb.bin`
- `guest/bertmini-batch8.breadcrumb.decode.txt`
- `guest/triage_prt_capture.txt`
- `manager/runworkload.pane.tail.txt`
- `manager/ec2-instance.txt`

## Next

Add narrower GDB marker sites between `after-c1` and `worker-gemm-run`, then rerun from a fresh gdbserver workload. If the gap remains hard to resolve, add a no-DMA compute-only bisection path that marks entry/fixed tensors ready without DMA and tests only Gemmini dispatch/fence/export-sync boundaries.
