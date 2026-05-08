# 20260508T201440Z - sbus64 segment2 stage1 entry tensor2 full return hit

## Scope

- Hardware: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO.
- AGFI: agfi-077451484fe3b63c3.
- AFI: afi-07989ce9ce725a690.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `192.168.1.247`.

## Marker Configuration

- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry-full-return`
- `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
- `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
- `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0`
- `PIPELINE_RUNTIME_GDB_MARKER_TENSOR=2`

Guest file logs, breadcrumb, deep logs, audit logs, checkpoint logs, and DMA probes were disabled.

Image/runtime evidence:

- runtime binary sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- firemarshal env sha256: `e795451fe2a8782ac0ac2d9bee87ae676c2e32d184997f7e34786e62a31dcf46`
- remote image sha256: `1f7129fbe4c0f4b965c750670303f729a6716e986772960a274668109cd8d809`

## Evidence

GDB-first marker hit:

```text
Thread 2 "rerocc_pipeline" hit Breakpoint 1, prt_gdb_marker_stop ()
site_id = 20
segment_idx = 2
global_stage_id = 3
local_stage_id = 1
subbatch_id = 0
manager_id = 4
tensor_id = 2
page_idx = 0
rc = 0
aux0 = 0
aux1 = 1
line = 4325
```

Interpretation:

- `site_id=20` is `worker-entry-full-return`.
- `tensor_id=2` is segment2/stage1 entry 1.
- `page_idx=0` is the pipe buffer index.
- `rc=0` means `prt_pipebuf_wait_full()` returned successfully for this entry.
- `aux1=1` is the second entry in the stage entry list.

All-thread sample at the marker:

```text
Thread 4: prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000) at prt_scheduler.c:225
Thread 3: hw_dma_fence -> dma_blocking_wait -> prt_dma_wait -> dma_submit_wait_annotated_scoped -> dma_copy_host_to_spm_pages_linux -> prt_process_c1(stage0 tensor0)
Thread 2: segment2/global-stage3/local-stage1 worker-entry-full-return for tensor2 rc=0
Thread 1: prt_runtime_run sleeping
```

Artifact directory:

```text
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T201126Z-192_168_1_247-172_16_0_2/
```

Runworkload result directory:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-08--20-03-02-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Manager log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-08--20-03-02-runworkload-I776AJHNI2P2DE0C.log
```

## Result

This run proves segment2/stage1 successfully receives its second entry, tensor 2, for subbatch 0. Combined with the previous tensor3 entry-full hit, segment2/stage1 entry waits are not the permanent blocker.

The current unresolved frontier returns to the post-entry portion of segment2/stage1:

1. `stage_wait_exports_ready()` for stage1.
2. `build_stage_task_desc()` / stage preparation for stage1.
3. `prt_gemm_conv_run()` for stage1.
4. `sync_stage_export_aliases()` and `prt_process_c4()` for tensor6 shared handoff into stage2.

The next clean run should use a checked command file to stop at `worker-after-exports-ready` and then follow the stage1 build/compute/export/C4 path. If `worker-after-exports-ready` is unstable again, use `worker-before-exports-ready` first, because both entries have now been proven available.

## Cleanup

The run farm was terminated through the workflow helper after the marker capture. GDB detached cleanly with `gdb_rc=0`.
