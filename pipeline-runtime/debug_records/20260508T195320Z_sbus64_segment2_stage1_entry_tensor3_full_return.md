# 20260508T195320Z - sbus64 segment2 stage1 entry tensor3 full return hit

## Scope

- Hardware: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO.
- AGFI: agfi-077451484fe3b63c3.
- AFI: afi-07989ce9ce725a690.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `192.168.1.133`.

## Marker Configuration

- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry-full-return`
- `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
- `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
- `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0`

No tensor filter was used. Guest file logs, breadcrumb, deep logs, audit logs, checkpoint logs, and DMA probes were disabled.

Image/runtime evidence:

- runtime binary sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- firemarshal env sha256: `b6886307dcc7fa3c23337a703d3f12f27ba90c55dfbfb7aefdca61242b968c72`
- remote image sha256: `f12391464111910e4b10535395c9c1c7d54a1b5f37f64d64613362ef5881b627`

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
tensor_id = 3
page_idx = 0
rc = 0
aux0 = 0
aux1 = 0
line = 4325
```

Interpretation:

- `site_id=20` is `worker-entry-full-return`.
- `tensor_id=3` is segment2/stage1 entry 0.
- `page_idx=0` is the pipe buffer index.
- `rc=0` means `prt_pipebuf_wait_full()` returned successfully for this entry.
- `aux0=0` is the pipe buffer kind value for this entry.
- `aux1=0` is the entry index in the stage entry list.

All-thread sample at the marker:

```text
Thread 4: prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000) at prt_scheduler.c:225
Thread 3: prt_dma_wait -> dma_submit_wait_annotated_scoped -> dma_copy_host_to_spm_pages_linux -> stage_prepare_exec_views(stage_id=0) -> build_stage_task_desc(stage_id=0)
Thread 2: segment2/global-stage3/local-stage1 worker-entry-full-return for tensor3 rc=0
Thread 1: prt_runtime_run sleeping
```

Artifact directory:

```text
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T194956Z-192_168_1_133-172_16_0_2/
```

Runworkload result directory:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-08--19-41-32-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Manager log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-08--19-41-32-runworkload-LWTYAWHSZBK25WD3.log
```

## Result

This run proves segment2/stage1 reaches at least the first entry full-return boundary and successfully receives tensor 3 for subbatch 0. Therefore the next unresolved question is whether the second segment2/stage1 entry, tensor 2, also returns from `prt_pipebuf_wait_full()`.

The concurrent stack still shows segment2/stage2 waiting on the tensor6 shared entry buffer, which remains consistent with the higher-level stall. However, the producer side has not yet been proven to reach compute/export for stage1 in this run.

## Next Step

Run the same marker site with a tensor filter:

```sh
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry-full-return
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=2
```

If tensor 2 hits with `rc=0`, segment2/stage1 entry waits are not the permanent blocker and the next target should move back to `worker-after-exports-ready` or `worker-gemm-run` with a checked follow-up command file. If tensor 2 does not hit, the frontier is specifically the second entry handoff into segment2/stage1.

## Cleanup

The run farm was terminated through the workflow helper after the marker capture. GDB detached cleanly with `gdb_rc=0`.
