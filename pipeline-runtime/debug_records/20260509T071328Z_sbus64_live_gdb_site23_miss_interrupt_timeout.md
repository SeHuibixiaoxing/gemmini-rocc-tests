# 20260509T071328Z - sbus64 live GDB site23 miss with interrupt timeout

## Goal

Re-run the live interactive GDB flow correctly after the scheduler-locking
misarm: connect to `gdbserver --once`, set only the marker breakpoint, leave
scheduler locking off, and wait for
`site=23 worker-before-build-stage-task` for
`segment=2/global_stage=3/local_stage=1/subbatch=0/manager=4`.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host:
  - Instance: `i-07375fb3a5c221d42`
  - Private IP: `192.168.1.148`
  - Instance type: `f2.6xlarge`
  - FireSim cluster tag: `pairbertb8d12s64gdbcfg32nicnt`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--06-51-59-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Runworkload tmux:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-065158`
- Runworkload log:
  `sims/firesim/deploy/logs/2026-05-09--06-51-59-runworkload-D178C4W0RJGOKGVH.log`
- GDB tunnel:
  `prt-gdb-tunnel-sbus64-postflush-valid`, local `:32367` to guest
  `172.16.0.2:2345`
- GDB tmux:
  `prt-gdb-sbus64-postflush-valid`
- Transcript directory:
  `tmp/firesim-aws-f2/gdb-live/20260509-0700-sbus64-postflush-valid/`

## Freshness and Entry Checks

The local and remote FireMarshal image matched:

```text
local_sha256=a9bf2f451edc4fca66599068736e6bd6175a4c3a25b9967c4b47a23d029edf36
remote_sha256=a9bf2f451edc4fca66599068736e6bd6175a4c3a25b9967c4b47a23d029edf36
runtime-binary sha256=b0c99c20e02c995c24d0f1fcaaba1c8e2a59b6f844c746cc94404fc7d407a73c
firemarshal-env sha256=12ee85d975c9998247e13ce28a11567084ca73dbcb69346a228c45a3281e87a1
```

The guest env was:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=4
```

UART confirmed the GDB server and inferior phases:

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

GDB symbol sanity matched the staged ELF:

```text
&g_prt_gdb_marker_state = 0x58360
prt_gdb_marker_stop = 0x1457a
```

Unlike the previous invalid round, this run did **not** enable
`set scheduler-locking on` before continuing to the marker.

## Observation

GDB set only:

```gdb
break prt_gdb_marker_stop
continue
```

The intended `worker-before-build-stage-task` marker did not hit in the
observed window. UART stayed at the gdbserver inferior handoff point with no
later runtime text because this profile has guest sparse/deep/checkpoint logs
and breadcrumbs disabled. The host watchdog saw the guest status file arm and
then no further guest-side file progress:

```text
guest-status arm observed at 2026-05-09T06:59:18Z
uart=20822
guest_status=1678
guest_log=0 guest_sparse=0 guest_checkpoint=0 guest_trigger=0
```

A controlled GDB interrupt after the marker miss only echoed `^C` and did not
return a GDB prompt:

```text
(gdb) continue
Continuing.
^C
```

After an additional short wait, no stop prompt or stack was recovered.

## Interpretation

This is a valid negative live-GDB result for this run: without the
scheduler-locking misarm, `site=23 worker-before-build-stage-task` still did not
hit, and remote interrupt did not recover a stack.

This does not invalidate the earlier positive run that reached `site=23` and
then walked through `stage_prepare_exec_views()` to `prt_runtime.c:2319`. It
does show that `site=23` is not a reliable first stop for every run when the
target can enter a remote-interrupt-unresponsive path before that marker.

Current evidence split:

- Earlier valid positive round:
  the path can reach `site=23` and then pass fixed-load DMA, SPM bind, and
  manager 4/5/6/7 SPM xlate flush, moving the post-site23 frontier to
  `stage_prepare_exec_views()` after `runtime_flush_stage_spm_xlate()` returns.
- This valid negative round:
  a fresh run with correct GDB setup missed `site=23` and could not be
  interrupted, so there is also a pre-site23 failure mode.

The next useful run should not wait directly on `worker-before-build-stage-task`
as the first marker. It should stop at an earlier already-observed ladder point,
then dynamically add the post-flush ladder if execution reaches site23. Candidate
earlier stops:

```text
worker-entry-process-return
worker-entry-full-return
worker-before-exports-ready
worker-after-exports-ready
```

If those earlier markers also miss, the run should move the first stop even
earlier to `worker-entry` or `segment-begin` and avoid relying on post-hoc
interrupt for PC recovery.

## Cleanup

The run farm was terminated with:

```text
pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` targeted `i-07375fb3a5c221d42` and returned exit code 0. A
follow-up AWS query confirmed the instance reached `terminated`.
