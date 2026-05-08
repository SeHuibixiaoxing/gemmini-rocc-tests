# 2026-05-08 06:45 UTC: dummy8x8 gdbserver run ended before GDB attach

## Target

- AGFI/AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host: `i-01f19222219df8306`, private IP `192.168.1.211`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Marker:
  `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`,
  `PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin`

## What Happened

The run reached guest userspace and entered `/etc/init.d/S99run`, but the run
host shut down before gdbserver reached `Listening on port 2345`.

Last useful guest evidence from live debugfs polling:

- `uartlog` reached `running /etc/init.d/S99run`.
- `/root/pipeline-runtime-debug/bertmini-batch8.log` contained the FireMarshal
  wrapper preamble, including:
  - `gdbserver enable: 1`
  - `gdbserver endpoint: 0.0.0.0:2345`
  - `profile id: pairdummy-sbus64-dummy8x8-fixed-v3`
- `bertmini-batch8.gdbserver.log` was still empty.
- `bertmini-batch8.runner.stage` was still empty.
- Host watchdog saw nonzero wrapper/status/proc files but no runner progress:

```text
guest_status=1644 guest_proc=1290 guest_runner=0 guest_runner_post=363 guest_runner_early=0 guest_binary=0 guest_runner_proc=0
```

Then SSH to the run host began returning banner/connect failures, and EC2 state
showed:

```text
i-01f19222219df8306  shutting-down/terminated  Client.UserInitiatedShutdown
```

No GDB attach was attempted in this run because gdbserver never reached the
documented listening state.

## Important Local Finding

Before cleanup, the workstation had multiple stale `firesim runworkload`
manager processes and tmux sessions for the same runtime config and same
`run_farm_tag`:

- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-041030`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-051030`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-054242`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-060806`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-063150`

Because all of these use `run_farm_tag=pairbertb8d12s64gdbcfg32nicnt`, they can
interfere with a fresh run by polling or terminating the same tagged run farm.
This is the leading explanation for why the current host entered
`UserInitiatedShutdown` before the gdbserver attach point.

Those stale local manager/tmux sessions were killed after confirming there were
no running F2 instances left for the tag.

## Artifacts

```text
pipeline-runtime/debug_records/artifacts/20260508T064500Z_dummy8x8_sbus64_gdbserver_premature_host_shutdown/
```

Contains:

- `runworkload.pane.log`
- `firesim-runworkload.log`
- `host-watchdog.log`
- `monitor.log`
- `tmux.metadata`
- `aws-state-after-run.txt`
- `local-runworkload-processes-after-cleanup.txt`
- `tmux-ls-after-cleanup.txt`

## Next Action

Rerun from a clean local manager state:

1. Confirm no stale local `firesim runworkload` processes for this runtime.
2. Confirm no running/stopping F2 instances for `pairbertb8d12s64gdbcfg32nicnt`.
3. Launch a fresh run farm.
4. Run `infrasetup`.
5. Run workload and attach only after `bertmini-batch8.gdbserver.log` contains
   `Listening on port 2345`.

doneflag remains known-bad and was not used as a completion/pass signal.
