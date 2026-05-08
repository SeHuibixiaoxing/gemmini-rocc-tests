# 2026-05-08T01:02Z dummy8x8/sbus64 token16 window helper left gdbserver unrecoverable

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-0be570f23f8f1f6a6`, private IP `192.168.1.111`
- Guest endpoint: `172.16.0.2:2345`
- Runworkload session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-003324`

The tested runtime ELF SHA256 was:

```text
062f9dcc6899f355f26d0444bf0d8bf3bc9abe2d146718ac6e076b0c82793a3a
```

## Commands

The image was rebuilt/reinstalled with:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=segment-begin
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0
```

The first GDB run used the token16/page15 window helper:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=540 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_token16_window.sh \
  192.168.1.111 172.16.0.2:2345 32345
```

After that helper exited, a controlled GDB reconnect/stack-sample attempt used:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_SAMPLE_COUNT=1 \
PRT_GDB_SAMPLE_SECONDS=10 \
PRT_GDB_EXPECT_TIMEOUT=360 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_stack_sample.sh \
  192.168.1.111 172.16.0.2:2345 32346
```

No `nc`, `curl`, or `telnet` probe touched the guest gdbserver port.

## Result

This run is not valid token16 frontier evidence.

The first helper hit only the initial segment-begin marker:

```text
site_id=2 segment_idx=0 line=5181
```

It then waited for the token16/page15 wait-return marker and eventually exited
with `gdb_rc=124`. The transcript ended in the token16 section with:

```text
Cannot execute this command while the target is running.
Use the "interrupt" command to stop the target
and then try again.
```

The later reconnect attempt proved that the gdbserver connection was no longer
usable. GDB was the first client for this reconnect and printed:

```text
Remote debugging using :32346
Remote communication error.  Target disconnected: Connection reset by peer.
```

The guest image debug files showed:

- `bertmini-batch8.log` reached `init ready` and then
  `segment=0 init begin`.
- There were no later `action-generate`, `worker-create`, DMA wait, or page
  accounting lines.
- `bertmini-batch8.gdbserver.log` recorded only the original remote debugging
  connection from `172.16.0.1`.
- Breadcrumb decode ended at:
  `last_kind=runtime last_phase=runtime_init_done`.

The F2 instance was then terminated through the workflow. A post-terminate AWS
query showed no running or pending F2 instances.

## Interpretation

Do not interpret this run as evidence that token16/page15 is unreachable. The
runtime had not produced any post-segment-begin progress in the captured guest
logs, and the reconnect result shows the debug control path was already broken.

The useful conclusion is operational:

- Killing a long-running non-interactive GDB command with the shell `timeout`
  can leave a `gdbserver --once` workload in an unrecoverable state.
- Future long marker walks should use an expect-style helper that explicitly
  interrupts the target, records a stack sample, and detaches on timeout instead
  of relying on external `timeout` to kill GDB.
- The next dynamic probe should restart from a fresh run and stop much earlier:
  `segment-begin -> worker-create -> worker-entry -> first DMA wait`, only then
  advance toward the token16/page window.

## Artifacts

Artifacts are archived under:

```text
pipeline-runtime/debug_records/artifacts/20260508T005453Z_dummy8x8_sbus64_token16_helper_left_gdbserver_reset/
```

Key files:

- `gdb/token16-window/pairdummy-cfg32-marker-stop.gdb.log`
- `gdb/stack-reconnect/pairdummy-cfg32-stack-sample.expect.log`
- `guest/bertmini-batch8.log.txt`
- `guest/bertmini-batch8.gdbserver.log.txt`
- `guest/breadcrumb.decode.txt`
- `manager/runworkload.pane.log`
- `manager/terminaterunfarm.pane.log`
- `aws/instance-i-0be570f23f8f1f6a6.json`
- `aws/live-f2-after-terminate.json`
