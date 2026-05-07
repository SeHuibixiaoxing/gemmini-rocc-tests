Debug category: gdbserver / cfg32 NIC / dummy8x8 sbus64 / extended matrix

# 20260507T061559Z - dummy8x8 sbus64 cfg32 NIC gdbserver extended matrix PASS

## Context

This run retested the current no-TraceIO cfg32 NIC AGFI with the restored
SimpleNIC plusargs and an expanded GDB command matrix. The goal was to cover
the remaining operations from the old 1BP single-core gdbserver smoke:
`next` and explicit inferior variable / memory mutation.

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Target:
  `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`
- Build result:
  `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`
- Run host: `i-0a5ab099582b90a9e` / `192.168.1.68`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`

The runtime command line preserved the known-good SimpleNIC settings:

```text
+simplenic-relaxed-required-bytes=1
+simplenic-empty-switch-poll-interval=1024
+simplenic-token-debug=0
+cpu-managed-stream-debug=0
+heartbeat-polling-interval=100000000
```

## Commands

FireSim workflow:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.68
```

GDB was the first TCP client to `gdbserver --once`; no `nc` / telnet probe was
used. The helper installed a permanent ARP entry on the run host before opening
the SSH tunnel:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  192.168.1.68 172.16.0.2:2345 32345
```

Result:

```text
[pairdummy-gdb-expect] expect_rc=0
[pairdummy-gdb-expect] PASS
```

Run-farm shutdown:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

The run host entered `shutting-down` after artifact collection.

## Guest Bringup

The guest reached the expected gdbserver state:

```text
Registered IceNet NIC 00:12:6d:00:00:02
Starting network: OK
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=221
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=235
```

Guest-side gdbserver log:

```text
Process /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux created; pid = 235
Listening on port 2345
Remote debugging from host 172.16.0.1, port 43296
Detaching from process 235
```

## Covered GDB Matrix

This pass covered the full intended remote-gdbserver smoke matrix:

- `target remote`
- `info threads`
- `thread apply all bt`
- register reads
- disassembly at `$pc`
- inferior variable read/write through
  `g_prt_debug_filter.rr_cfg_id`
- inferior memory read/write through
  `x/wx $prt_gdb_filter_addr` and
  `set {unsigned int}($prt_gdb_filter_addr) = ...`
- `break prt_main_entry`
- `continue` to `prt_main_entry`
- `next` from the early software-only entry point
- multiple software breakpoints on pipeline-runtime functions
- `continue` to a later pipeline-runtime breakpoint
- backtrace at breakpoint
- Ctrl-C / SIGINT recovery while the inferior was running
- post-interrupt thread, register, and disassembly reads
- `detach`

Observed markers:

```text
GDB_MARK_CONNECTED
GDB_MARK_MEMORY_RW_DONE
GDB_MARK_HIT_MAIN_ENTRY
GDB_MARK_NEXT_DONE
GDB_MARK_HIT_FIRST_BREAK
GDB_MARK_INTERRUPT_BEGIN
GDB_MARK_INTERRUPT_DONE
GDB_MARK_DETACH_OK
```

Variable / memory mutation evidence:

```text
$1 = 0xffffffff
set variable g_prt_debug_filter.rr_cfg_id = 0x5a5aa5a5
$2 = 0x5a5aa5a5
0x56328 <g_prt_debug_filter+48>: 0x5a5aa5a5
set {unsigned int}($prt_gdb_filter_addr) = 0xa5a55a5a
0x56328 <g_prt_debug_filter+48>: 0xa5a55a5a
set variable g_prt_debug_filter.rr_cfg_id = $prt_gdb_old_rr_cfg
$3 = 0xffffffff
```

The first later breakpoint hit was:

```text
Breakpoint 8, prt_gemmini_spm_xlate_program(...)
```

Ctrl-C recovery stopped in libc during YAML allocation, with a usable stack
back into `prt_load_pipeline_yaml()` and `prt_runtime_run()`.

## Packet Evidence

The run-host pcap captured a clean bidirectional TCP session:

- file:
  `pipeline-runtime/debug_records/artifacts/20260507T061559Z_dummy8x8_sbus64_gdbserver_extended_matrix_pass/remote/gdb-extended-staticarp-20260507T061554Z.pcap`
- `1587 packets captured`
- no kernel drops reported by tcpdump
- TCP handshake:
  `172.16.0.1.43296 > 172.16.0.2.2345 [S]`,
  `172.16.0.2.2345 > 172.16.0.1.43296 [S.]`,
  host ACK followed by RSP payload exchange
- clean close after detach:
  host FIN, target FIN, final ACK

Permanent neighbor entry:

```text
172.16.0.2 lladdr 00:12:6d:00:00:02 PERMANENT
```

## Artifacts

Local artifact directory:

```text
pipeline-runtime/debug_records/artifacts/20260507T061559Z_dummy8x8_sbus64_gdbserver_extended_matrix_pass/
```

Important contents:

- `remote/uartlog`
- `remote/gdbserver.info`
- `remote/gdbserver.log`
- `remote/switchlog`
- `remote/sim-run.sh`
- `remote/gdb-extended-staticarp-20260507T061554Z.pcap`
- `remote/gdb-extended-staticarp-20260507T061554Z.tcpdump.log`
- `local/pairdummy-cfg32-expect-20260507T061559Z-192_168_1_68-172_16_0_2/`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-launchrunfarm-20260507-060155.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-infrasetup-20260507-060232.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-060642.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-terminaterunfarm-20260507-061838.pane.log`

## Interpretation

The current dummy8x8 sbus64 cfg32 NIC no-TraceIO AGFI can run the old
single-core remote-gdbserver smoke matrix when the runtime keeps the restored
SimpleNIC plusargs. The previous empty-plusargs failure does not reproduce
under these settings.

This is a gdbserver-only result. It does not claim that the pipeline-runtime
model computation itself completes, because that was intentionally not tested
in this round.
