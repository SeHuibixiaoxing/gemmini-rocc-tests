# 2026-05-07T08:30Z dummy8x8/sbus64 gdbserver run killed by stale watchdog

## Scope

Target under test:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Runtime config: `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- Run farm tag: `pairbertb8d12s64gdbcfg32nicnt`
- Instance killed: `i-0d8d6934878cac902`, private IP `192.168.1.128`

## What happened

The fresh 08:09 run was validly launched and infrasetup completed. The guest booted Linux, loaded IceNet, mounted the root filesystem, and reached:

```text
running /etc/init.d/S99run
```

The 08:09 run had not yet reached `[gdbserver] phase=listening`, and no GDB TCP connection had been made. The first GDB connection was therefore not consumed.

At 2026-05-07 08:17:40 UTC, a separate `firesim terminaterunfarm` started and terminated the live instance. AWS reported:

```text
StateReason.Code=Client.UserInitiatedShutdown
StateReason.Message=Client.UserInitiatedShutdown: User initiated shutdown
```

The terminaterunfarm log was:

```text
sims/firesim/deploy/logs/2026-05-07--08-17-40-terminaterunfarm-K415IAA3N5AI2JPP.log
```

That terminaterunfarm was not part of the 08:09 run's own timeout path. It matched an older run using the same runtime config and same `run_farm_tag`.

## Root cause

Stale local FireSim manager/watchdog state from older `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace` runs was still alive. In particular, old runworkload sessions using the same fixed runtime config and the same `run_farm_tag=pairbertb8d12s64gdbcfg32nicnt` remained in local process/tmux state.

The 05:17 run's watchdog reached its timeout around 08:17 and called `firesim terminaterunfarm --forceterminate` against the shared tag. Because the 08:09 fresh run reused that tag, the stale watchdog terminated the current run farm.

Evidence:

- `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-051724.watchdog.triggered`
- `sims/firesim/deploy/logs/2026-05-07--08-17-40-terminaterunfarm-K415IAA3N5AI2JPP.log`
- `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-080950.host-watchdog.log`

The 08:09 host watchdog saw guest status progress at 08:17:10, then immediately observed no pending/running instances after the external termination:

```text
guest-status arm observed at 2026-05-07T08:17:10Z
progress ... uart=16723 guest_sparse=1293 guest_status=1644 ...
no pending/running instances remain for pairbertb8d12s64gdbcfg32nicnt
```

## Cleanup done

I terminated stale local `firesim runworkload` processes and tmux sessions matching this exact dummy8x8/sbus64 gdbserver workflow. I did not delete logs, results, build outputs, or directories.

After cleanup, AWS showed no live F2 instances and `i-0d8d6934878cac902` reached `terminated`.

## Current blocker classification

This run does not yet locate the pipeline-runtime hang. It only establishes an infrastructure blocker:

- The image and hardware reached Linux userspace.
- The run reached `S99run`.
- The run was killed externally before gdbserver listening.
- The next valid run must start after confirming no stale same-tag managers/watchdogs are alive.

For the next run, before launch:

1. Confirm no local `firesim runworkload` process remains for `dummy8x8_4c12p12_sbus64` and `gdbserver_cfg32_nic_notrace`.
2. Confirm no tmux session remains matching `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-*`.
3. Confirm AWS has no pending/running/stopping/shutting-down F2 instances for `pairbertb8d12s64gdbcfg32nicnt`.
4. Launch/infrasetup/run fresh.
5. When `[gdbserver] phase=listening` appears in UART or the info file, connect with GDB as the first TCP client.

