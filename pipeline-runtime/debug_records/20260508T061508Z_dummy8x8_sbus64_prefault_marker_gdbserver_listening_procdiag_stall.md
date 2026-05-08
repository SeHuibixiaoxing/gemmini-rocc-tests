# 20260508T061508Z dummy8x8/sbus64 prefault-marker gdbserver listening and procdiag stall

## Context

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-0661abf56e8b6b62f`, private IP `192.168.1.142`
- Workload result:
  `sims/firesim/deploy/results-workload/2026-05-08--06-08-07-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Captured artifacts:
  `pipeline-runtime/debug_records/artifacts/20260508T061508Z_dummy8x8_sbus64_prefault_marker_gdbserver_prelaunch_proc_wchan_stall/`

## Guest Setup

The image carried the intended prefault marker filter:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0
```

Local and remote image freshness passed:

```text
local/remote image sha256 = 8429813f17bc54b122cd05344e0b465a2eab248acd1b23adbde3598e5b74bc00
remote /firemarshal.env sha256 = 62cade1834c02aa9fedf8983e0bace1ebf708a3feabff2c85b48625ea7775ec9
runtime ELF sha256 = 62aa6489325dcec067e94ad04401f2b26f544afc73c1bb57bb1aabdc74414f42
```

## What Happened

The live sparse log and runner stage showed:

```text
[bertmini-stage] before-bin method=ours2 ts=1970-01-01T00:00:04Z pid=144
```

The first live read of `runner-proc.stage` ended mid-record at:

```text
label=before-bin method=ours2 ...
cmdline=/bin/sh /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh --batch 8
wchan=
```

This made the run look like it had not reached gdbserver launch. That was an
incomplete interpretation.

The captured `bertmini-batch8.gdbserver.info` showed the runner had reached
the prelaunch phase:

```text
phase=prelaunch
method=ours2
tool=/usr/bin/gdbserver
endpoint=0.0.0.0:2345
net_dev=eth0
guest_ipv4=172.16.0.2
pid=0
```

More importantly, the captured `bertmini-batch8.gdbserver.log` showed that
gdbserver had actually created the inferior and was listening:

```text
Process /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux created; pid = 235
Listening on port 2345
```

So this run was a missed GDB attach opportunity. The first TCP client rule was
still preserved, but I terminated the run before invoking GDB.

## Root Cause Of The Misread

`run_rerocc_pipeline_runtime_bertmini.sh` writes process diagnostics on the
critical runner path. The captured `runner-proc.stage` is consistent with a
partial write inside `append_proc_stage`, around the `wchan=` field. That
diagnostic path can lag or block independently of the gdbserver child.

The practical lesson is:

- do not rely on `runner.stage` alone for gdbserver readiness;
- check `bertmini-batch8.gdbserver.log` and `bertmini-batch8.gdbserver.info`;
- if `gdbserver.log` says `Listening on port 2345`, attach GDB as the first
  TCP client even if runner telemetry has not yet emitted `after-bin-spawn`.

## Cleanup

After artifact capture, the run farm was terminated through:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin \
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0 \
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

AWS reported `i-0661abf56e8b6b62f` in `shutting-down` state after cleanup.

## Next Fix

Make runner process diagnostics low-risk by default:

- avoid reading `/proc/<pid>/wchan` on the critical path unless explicitly
  enabled;
- keep status/cmdline diagnostics, but never let a diagnostic read decide
  whether gdbserver can be reached;
- on the next fresh run, use `gdbserver.log` readiness and immediately invoke
  `run_pairdummy_cfg32_gdbserver_synthetic_prefault_frontier.sh`.

doneflag remains known-bad and was not used as completion evidence in this run.
