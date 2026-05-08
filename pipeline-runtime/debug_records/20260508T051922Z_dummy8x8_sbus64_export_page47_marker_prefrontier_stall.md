# 20260508T051922Z dummy8x8/sbus64 export page47 marker pre-frontier stall

## Context

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-04287ec41afaf027f`, private IP `192.168.1.86`
- Workload results:
  `sims/firesim/deploy/results-workload/2026-05-08--05-10-31-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Pre-interrupt artifacts:
  `pipeline-runtime/debug_records/artifacts/20260508T051922Z_dummy8x8_sbus64_export_page47_marker_preinterrupt/`
- Final artifacts:
  `pipeline-runtime/debug_records/artifacts/20260508T051922Z_dummy8x8_sbus64_export_page47_marker_final/`

## Guest Marker Setup

The run used an export-page guest marker:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-export-page-submit-begin
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=2
PIPELINE_RUNTIME_GDB_MARKER_PAGE=47
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0
```

Freshness checks passed locally and remotely. The guest `/firemarshal.env`
sha256 was:

```text
e5462372cf9e8d4388318aed342574b50e9549f4f190fe33d9edbcb13da95765
```

## GDB Attempt

The host helper was:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_EXPORT_STAGE=0 \
PRT_GDB_EXPORT_MANAGER=0 \
PRT_GDB_EXPORT_TENSOR=2 \
PRT_GDB_EXPORT_PAGE=47 \
PRT_GDB_MARKER_TIMEOUT=1800 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_export_page_frontier.sh \
  192.168.1.86 172.16.0.2:2345 32345
```

GDB was the first TCP client to `gdbserver --once`; no `nc`, `curl`, or
`telnet` probe was used.

GDB connected and set:

```text
break prt_gdb_marker_stop
```

but the export page47 marker never hit.

## Observed Frontier

The sparse log stopped before the export path:

```text
[prt-progress] artifacts validate segment-begin seg=14 stages=5
[prt-progress] artifacts validate stage-begin seg=14 local_stage=0 global_stage=35 layer=35 acc=2 entries=16848
```

No `stage-hit`, `stage-end`, segment runtime, fixed-load DMA, or export DMA
logs appeared after this point.

Breadcrumb is consistent with a pre-runtime or very early runtime frontier:

```text
update_count=182
last_kind=runtime
last_phase=runtime_init_done
line=4866
```

The sparse triage reports the most recent breadcrumb neighborhood as
`spm-xlate-release mgr=11 cfg=31 phase=restore-end`, followed by
`runtime_init_done`; there is no DMA evidence.

## GDB Recovery Attempt

After several minutes with no sparse-log growth, a pre-interrupt capture was
taken. A SIGINT to the local GDB process did not produce a target stack. A
SIGINT to the GDB process group caused:

```text
Disconnected from target.
gdb_rc=1
```

A second GDB-only reconnect diagnostic through a new SSH tunnel timed out:

```text
gdb_rc=124
```

The reconnect transcript is empty, so it did not obtain a stack.

## Interpretation

This run is not evidence for tensor2/page47 export DMA. It never reached the
export marker. The useful conclusion is narrower:

- the image and env were fresh for the export marker;
- GDB connected correctly as the first TCP client;
- the run stopped before the requested marker, with the last sparse log inside
  artifact validation for segment 14 / local stage 0;
- the batch-mode marker helper could not recover a stack on marker timeout;
- force-signalling the GDB process group disturbed the target and made the run
  unrecoverable.

This should be treated as a pre-frontier / observation-control failure, not as
a pipeline export-DMA failure.

## Cleanup

The run farm was terminated with:

```sh
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

FireSim reported termination for `i-04287ec41afaf027f`; AWS reported the
instance in `shutting-down` state immediately after cleanup.

## Next Step

Do not repeat this exact batch marker wait for a late export marker. The next
run should either:

1. use an expect-driven marker helper that can send GDB console Ctrl-C and dump
   stacks on marker timeout, or
2. use a staged marker chain: first stop at an early marker such as
   `artifact-mapping-parse-done`, then change `g_prt_gdb_marker_filter` in GDB
   to the export-page filter and continue.

doneflag polling remains invalid as completion evidence. This run did not use
doneflag as a completion decision.
