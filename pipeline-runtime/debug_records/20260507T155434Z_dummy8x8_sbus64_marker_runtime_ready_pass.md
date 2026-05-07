# 2026-05-07T15:54Z dummy8x8/sbus64 runtime-ready GDB marker pass

## Goal

Validate the source-level `prt_gdb_marker_stop()` strategy with the correct
FireMarshal-staged ELF on the current `dummy8x8 / sbus64 / cfg32 / NIC /
no TraceIO` F2 AGFI.

This is the follow-up to
`20260507T153227Z_dummy8x8_sbus64_marker_wrong_symbol.md`, where the marker hit
but the local host ELF did not match the staged runtime binary.

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Config: dummy8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host: `i-05911d8f6da348736`
- Run host private IP: `192.168.1.198`
- Guest endpoint: `172.16.0.2:2345`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Runworkload session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-154236`

## Command

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=1200 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  192.168.1.198 172.16.0.2:2345 32345
```

The helper used the staged build ELF:

```text
generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
sha256=6f59324bde25e49b2ad80afbf6e9d21fee0270699e4adb38cd29a74c612a5622
```

## Result

Pass for the intended GDB marker smoke:

- GDB connected through the SSH tunnel as the first TCP client.
- `break prt_gdb_marker_stop` resolved to
  `pipeline-runtime/src/prt_debug_state.c:332`.
- The breakpoint hit in `prt_gdb_marker_stop()`.
- `g_prt_gdb_marker_state` decoded correctly.
- `bt`, `thread apply all bt`, `info registers`, and a PC disassembly window
  all completed.
- GDB detached cleanly with `gdb_rc=0`.

The marker state at the stop was:

```text
site_id = 1
segment_idx = 4294967295
global_stage_id = 4294967295
local_stage_id = 4294967295
subbatch_id = 4294967295
manager_id = 4294967295
tensor_id = 4294967295
page_idx = 4294967295
token_id = 4294967295
rc = 0
aux0 = 15
aux1 = 1
hit_count = 0
line = 5079
```

Interpretation:

- `site_id=1` is `runtime-ready`.
- `aux0=15` is `all_num_segments`.
- `aux1=1` is `all_subbatch_size`.
- `line=5079` matches the marker call in `prt_runtime.c`.

The trusted stack at the marker:

```text
#0 prt_gdb_marker_stop()
#1 prt_gdb_marker_note(site_id=1, ...)
#2 prt_runtime_gdb_marker(... line=5079)
#3 prt_runtime_run(...)
#4 prt_main_entry(argc=34, argv=...)
#5 libc
#6 __libc_start_main()
#7 _start()
```

## Meaning

The flexible source-marker strategy is valid on this AGFI. It is better than a
pure GDB-side conditional breakpoint for this target because the runtime can
pre-filter on stable semantic coordinates such as segment, local/global stage,
subbatch, manager, tensor, page, and token before GDB needs to stop the target.

For forward localization, the preferred next pattern is:

1. Enable a narrow source marker, for example `segment-begin`,
   `worker-entry`, `worker-export-sync`, `dma-export-page-submit-begin`, or
   `dma-wait-enter`.
2. Stop in `prt_gdb_marker_stop()`.
3. In the same GDB session, delete the marker breakpoint or leave it disabled.
4. Add the next small breakpoint set immediately after the frontier.
5. Continue and collect the next stop or timeout.

The current helper detached after collecting this marker. That is correct for a
smoke checkpoint, but not ideal for multi-hop localization because the workload
was launched by `gdbserver --once`. Future frontier runs should keep the same
GDB connection open until the post-marker breakpoint set has been installed.

## Artifacts

Artifacts were copied to:

```text
pipeline-runtime/debug_records/artifacts/20260507T155434Z_dummy8x8_sbus64_marker_runtime_ready_pass/
```

Included:

- `pairdummy-cfg32-marker-stop.gdb`
- `pairdummy-cfg32-marker-stop.gdb.log`
- `ssh-tunnel.log`
- `static-neigh.log`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-154236.pane.log`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-154236.host-watchdog.log`
- `uartlog.tail.txt`

## Next Step

Run the next test with `PRT_GDB_MARKER_DETACH=0` or a dedicated frontier script
that keeps GDB attached after the source marker hit, installs the next breakpoint
set, and only detaches after the post-frontier evidence is collected.
