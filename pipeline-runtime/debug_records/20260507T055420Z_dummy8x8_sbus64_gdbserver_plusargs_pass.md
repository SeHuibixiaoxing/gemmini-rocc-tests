调试类别：gdbserver / cfg32 NIC / dummy8x8 sbus64 / plusarg recovery

# 20260507T055420Z - dummy8x8 sbus64 cfg32 NIC gdbserver triage PASS

## Context

This run retested the new no-TraceIO cfg32 NIC AGFI after restoring the
SimpleNIC host-driver plusargs that were present in the known-good 1BP
single-core gdbserver runs.

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Target:
  `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`
- Run host: `i-0ae7ffc75a6fc1624` / `192.168.1.125`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- Build result:
  `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`

## Change Under Test

The runtime `plusarg_passthrough` was changed from empty to:

```text
+simplenic-relaxed-required-bytes=1
+simplenic-empty-switch-poll-interval=1024
+simplenic-token-debug=0
+cpu-managed-stream-debug=0
+heartbeat-polling-interval=100000000
```

The cfg32 GDB triage helper also gained an optional static-neighbor setup:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02
```

This does not connect to the gdbserver port and does not consume
`gdbserver --once`; it only installs a permanent run-host neighbor entry before
GDB opens the SSH tunnel.

## FireSim Commands

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.125
```

`infrasetup` flashed `agfi-077451484fe3b63c3` and passed driver readiness.
The remote driver command line confirmed:

```text
using simplenic relaxed required bytes: 1
using simplenic empty switch poll interval: 1024 empty rounds
using simplenic token debug: 0 events
```

## Guest Bringup

The guest booted the expected image and reached:

```text
Registered IceNet NIC 00:12:6d:00:00:02
Starting network: OK
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=221
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=235
```

The guest-side gdbserver log contained:

```text
Process /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux created; pid = 235
Listening on port 2345
Remote debugging from host 172.16.0.1, port 42778
```

## GDB Command

No `nc` or telnet probe was used. The first TCP connection to gdbserver was
GDB:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  192.168.1.125 172.16.0.2:2345 32345
```

Result:

```text
[pairdummy-gdb-expect] expect_rc=0
[pairdummy-gdb-expect] PASS
```

Transcript:

`tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-expect-20260507T055420Z-192_168_1_125-172_16_0_2/pairdummy-cfg32-triage.expect.log`

## Covered Capability Matrix

The cfg32 triage script validated:

- `target remote`
- `info threads`
- `thread apply all bt`
- register reads
- disassembly at `$pc`
- multiple software breakpoints on pipeline-runtime functions
- `continue` to the first breakpoint
- backtrace at breakpoint
- Ctrl-C / SIGINT recovery while the inferior was running
- post-interrupt thread, register, and disassembly reads
- `detach`

Observed markers:

```text
GDB_MARK_CONNECTED
GDB_MARK_HIT_FIRST_BREAK
GDB_MARK_INTERRUPT_BEGIN
GDB_MARK_INTERRUPT_DONE
GDB_MARK_DETACH_OK
```

The first breakpoint hit was:

```text
Breakpoint 7, prt_gemmini_spm_xlate_program(...)
```

## Packet Evidence

A tap0 packet capture was taken during the GDB attach:

`pipeline-runtime/debug_records/artifacts/20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass/remote/gdb-plusargs-staticarp-20260507T055420Z.pcap`

Summary:

- 1403 packets captured
- tap0 RX: 705 packets / 102182 bytes
- tap0 TX: 704 packets / 56879 bytes
- permanent neighbor:
  `172.16.0.2 lladdr 00:12:6d:00:00:02 PERMANENT`
- TCP handshake completed:
  `172.16.0.1.42778 > 172.16.0.2.2345 [S]`,
  `172.16.0.2.2345 > 172.16.0.1.42778 [S.]`,
  host ACK followed by RSP payload exchange
- the connection closed cleanly with FIN/ACK after detach

This contrasts with the previous empty-plusargs run, where static ARP produced
only outbound SYN retransmissions and no target-to-host packets.

## Artifacts

Local artifact directory:

`pipeline-runtime/debug_records/artifacts/20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass/`

Important contents:

- `remote/uartlog`
- `remote/switchlog`
- `remote/sim-run.sh`
- `remote/gdb-plusargs-staticarp-20260507T055420Z.pcap`
- `remote/gdb-plusargs-staticarp-20260507T055420Z.tcpdump.log`
- `local/pairdummy-cfg32-expect-20260507T055420Z-192_168_1_125-172_16_0_2/`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-infrasetup-20260507-054023.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-054505.pane.log`

Run workload result dir:

`sims/firesim/deploy/results-workload/2026-05-07--05-45-06-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

## Interpretation

The current cfg32 NIC no-TraceIO bitstream is capable of supporting remote
gdbserver traffic when the runtime preserves the known-good SimpleNIC driver
plusargs. The previous `No route to host` / static-ARP SYN-timeout failure was
not sufficient evidence of a hardware NIC failure; it was reproduced with an
empty `plusarg_passthrough`, while this run passed with the old-success
SimpleNIC parameters restored.

This pass does not yet cover the full old single-core smoke matrix. In
particular, the current cfg32 triage script does not test `next` or explicit
inferior variable / memory mutation. The next gdbserver-only step should extend
the cfg32 expect flow to include those operations, then rerun the same AGFI
with the same plusargs and static-neighbor setup.

The run farm was terminated after artifact collection. Instance
`i-0ae7ffc75a6fc1624` entered `shutting-down`.
