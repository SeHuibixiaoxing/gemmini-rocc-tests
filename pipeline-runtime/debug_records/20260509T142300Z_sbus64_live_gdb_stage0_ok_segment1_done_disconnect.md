# 20260509T142300Z - sbus64 live GDB proved stage0 compute return and segment1 worker-done frontier

## Goal

Continue the low-log live-GDB run on the historical
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` F2 profile, without adding
guest runtime logs. The immediate question was whether the run still entered
the program and whether it could progress beyond segment0 after the earlier
breakpoint cleanup.

This was a continuation of the live session started at
`2026-05-09--13-41-04`.

## Configuration

- Date: `2026-05-09` UTC
- AGFI: `agfi-077451484fe3b63c3`
- Run host:
  - Instance: `i-0f8a5229a111f752f`
  - Private IP: `192.168.1.131`
  - Instance type: `f2.6xlarge`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Build recipes loaded by this runworkload:
  `sims/firesim-staging/sample_config_build_recipes.yaml`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--13-41-04-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- GDB evidence directory:
  `tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/`
- GDB transcript:
  `tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/gdb_live.log`
- Extra copied artifacts:
  `tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/artifacts/`

Effective guest settings:

```text
NO_DMA=0
GDBSERVER=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0
guest sparse/deep/audit/checkpoint/breadcrumb/periodic sync disabled
dma_force_direct=1
no_dma_compute=0
log_profile=coarse
```

## Live-GDB Observations

The session was already attached with the current staged ELF. The run had
previously reached `stage_worker_main()` after the first segment0/stage0
`prt_gemm_conv_run()` call.

At the return boundary:

```text
stage_worker_main at prt_runtime.c:4548
rc = 0
ctx->stage_id = 0
task.op_kind = PRT_STAGE_OP_CONV
task.split_kind = PRT_LAYER_SPLIT_OC
task.tile_count = 8
ctx->action->segment_idx = 0
```

This proves that the run did enter the program and that segment0/stage0
compute returned successfully in this live-GDB round. The earlier impression
that the program could not enter was not reproduced.

The first attempt to skip forward used breakpoint 10 on
`prt_debug_state_set_worker()` with:

```gdb
condition 10 segment_idx==2 && local_stage_id==1 &&
  (phase_id==PRT_DEBUG_PHASE_GEMM_PREP || phase_id==PRT_DEBUG_PHASE_GEMM_RUN)
```

After a long `continue`, heartbeat did not advance from:

```text
36499678307, 1887
```

An interactive interrupt succeeded and stopped at:

```text
Thread 3
prt_debug_state_set_worker(
  segment_idx=1,
  global_stage_id=1,
  local_stage_id=0,
  subbatch_id=3,
  phase_id=PRT_DEBUG_PHASE_WORKER_DONE)
stage_worker_main at prt_runtime.c:4613
```

Thread state at that stop:

```text
Thread 3: stage_worker_main -> prt_debug_state_set_worker(... WORKER_DONE)
Thread 1: prt_runtime_run -> nanosleep/clock_nanosleep
```

This means execution had moved beyond segment0 and reached the segment1
worker-done path. It had not yet produced a segment2/local-stage1
GEMM_PREP/GEMM_RUN stop.

Breakpoint 10 was then disabled because even a false conditional breakpoint on
this function forces remote GDB to trap and evaluate every call. Lower-frequency
line breakpoints were placed at `stage_worker_main()` after-build, GEMM_RUN,
and GEMM-return boundaries with conditions on:

```gdb
ctx->action && ctx->action->segment_idx==2 && ctx->stage_id==1
```

The pre-call line `prt_runtime.c:4447` could not be armed with that condition
from the current optimized context:

```text
No symbol "ctx" in current context.
```

After continuing again, no segment2/stage1 boundary breakpoint was observed.
Heartbeat still did not advance, and the first Ctrl-C only echoed `^C`.
The second Ctrl-C disconnected the GDB session:

```text
Disconnected from target.
```

## Interpretation

Known-good progress in this round:

```text
gdbserver inferior entered
segment0/stage0 build_stage_task_desc returned rc=0
segment0/stage0 prt_gemm_conv_run returned rc=0
segment1/global_stage1/local_stage0/subbatch3 reached WORKER_DONE
```

The current negative frontier for this specific round is:

```text
after segment1/local_stage0 worker-done observation
before any observed segment2/local_stage1 after-build / GEMM_RUN / GEMM-return stop
```

This does not prove a regression before segment2. The breakpoint strategy still
had two limitations:

- The useful segment2 pre-build line breakpoint could not be armed with the
  desired `ctx` condition from the optimized current frame.
- Conditional software breakpoints still perturb remote execution because the
  target traps before the host evaluates the condition.

The run is therefore useful as a correction to the "program no longer enters"
hypothesis and as a GDB-method lesson, but it is not enough to locate the final
hardware wait PC.

## Post-Run Static Cross-Check

`PRT_DEBUG_PHASE_WORKER_DONE` is not the end of `stage_worker_main()`. The code
continues through:

```text
prt_runtime.c:4613  prt_debug_state_set_worker(... WORKER_DONE)
prt_runtime.c:4619  entry buffer cleanup / rotate
prt_runtime.c:4647  export buffer loop
prt_runtime.c:4662  PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN -> prt_process_c2()
```

The selected pipeline YAML shows segment1 is a single-stage segment:

```text
segment_idx=1
globalStageId=1
local_stage=0
entryTensorIdList=[0]
entryTensorTypeList=[DRAM]
exportTensorIdList=[3]
exportTensorTypeList=[DRAM]
```

Therefore the second `continue` after the segment1 worker-done stop could have
entered the segment1 post-compute export path before segment2 was ever reached.
This matches the older segment1 records:

- `20260508T075500Z_dummy8x8_sbus64_segment1_subbatch7_stall.md`
- `20260508T075800Z_dummy8x8_sbus64_gdb_frontier_export_dma_page15_timeout.md`

Those records narrowed a previous segment1/subbatch7 frontier to tensor-3
export sync / `prt_process_c2()` / DMA wait, and warned that dense GDB
breakpoints can create timeout artifacts. The current round reached subbatch3
instead of subbatch7, so it is not an exact reproduction, but it argues against
treating the missed segment2 breakpoint as proof that segment2 itself regressed.

## GDB Speed Lessons

- Avoid `next` / `step` across large runtime functions on this target. They can
  make a returning scalar path look stuck.
- Prefer return/boundary source-line breakpoints plus `continue`.
- Disable low-level breakpoints immediately after the path is ruled out.
- Avoid function-entry conditional breakpoints on hot helper functions such as
  `prt_debug_state_set_worker()`; false conditions still require a remote trap.
- Prefer lower-frequency source-line boundaries in `stage_worker_main()`, or
  use runtime marker filtering when `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`.
- `finish` is only appropriate for short callees already known to return.
- Keep `scheduler-locking off` while trying to let other worker/main threads
  make forward progress.
- Treat a failed late Ctrl-C as evidence. Do not wait indefinitely after the
  first interrupt only echoes `^C`.

## Cleanup

Artifacts were copied before teardown:

```text
tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/artifacts/uartlog
tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/artifacts/heartbeat.csv
tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/artifacts/runworkload_tmux_tail.txt
tmp/firesim-aws-f2/gdb-live/20260509-1341-sbus64-early-boundaries/artifacts/gdb_tunnel_tmux_tail.txt
```

The run farm was terminated with:

```text
firesim terminaterunfarm --forceterminate
  -c config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml
  -a config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml
  -r sims/firesim-staging/sample_config_build_recipes.yaml
```

Terminate log:

```text
sims/firesim/deploy/logs/2026-05-09--14-22-24-terminaterunfarm-45MCOO05URFDJCOC.log
```

`terminaterunfarm` selected and terminated `i-0f8a5229a111f752f`. The follow-up
active-F2 AWS query returned empty; the instance-specific query showed
`shutting-down`.

## Next Step

For the next F2 round, avoid the hot function-entry conditional breakpoint.
Start from a marker-enabled or lower-frequency line-breakpoint plan. The first
target should be the segment1 post-compute export boundary, then segment2 only
after that path is proven to return:

- stop at segment1/local_stage0 `WORKER_DONE` for the target subbatch and arm
  low-frequency boundaries around `prt_runtime.c:4647`, `prt_process_c2()`,
  `dma_copy_spm_pages_to_host_linux()`, and `dma_blocking_wait()`;
- once segment1 export returns, use runtime marker filtering for the segment2
  worker ladder if possible;
- otherwise stop at a coarse segment/action boundary and only then arm
  `stage_worker_main()` pre-build/after-build/GEMM_RUN lines for the selected
  worker;
- if pre-build cannot be conditionally armed on the source line, use a temporary
  unconditional stop while the selected worker is already current, then set
  scheduler locking only for short local inspection.
