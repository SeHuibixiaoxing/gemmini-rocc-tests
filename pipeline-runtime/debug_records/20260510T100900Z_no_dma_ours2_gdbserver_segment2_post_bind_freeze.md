# 20260510T100900Z - no-DMA ours2 gdbserver segment2 post-bind heartbeat freeze

## Context

- Date: 2026-05-10 UTC
- Purpose: return to the known-good-like low-noise `gdbserver` path after
  repeated no-GDB perf-profile stalls, and check whether no-DMA `ours2` still
  reaches `runtime_run` / segment2.
- AGFI: `agfi-077451484fe3b63c3`
- Instance: `i-04daeff72e297010a`, private IP `192.168.1.158`
- Runtime profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`
- Workflow:
  `scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Key env:
  `METHODS=ours2`, `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`,
  `TRACE_ENABLE=0`, `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`,
  `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=uart`,
  `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`.
- Effective guest env SHA:
  `da796d6ec0b908ee4256dabdef7b70582775705a91dc41367dc05ca1d46d7d15`
- Runtime binary SHA in image:
  `0d126d06d4186316961aff539e9f72670fe8eabbd13a457a79172842f87e3a56`
- Remote/local image SHA:
  `c5ae22f2ae5dde6754902a417b0b65c7b28e6d98667319b1eb20ec11b7fe6d1d`

Archived evidence:

```text
pipeline-runtime/debug_records/artifacts/20260510T1009_no_dma_ours2_segment2_post_bind_heartbeat_freeze/
```

## GDB Route

Used the existing tunnel/helper path, with auto-continue disabled:

```bash
PRT_GDB_CONTINUE=0 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh \
  192.168.1.158 172.16.0.2:2345 32345
```

The first TCP client to guest `gdbserver --once` was GDB. No `nc`, `telnet`,
`curl`, or other port probe was used.

## Confirmed Progress

This run disproves the concern that the current no-DMA image cannot enter the
program:

- guest booted to `/etc/init.d/S99run`.
- gdbserver announced:
  `[gdbserver] phase=listening ... endpoint=172.16.0.2:2345 pid=202`.
- GDB attached successfully and initially stopped in the dynamic loader.
- The early `prt_gemmini_spm_xlate_program(enable=0)` hit was
  `runtime_bootstrap_spm_xlate()` reset during `prt_runtime_init`; it was not a
  hang.
- `prt_runtime_run()` was hit from `prt_main_entry()`.
- segment0 was reached:
  `seg_idx=0`, `num_stages=1`, `subbatch_size=1`.
- segment0 passed `prt_action_bind_topology()`, launched its worker, hit
  `stage_prepare_exec_views(stage_id=0)`, and entered `prt_gemm_conv_run()`.
- segment1 was reached:
  `seg_idx=1`, `num_stages=1`, `subbatch_size=1`, and passed
  `prt_action_bind_topology()`.
- segment2 was reached:
  `seg_idx=2`, `num_stages=3`, `subbatch_size=1`.
- segment2 passed into `prt_action_bind_topology()` with:
  `action->segment_idx=2`, `stage_thread_count=3`, `pipebuf_count=8`,
  `ringbuf_count=1`.

No active `prt_dma_submit`, `prt_dma_wait`, or `dma_blocking_wait` breakpoint
was hit before the final freeze.

## Freeze Shape

After continuing from segment2 `prt_action_bind_topology()`, no later segment
breakpoint or DMA breakpoint fired. The runhost heartbeat stopped updating at:

```text
Target Cycle (fastest), Seconds Since Start
18246938145, 964
```

The heartbeat file mtime remained:

```text
2026-05-10 10:09:07.219618340 +0000
```

while host processes were still alive:

```text
FireSim-f2: ~99.9% CPU
switch0: ~162% CPU
```

A first GDB Ctrl-C did not return a prompt for 120 seconds. A second Ctrl-C
reported:

```text
Disconnected from target.
```

No usable PC/thread stack was recoverable from this `gdbserver --once` session.

## Interpretation

The reliable frontier for this run is:

```text
no-DMA ours2 reaches segment2 and segment2 action bind;
after continuing from segment2 bind, heartbeat freezes before any observed
software DMA entry breakpoint or later segment boundary.
```

This is not evidence that current code can no longer enter the runtime or
segment2. It also should not override the stronger 2026-05-09 result where the
same cfg32/NIC/noTrace AGFI completed a full no-DMA compute run. The most likely
next explanation to test is GDB/manual-breakpoint perturbation or a very narrow
segment2 worker/custom-instruction window, not a broad artifact-read or
pipeline-YAML failure.

## Next Step

Do not repeat this broad manual-breakpoint run. If another F2 run is needed,
use the existing marker/expect helpers with a current-run listening filter and a
bounded timeout, for example:

- `run_pairdummy_cfg32_gdbserver_no_dma_segment2_stage1_gemm_frontier.sh`
- `run_pairdummy_cfg32_gdbserver_no_dma_segment2_stage2_c7_frontier.sh`

Those helpers should arm a marker before the segment2 worker frontier and let
the expect harness handle Ctrl-C. This avoids broad early breakpoints and avoids
manual long-continue sessions that lose the only `gdbserver --once` attach.

The no-GDB perf profile should not be rerun blindly until this segment2
frontier is reconciled with the 2026-05-09 full no-DMA PASS.

## Next-Run Local Preparation

Local `show` and `debug-preflight` were run for the recommended narrow marker:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

The profile remained low-noise:

```text
TRACE_ENABLE=0
breadcrumb_enable=0
guest_log_enable=0
guest_deep_log_enable=0
stdio_capture_mode=uart
disable_mapping_cache=1
debug_preflight_probe_tier=1
debug_preflight_status=pass
```

`image-closure` then completed locally without starting F2. Freshness results:

```text
firemarshal-env sha256=3389b14dcfc5aa2ac0692950338ff14313a7d7d20c6b02d707a95029e0e351b2
runtime-binary sha256=0d126d06d4186316961aff539e9f72670fe8eabbd13a457a79172842f87e3a56
```

The patched image `/firemarshal.env` confirmed:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'
PIPELINE_RUNTIME_GDBSERVER_ENABLE='1'
PIPELINE_RUNTIME_GDB_MARKER_ENABLE='1'
PIPELINE_RUNTIME_GDB_MARKER_SITE='worker-gemm-run'
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT='2'
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE='3'
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE='1'
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH='0'
```

If the next run is authorized, it should start from this image state and attach
with:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_segment2_stage1_gemm_frontier.sh \
  <run-host-private-ip> 172.16.0.2:2345 <local-port>
```
