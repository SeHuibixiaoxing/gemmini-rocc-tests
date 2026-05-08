# 20260508T184826Z - sbus64 gdbserver worker-gemm-run marker missed

## Scope

- Hardware: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO.
- AGFI: agfi-077451484fe3b63c3.
- AFI: afi-07989ce9ce725a690.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `192.168.1.213`, instance `i-030582a58f8e6b19c`.

## Marker Configuration

The guest image was patched and freshness-checked with:

- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run`
- `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
- `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
- `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0`

The image freshness checks reported matching runtime binary and environment hashes:

- runtime binary sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- firemarshal env sha256: `b770cfa0499b3b9886871f791cfa256a2032ba12fcc1497de4006fe480663a29`

Guest file logs, breadcrumb, deep logs, audit logs, checkpoint logs, and DMA probes were disabled. The only intended frontier evidence was UART wrapper metadata plus gdbserver/GDB.

## Commands

Preflight:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh debug-preflight
```

FireSim workflow:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch

PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup

PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2 \
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 \
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
FIRESIM_RUNWORKLOAD_WATCHDOG_SECONDS=21600 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.213
```

GDB attach:

```sh
PRT_GDB_MARKER_TIMEOUT=2400 \
PRT_GDB_MARKER_DELETE_AFTER_HIT=1 \
PRT_GDB_MARKER_DETACH=1 \
PRT_GDB_POST_MARKER_GDB_CMDS='<frontier breakpoints after marker>' \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  192.168.1.213 172.16.0.2:2345 32350
```

The GDB helper used:

- GDB: `.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb`
- ELF: `generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- target ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`

## Evidence

UART showed that gdbserver started and GDB became the first TCP client:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

GDB transcript:

```text
0x0000003ff7fec386 in ?? () from /home/ubuntu/chipyard/.conda-env/riscv-tools/sysroot/lib/ld-linux-riscv64-lp64d.so.1
Breakpoint 1 at 0x14482: file /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_debug_state.c, line 396.
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T183726Z-192_168_1_213-172_16_0_2/pairdummy-cfg32-marker-stop.gdb:10: Error in sourced command file:
Disconnected from target.
```

Artifact directory:

```text
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T183726Z-192_168_1_213-172_16_0_2/
```

Runworkload result directory:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-08--18-27-35-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Manager log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-08--18-27-35-runworkload-2J41UDDVW5UBZED8.log
```

## Result

The `worker-gemm-run` marker for segment 2, global stage 3, local stage 1, subbatch 0 did not hit before the run became unresponsive to GDB control. A local SIGINT to the batch GDB did not recover a thread stack and eventually caused a target disconnect.

This rejects the immediate next hypothesis that the current run reliably reaches segment2/stage1 compute entry and then stalls inside or after `prt_gemm_conv_run`. The current frontier moved earlier than `PRT_GDB_MARKER_SITE_WORKER_GEMM_RUN` for segment2/stage1/subbatch0.

## Interpretation

The next marker run should move earlier in the worker loop for the same segment and stage:

1. `worker-after-exports-ready` for segment 2, global stage 3, local stage 1, subbatch 0.
2. If that does not hit, inspect entry waits for segment2/stage1 entries, especially tensor 2/tensor 3 handoff from earlier stages.
3. If it hits, continue through `worker-before-build-stage-task`, `worker-after-build-stage-task`, and then `worker-gemm-run` to identify the exact pre-compute transition.

Do not add guest file logging or breadcrumbs for this next step. The previous evidence shows the target can become hard to interrupt after GDB continues, so breakpoints need to be placed before the suspected pre-compute region.

## Cleanup

The run farm was force-terminated with the workflow helper after the failed GDB capture. AWS reported instance `i-030582a58f8e6b19c` in `shutting-down` state at the time of this record.
