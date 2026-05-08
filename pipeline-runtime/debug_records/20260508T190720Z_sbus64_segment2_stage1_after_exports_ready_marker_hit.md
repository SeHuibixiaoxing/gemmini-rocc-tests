# 20260508T190720Z - sbus64 segment2 stage1 after-exports-ready marker hit

## Scope

- Hardware: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO.
- AGFI: agfi-077451484fe3b63c3.
- AFI: afi-07989ce9ce725a690.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `192.168.1.196`, instance `i-080389dd4334f8dcc`.

## Marker Configuration

The guest image was patched and freshness-checked with:

- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-exports-ready`
- `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
- `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
- `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0`

Guest file logs, breadcrumb, deep logs, audit logs, checkpoint logs, and DMA probes were disabled. GDB was the first TCP client to `gdbserver --once`.

Image/runtime evidence:

- runtime binary sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- firemarshal env sha256: `7e1856b32ce7f553f063259ba9bf2d1822e43e7eb0f93e7da3f80c02fa5be4f5`
- remote image sha256: `4cb4b404e160e03da9119207e9051f241902877a5a496a54996634236e6724cc`

## Commands

Workflow:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-exports-ready \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch

PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-exports-ready \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup

PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-exports-ready \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
FIRESIM_RUNWORKLOAD_WATCHDOG_SECONDS=21600 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.196
```

GDB attach:

```sh
PRT_GDB_MARKER_TIMEOUT=2400 \
PRT_GDB_MARKER_DELETE_AFTER_HIT=1 \
PRT_GDB_MARKER_DETACH=1 \
PRT_GDB_POST_MARKER_GDB_CMDS='<post-marker breakpoint script with a quoting bug>' \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  192.168.1.196 172.16.0.2:2345 32351
```

## Evidence

The marker hit in thread 2:

```text
Thread 2 "rerocc_pipeline" hit Breakpoint 1, prt_gdb_marker_stop ()
site_id = 22
segment_idx = 2
global_stage_id = 3
local_stage_id = 1
subbatch_id = 0
manager_id = 4
rc = 0
aux0 = 1
aux1 = 1
line = 4367
```

Interpretation of `aux0/aux1` at `prt_runtime.c:4367`:

- `export_count = 1`
- `shared_pair_count = 1`

Current thread backtrace:

```text
#0 prt_gdb_marker_stop
#1 prt_gdb_marker_note
#2 prt_runtime_gdb_marker
#3 stage_worker_main at prt_runtime.c:4360
```

All-thread sample at the marker:

```text
Thread 4: prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000) at prt_scheduler.c:225
Thread 3: prt_process_c1 -> prt_dma_copy_dram_to_spm_pages_prefix -> dma_copy_host_to_spm_pages_linux -> prt_host_virt_to_phys -> sysconf
Thread 2: marker hit at segment2/global-stage3/local-stage1 after exports-ready
Thread 1: prt_runtime_run sleeping
```

Artifact directory:

```text
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T190234Z-192_168_1_196-172_16_0_2/
```

Runworkload result directory:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-08--18-54-07-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Manager log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-08--18-54-07-runworkload-VY0PBTZXQB9PTRDV.log
```

## Result

This run proves segment2/stage1 reaches the worker loop after all entry waits and `exports_ready` checks for subbatch 0. Therefore the previous `worker-gemm-run` marker miss is not a stable proof that the permanent stall is before stage1 compute; it was likely timing-sensitive or the run entered a hard-to-interrupt region before GDB could stop.

The concurrent stack still shows segment2/stage2 blocked on `prt_pipebuf_wait_full(buf=0x96f48, idx=0)`, matching the prior concrete frontier for tensor 6 shared entry buffer 9.

The intended post-marker breakpoint sequence did not run because the inline GDB command string had a quoting error:

```text
Bad format string, non-terminated '"'.
```

The next clean run must use a checked command file instead of an inline multiline environment string. The next target is to continue from `worker-after-exports-ready` and stop at:

1. `build_stage_task_desc` for `stage_id == 1`.
2. `prt_gemm_conv_run` for `task->stage_id == 1`.
3. `sync_stage_export_aliases(segment_idx=2, stage_id=1, global_stage_id=3, subbatch_id=0)`.
4. `prt_process_c4` for segment2/stage1/tensor6 export.

Do not reuse this same `gdbserver --once` run for the next evidence point; the GDB client disconnected after the command error.
