# 20260508T193540Z - sbus64 after-exports-ready rerun marker missed

## Scope

- Hardware: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO.
- AGFI: agfi-077451484fe3b63c3.
- AFI: afi-07989ce9ce725a690.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `192.168.1.52`, instance `i-0b65a81bfe1bf834c`.

## Marker Configuration

- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-after-exports-ready`
- `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
- `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
- `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
- `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0`

Guest file logs, breadcrumb, deep logs, audit logs, checkpoint logs, and DMA probes were disabled.

The run used the fixed post-marker command file:

```text
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_follow_after_exports_ready.gdb
```

## Evidence

UART confirmed gdbserver startup and GDB-first attach:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

GDB transcript:

```text
0x0000003ff7fec386 in ?? () from /home/ubuntu/chipyard/.conda-env/riscv-tools/sysroot/lib/ld-linux-riscv64-lp64d.so.1
Breakpoint 1 at 0x14482: file /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_debug_state.c, line 396.
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T192603Z-192_168_1_52-172_16_0_2/pairdummy-cfg32-marker-stop.gdb:10: Error in sourced command file:
Disconnected from target.
```

The marker did not hit before manual SIGINT/termination of the local batch GDB. SIGINT again did not recover a thread stack and ended in target disconnect.

Artifact directory:

```text
/home/ubuntu/chipyard/tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T192603Z-192_168_1_52-172_16_0_2/
```

Runworkload result directory:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-08--19-17-43-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Manager log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-08--19-17-43-runworkload-5S0L65UNZELSO9RB.log
```

## Result

This rerun did not reproduce the previous `worker-after-exports-ready` marker hit. Since an immediately previous clean run did hit the same marker with `rc=0`, the current evidence points to a run-to-run moving frontier under the same no-file-log gdbserver profile.

The frontier is now between:

- segment2/stage1 reaching `worker-after-exports-ready` in the successful 20260508T190720Z run, and
- segment2/stage1 failing to reach that marker in this rerun.

The next clean run should move the marker earlier, in this order:

1. `worker-entry-full-return` filtered to segment 2, global stage 3, local stage 1, subbatch 0. Use tensor filter if needed to distinguish tensor 3 vs tensor 2 entries.
2. `worker-entry-process-return` filtered to the same stage if entry full does not hit.
3. If those are unstable, mark `worker-entry` for segment2/stage1 and use a post-marker command file to break on `prt_pipebuf_wait_full`, `prt_process_c1`, and the producer-side C2/C8 handoffs.

Do not add guest file logs or breadcrumbs. GDB remains useful, but breakpoints must be placed before the hard-to-interrupt wait/DMA regions.

## Cleanup

The run farm was terminated through the workflow helper. AWS reported instance `i-0b65a81bfe1bf834c` in `shutting-down` at record time. Local GDB/tunnel/runworkload remnants were killed.
