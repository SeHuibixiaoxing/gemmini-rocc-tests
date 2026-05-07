# 2026-05-07T15:32Z dummy8x8/sbus64 marker hit with wrong host symbol file

## Goal

First live test of the new source-level `prt_gdb_marker_stop()` workflow on the
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` AGFI.

## Target

- AGFI: `agfi-077451484fe3b63c3`
- Run host: `i-012e34a444b22df61`
- Run host private IP: `192.168.1.67`
- Guest endpoint: `172.16.0.2:2345`
- Marker env:
  - `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
  - `PIPELINE_RUNTIME_GDB_MARKER_SITE=runtime-ready`
- Runworkload session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-152059`

## What Passed

Guest boot reached the workload:

- IceNIC enumerated as `00:12:6d:00:00:02`.
- rootfs mounted from `iceblk`.
- `/etc/init.d/S40network` reported `Starting network: OK`.
- `/etc/init.d/S99run` started.
- `gdbserver` announced:
  - `phase=prelaunch`
  - `phase=listening`
  - `phase=inferior`
- guest IPv4 was `172.16.0.2`.
- run-host static neighbor was installed:
  `172.16.0.2 lladdr 00:12:6d:00:00:02 PERMANENT`.
- cross-gdb was the first TCP client through
  `localhost:32345 -> 172.16.0.2:2345`.
- `break prt_gdb_marker_stop` was accepted and later hit.

## Problem

The GDB command used the wrong local ELF:

```text
generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

That file did not match the FireMarshal-staged image binary:

```text
staged build ELF:
  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
  sha256=6f59324bde25e49b2ad80afbf6e9d21fee0270699e4adb38cd29a74c612a5622

wrong source-tree ELF used by manual GDB:
  generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_pipeline_runtime-linux
  sha256=812145dbb08297f2b821ac2209b31507508e25c4c267c4a35abf9cc0e8f125d9
```

The breakpoint hit is therefore valid as a control-flow proof, but the printed
`g_prt_gdb_marker_state`, line attribution, and deeper call stack are not
trustworthy. The bad marker state decoded to ASCII fragments such as
`RIGG`, `ER_S`, `UBBA`, `PIPE`, `LINE`, which is consistent with reading the
wrong data address rather than the runtime marker state.

## Artifacts

Artifacts were copied to:

```text
pipeline-runtime/debug_records/artifacts/20260507T153227Z_dummy8x8_sbus64_marker_wrong_symbol/
```

Included:

- `gdb_marker_runtime_ready.log`
- `runworkload.pane.log`
- `live_extract.txt`

## Cleanup

The SSH tunnel was killed, and the FireSim run farm was terminated:

- terminate session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-terminaterunfarm-20260507-153426`
- instance:
  `i-012e34a444b22df61`
- observed state after terminate:
  `shutting-down`

## Fix

Added a dedicated helper:

```text
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh
```

The helper defaults to the staged build ELF:

```text
generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

It also records the target ELF sha256 in its transcript before attaching.

## Next Step

Rerun the same marker test with the helper:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```
