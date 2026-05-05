# Current 1BP gdbserver recovery status, 2026-05-05

## Short status

The recovered current 1BP bitstream is `agfi-03d9518415ec82449`, not the
historical `2026-04-30` AGFI `agfi-0079cbbca617eca4e`.

As of `2026-05-05T12:10:59Z`, `agfi-03d9518415ec82449` has passed the same
remote `gdbserver` software-breakpoint capability matrix twice:

- `2026-05-05T11:10:31Z`, run host `192.168.1.197`, record
  `debug_records/20260505T111031Z.md`
- `2026-05-05T12:10:59Z`, run host `192.168.1.73`, record
  `debug_records/20260505T121059Z.md`

This means the current software/runtime stack and the rebuilt 1BP
single-core Rocket + NIC + no TraceIO + 30 MHz hardware path are both
functionally good for the required software-breakpoint remote GDB scope.

## Hardware under test

- AGFI: `agfi-03d9518415ec82449`
- AFI: `afi-0bf1f9a2bdacaab09`
- Build:
  `firesim_rocket_singlecore_nic_notrace_timingholdfixpcisreg1bp_30mhz`
- Build result:
  `sims/firesim/deploy/results-build/2026-05-05--06-39-45-firesim_rocket_singlecore_nic_notrace_timingholdfixpcisreg1bp_30mhz/`
- Config: `FireSimRocketNICNoTraceConfig + BaseF2Config`
- Type: single-core Rocket + NIC + no TraceIO + 30 MHz + 1BP
- Not tested here: `FireSimRocketNICNoTrace8BPConfig`

The build still emitted `post_route.VIOLATED.dcp`. That is not by itself a
functional failure for this project phase, because the older known-good AGFI
also had timing violations. The acceptance gate for this recovery step is the
remote GDB smoke matrix.

## Passing command

The successful runs used the same host-side expect driver:

```sh
GDBSERVER_SMOKE_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
GDBSERVER_SMOKE_EXPECT_TIMEOUT=240 \
bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_remote_gdbserver_software_expect_smoke.sh \
  <run-host-private-ip> 32345
```

For the repeat run:

```sh
GDBSERVER_SMOKE_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
GDBSERVER_SMOKE_EXPECT_TIMEOUT=240 \
bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_remote_gdbserver_software_expect_smoke.sh \
  192.168.1.73 32345
```

Result:

```text
[remote-swbreak-expect] expect_rc=0
[remote-swbreak-expect] PASS
```

## Covered behavior

- `target remote`
- multiple software breakpoints
- `continue`
- `next`
- `info threads`
- `thread apply all bt`
- register reads
- disassembly
- variable reads
- memory writes and reads
- thread switching
- Ctrl-C interrupt and control recovery
- `detach`

## Operational notes

- Do not probe the gdbserver TCP port with `nc`, `telnet`, or a generic socket
  check. The guest uses `gdbserver --once`, so the first TCP connection must be
  the real GDB/expect session.
- Wait for the UART marker before running expect:

```text
[gdbserver] phase=prelaunch net_dev=eth0 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
```

- The workload is a persistent remote-debug target and does not naturally
  finish after GDB `detach`; terminate the run farm after copying live evidence.
- Every key validation state must be committed before the next debugging step,
  with AGFI/AFI, configs, commands, result, artifact paths, and limitations in
  the commit message.

## Evidence

First current-AGFI PASS:

```text
pipeline-runtime/debug_records/artifacts/20260505T111031Z_agfi03d951_current1bp_gdb_pass/
```

Repeat current-AGFI PASS:

```text
pipeline-runtime/debug_records/artifacts/20260505T121059Z_agfi03d951_current1bp_repeat_gdb_pass/
```
