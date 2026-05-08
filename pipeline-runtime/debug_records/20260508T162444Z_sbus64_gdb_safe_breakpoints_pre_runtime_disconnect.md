# 2026-05-08T16:24Z sbus64 GDB safe-breakpoint pre-runtime disconnect

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`
- Run host: `192.168.1.210`
- FireSim result directory: `sims/firesim/deploy/results-workload/2026-05-08--16-10-08-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace`
- FireSim run log: `sims/firesim/deploy/logs/2026-05-08--16-10-08-runworkload-48VIOPI90JLPPE11.log`

The image freshness checks passed before the run. The local and remote image SHA256 was `1334a828915217a43b1d7f52434c52431e709062c19e868f4a18a5280b0049fe`. The runtime binary in the image had SHA256 `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`, and `/firemarshal.env` had SHA256 `10329a9c03fac1549d237d05a2d9256c6fa0ce691f43c19716f1311773ee5668`.

## Commands

The run was launched with:

```sh
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
FIRESIM_RUNWORKLOAD_WATCHDOG_SECONDS=21600 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.210
```

`gdbserver` reached:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=203
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=216
```

GDB was the first TCP client to `gdbserver --once`, via a tunnel on local port `32350`.

## Breakpoint Policy Used

This run intentionally avoided the unsafe fail-block line breakpoint that caused the previous `SIGILL`. Only these breakpoints were set:

```text
break prt_action_bind_topology
break prt_runtime.c:5222
break prt_runtime.c:5226
```

The `prt_action_bind_topology` function-entry breakpoint resolved to `0x332a2`. No breakpoint was placed at `prt_schedule_action.c:1247/1248`.

## Observed Result

After `continue`, none of the three breakpoints fired for about three minutes. A GDB Ctrl-C attempt did not return a thread stop. A second Ctrl-C produced:

```text
Disconnected from target.
```

UART after the disconnect still only showed wrapper/gdbserver metadata:

```text
[bertmini] runner-enter batch=8 methods=ours2
[gdbserver] phase=prelaunch ...
[gdbserver] phase=listening ...
[gdbserver] phase=inferior ...
```

There was no `action_bind_topology` diagnostic line in UART, and no evidence that the process reached `prt_runtime.c:5222` or `prt_action_bind_topology` before the GDB connection dropped.

The run farm was terminated afterward. The cluster tag `pairbertb8d12s64gdbcfg32nicnt` had no pending/running/stopping/stopped instances after termination. The stale local FireSim manager process for this terminated run was also killed.

## Current Interpretation

This run does not move the software frontier to bind. Instead, it shows that using only bind/runtime breakpoints is too late for the current execution path: either the process is spending a long time before `prt_runtime_run`, or GDB/gdbserver loses the connection while waiting for that point.

The next run should move the breakpoint frontier earlier:

- `break prt_main_entry`
- `break main`
- `break prt_runtime_run`
- only then step/continue toward `prt_runtime.c:5222` and `prt_action_bind_topology`

If the process reaches `prt_main_entry` but not `prt_runtime_run`, inspect command-line parsing and YAML/model load setup. If it reaches `prt_runtime_run` but not segment/bind breakpoints, use `next` and `finish` from there instead of waiting on late breakpoints.
