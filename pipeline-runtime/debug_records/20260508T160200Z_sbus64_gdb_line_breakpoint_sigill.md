# 2026-05-08T16:02Z sbus64 GDB line-breakpoint SIGILL

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`
- Run host: `192.168.1.47`
- FireSim result directory: `sims/firesim/deploy/results-workload/2026-05-08--15-44-08-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- FireSim run log: `sims/firesim/deploy/logs/2026-05-08--15-44-08-runworkload-E1WKYFGY8LFJAJ00.log`

The image freshness checks passed before the run. The runtime binary in the image had SHA256 `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`, and `/firemarshal.env` had SHA256 `10329a9c03fac1549d237d05a2d9256c6fa0ce691f43c19716f1311773ee5668`.

## Commands

The run was launched with:

```sh
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
FIRESIM_RUNWORKLOAD_WATCHDOG_SECONDS=21600 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.47
```

`gdbserver` reached:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=203
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=216
```

GDB was the first TCP client to `gdbserver --once`, via:

```sh
ssh -i /home/ubuntu/firesim.pem \
  -o UserKnownHostsFile=/dev/null \
  -o StrictHostKeyChecking=no \
  -o ExitOnForwardFailure=yes \
  -N -L 32350:172.16.0.2:2345 ubuntu@192.168.1.47

.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

## GDB Event

Breakpoints were set at:

```text
1 prt_runtime.c:5222
2 prt_runtime.c:5226
3 prt_schedule_action.c:1247
4 prt_action_bind_topology
5 prt_runtime.c:3427
```

The run then stopped with:

```text
Program received signal SIGILL, Illegal instruction.
0x00000000000332fe in prt_action_bind_topology(...)
```

Disassembly around the PC:

```text
0x332f8 <prt_action_bind_topology+86>:  mul  a1,a1,s7
0x332fc <prt_action_bind_topology+90>:  add  a1,a1,a4
0x332fe <prt_action_bind_topology+92>:  divu a1,a1,s5
0x33302 <prt_action_bind_topology+96>:  divu a5,a5,s5
```

The important detail is that GDB reported breakpoint 3 at `0x33300`:

```text
3 breakpoint keep y 0x0000000000033300 in prt_action_bind_topology
  at .../pipeline-runtime/src/prt_schedule_action.c:1248
```

That address lies inside the 4-byte instruction beginning at `0x332fe`. The software breakpoint corrupted the instruction stream and caused the observed `SIGILL`. This is a debugger-induced artifact, not a valid pipeline-runtime failure frontier.

## Outcome

- This run is invalid for runtime-frontier evidence because the line breakpoint at `prt_schedule_action.c:1247/1248` was unsafe.
- The run did prove that the guest reached `gdbserver` cleanly on this image and that GDB could attach as the first TCP client.
- The run farm was terminated afterward with the workflow `terminate` command. The cluster tag `pairbertb8d12s64gdbcfg32nicnt` had no pending/running/stopping/stopped instances after termination.

## Constraint Added For Next Runs

Do not set software line breakpoints on optimized code around dense RISC-V instruction regions unless the chosen address is verified to be an instruction boundary. Prefer:

- function-entry breakpoints such as `break prt_action_bind_topology`
- stepping from a known safe function-entry breakpoint
- `finish` to observe return values
- temporary breakpoints at symbol addresses or disassembled instruction starts
- UART-only one-shot failure diagnostics for fail paths

Avoid line breakpoints in the `prt_action_bind_topology()` fail block until their exact instruction address is checked with `x/i` or objdump.
