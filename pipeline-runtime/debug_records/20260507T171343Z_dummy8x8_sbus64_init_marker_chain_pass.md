# 20260507T171343Z dummy8x8/sbus64 init marker chain pass

## Goal

Validate that the current `dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO`
AGFI can keep one GDB connection alive across multiple runtime-initialization
source markers. This is the follow-up to the token548 run that appeared to
stall near initialization before producing valid DMA evidence.

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Type: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host: `i-03626871e2cb92bbb`
- Run host private IP: `192.168.1.157`
- Guest endpoint: `172.16.0.2:2345`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Runworkload session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-170026`

The FireMarshal-staged runtime ELF matched the local GDB helper ELF:

```text
1fc158f693f6c47d33ade03cb220fe32d38f39316a0d0ecf21bdb766f0229132
```

## Guest marker configuration

The workload image was built with:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=artifact-mapping-parse-done
```

The helper then updated `g_prt_gdb_marker_filter.site_id` from GDB inside the
same remote session to walk the initialization chain.

## Command

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=2400 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_init_marker_chain.sh \
  192.168.1.157 172.16.0.2:2345 32345
```

## Result

Pass. GDB connected as the first TCP client, hit all requested marker sites,
kept the same remote session alive across the chain, and detached cleanly with
`gdb_rc=0`.

Marker hits:

```text
site 14 artifact-mapping-parse-done: aux0=16848 aux1=870 line=1071
site 15 artifact-validate-done:     aux0=15    aux1=16848 line=1243
site 16 synthetic-prefault-begin:   aux0=17055744 aux1=4164 line=982
site 17 synthetic-prefault-end:     aux0=17055744 aux1=4164 line=1019
site 18 synthetic-ready:            aux0=17055744 aux1=0 line=1357
site 1  runtime-ready:              aux0=15    aux1=1 line=5104
```

The final trusted stack at `runtime-ready` was:

```text
#0 prt_gdb_marker_stop()
#1 prt_gdb_marker_note(site_id=1, ...)
#2 prt_runtime_gdb_marker(... line=5104)
#3 prt_runtime_run(...)
#4 prt_main_entry(argc=34, argv=...)
#5 libc
#6 __libc_start_main()
#7 _start()
```

## Interpretation

The previous `token548` marker run did not prove a DMA hang. This run shows that
the target can be controlled through mapping parse, artifact validation,
synthetic model allocation/prefault, and `runtime-ready`. The false frontier in
the earlier run came from observability lag rather than a demonstrated stop
inside mapping parse or prefault.

The sparse guest log captured after detach still ended around:

```text
[prt-progress] synthetic-model alloc before-mlock ...
```

That is stale relative to the GDB transcript, which already proved
`synthetic-ready` and `runtime-ready`. Therefore, for GDB runs, the source
marker transcript is stronger evidence than the file-backed sparse log or stale
runner-proc snapshots. Guest files are still useful as supporting evidence, but
they must not be used as the sole frontier after GDB stop/detach.

The next debugging frontier is now post-init:

- `segment-begin`
- worker creation/entry
- compute dispatch
- export synchronization
- DMA submit/wait markers

## Artifacts

Copied under:

```text
pipeline-runtime/debug_records/artifacts/20260507T171343Z_dummy8x8_sbus64_init_marker_chain_pass/
```

Key files:

- `gdb/pairdummy-cfg32-marker-stop.gdb`
- `gdb/pairdummy-cfg32-marker-stop.gdb.log`
- `gdb/ssh-tunnel.log`
- `gdb/static-neigh.log`
- `manager/runworkload.pane.log`
- `manager/runworkload.log`
- `runhost/uartlog.tail.txt`
- `runhost/heartbeat-and-stats.txt`
- `guest/bertmini-batch8.log.tail.txt`
- `guest/status-gdbserver-runner.txt`
- `staged-elf.sha256`

## Next step

Terminate this already-used `gdbserver --once` run farm and start a fresh run
with the first marker at `segment-begin`. Then keep the same GDB session open
and mutate the marker filter toward worker/export/DMA sites, so the next timeout
has a precise semantic coordinate rather than a stale log frontier.
