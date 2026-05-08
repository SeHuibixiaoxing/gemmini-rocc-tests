# 20260508T203620Z - sbus64 segment2 stage1 before-exports-ready marker hit

## Scope

- Hardware: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO.
- AGFI: agfi-077451484fe3b63c3.
- AFI: afi-07989ce9ce725a690.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `192.168.1.130`.

## Marker Configuration

- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-exports-ready`
- `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
- `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
- `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0`

Guest file logs, breadcrumb, deep logs, audit logs, checkpoint logs, and DMA probes were disabled.

Image/runtime evidence:

- runtime binary sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- firemarshal env sha256: `a5dc40f4848db2a43d2b0e96cac034cab888e05d67d1eefde8021eee987f7e4e`
- remote image sha256: `19e4c57515e2e2c2fee02680c84c5027b14d64fc863932d0b9586647508656bb`

## Evidence

GDB-first marker hit:

```text
site_id = 21
segment_idx = 2
global_stage_id = 3
local_stage_id = 1
subbatch_id = 0
manager_id = 4
rc = 0
aux0 = 1
aux1 = 1
line = 4352
```

Interpretation:

- `site_id=21` is `worker-before-exports-ready`.
- `aux0=1` is `export_count`.
- `aux1=1` is `shared_pair_count`.
- Combined with the prior tensor3/tensor2 entry-full hits, segment2/stage1 has completed both entry waits and is about to call `stage_wait_exports_ready()`.

All-thread sample at marker:

```text
Thread 4: segment2/stage2 waits in prt_pipebuf_wait_full(buf=0x96f48, idx=0)
Thread 3: stage0 is inside prt_gemm_conv_run -> tiled_matmul path
Thread 2: segment2/global-stage3/local-stage1 before-exports-ready marker
Thread 1: prt_runtime_run sleeping
```

Artifact directory:

```text
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T203218Z-192_168_1_130-172_16_0_2/
```

Runworkload result directory:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-08--20-24-01-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Manager log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-08--20-24-01-runworkload-U6KJZ34FXQ2C1LTR.log
```

## Result

This run proves segment2/stage1 reaches the post-entry, pre-export-readiness boundary. The permanent blocker is not segment2/stage1 entry wait. The next frontier is one of:

1. `stage_wait_exports_ready()` for stage1.
2. `build_stage_task_desc()` / stage1 preparation.
3. `prt_gemm_conv_run()` for stage1.
4. `sync_stage_export_aliases()` and `prt_process_c4()` for tensor6 handoff.

The follow-up GDB command file did not execute because the first breakpoint condition used the wrong parameter name for `stage_wait_exports_ready`:

```text
No symbol "progress_sbatch" in current context.
```

The command file was fixed to use `subbatch == 0` instead.

## Cleanup

The run farm was terminated through the workflow helper after the marker capture. The same commit also fixes the GDB command file for the next run.
