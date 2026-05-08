# 2026-05-08 07:01 UTC: dummy8x8 gdbserver attach reached synthetic prefault marker

## Target

- AGFI/AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host: `i-0e7c767e2f3ca0915`, private IP `192.168.1.179`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Marker:
  `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`,
  `PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin`

## Commands

GDB attach was launched only after the guest printed the gdbserver listening
announcement. No `nc`, `curl`, or `telnet` probe was used against
`172.16.0.2:2345`; the first TCP client was GDB.

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_PREF_HIT_TIMEOUT=1200 \
PRT_GDB_PREF_ADVANCE_TIMEOUT=180 \
PRT_GDB_PREF_MIN_PAGES=3328 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_synthetic_prefault_frontier.sh \
  192.168.1.179 172.16.0.2:2345 32345
```

## Result

The gdbserver path worked:

- `target remote` connected through the FireSim NIC.
- The marker breakpoint at `prt_gdb_marker_stop` hit.
- The marker state reported `site_id=16`, line `982`, `aux0=17055744`,
  `aux1=4164`.
- Backtrace placed the marker at:
  `prt_runtime_run -> allocate_synthetic_model_blob -> prefault_and_lock_blob`.
- Continuing from the marker reached synthetic model prefault progress stops at
  `touched_pages=3328`, `3584`, `3840`, and `4096`.
- The helper then reached `after-prefault` with `touched_pages=4164` and
  detached cleanly with `gdb_rc=0`.

This rules out `synthetic-model-prefault-begin` and the synthetic model prefault
loop as the immediate hang point for this run.

After detach, the runtime continued beyond initialization and entered segment
execution. A live snapshot after the GDB session showed progress through segment
0 and into segment 1:

```text
[prt-progress] stage-exec-views phase=end segment=1 stage=0 layer=1 rc=0 flush=1
[prt-progress] worker stage=0 subbatch=7 begin op=1 acc=0 dma=0 tiles=8
[prt-progress] conv-sync-strided stage=0 mgr=7 flushed use_pointwise=1
```

The run was still live at the time this record was written. Completion/stall
status must be recorded separately.

## Source/Image Freshness Finding

Static inspection during this run found that the local and remote FireMarshal
image still contained the old canonical runner from:

```text
generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
```

The old canonical runner had two problems:

- `append_proc_stage` read `/proc/<pid>/wchan` unconditionally.
- It lacked the GDB marker defaults/logging/env forwarding that had already
  been restored in the coupled-DMA workload overlay copy.

The workload `host-init.sh` copies the canonical runner into the overlay path
on image build, so patching only
`rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
is insufficient. The canonical runner was updated so the two copies now match.

The current live run used the old image content; the source fix must be applied
to the next FireMarshal image before re-running freshness-sensitive tests.

## Artifacts

```text
pipeline-runtime/debug_records/artifacts/20260508T070107Z_dummy8x8_sbus64_gdbserver_prefault_marker_pass/
```

Important files:

- `gdb/synthetic-prefault.gdb.log`
- `gdb/synthetic-prefault-*.gdb`
- `live-debugfs-snapshot.txt`
- `live-uart-heartbeat.txt`
- `host-watchdog.log`
- `runworkload.pane.log`

`doneflag` was not used as a completion or pass signal.
