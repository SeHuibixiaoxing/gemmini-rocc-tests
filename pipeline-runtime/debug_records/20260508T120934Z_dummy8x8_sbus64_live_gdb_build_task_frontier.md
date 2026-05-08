# 2026-05-08T12:09:34Z dummy8x8 sbus64 live GDB build-task frontier

## Context

- Hardware: AGFI `agfi-077451484fe3b63c3`, AFI `afi-07989ce9ce725a690`.
- Config: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Run host: `i-0d8e687da3efa5001`, private IP `192.168.1.83`, public IP `34.219.120.235`.
- Result dir: `sims/firesim/deploy/results-workload/2026-05-08--11-50-00-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`.
- GDB tunnel: local `32347` to guest `172.16.0.2:2345` through run host `192.168.1.83`.
- Artifacts: `pipeline-runtime/debug_records/artifacts/20260508T120934Z_dummy8x8_sbus64_live_gdb_build_task_frontier/`.

This round used direct live GDB instead of a helper script. The helper is not required for this stage; live GDB is preferable while the next useful breakpoint is being chosen from the current stop state.

## Commands

GDB was attached with:

```gdb
target remote :32347
break prt_gdb_marker_stop
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4571 if segment_idx == 0 && ctx->stage_id == 0 && progress_sbatch == 0
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4582 if segment_idx == 0 && ctx->stage_id == 0 && progress_sbatch == 0
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4617 if segment_idx == 0 && ctx->stage_id == 0 && progress_sbatch == 0
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4633 if segment_idx == 0 && ctx->stage_id == 0 && progress_sbatch == 0
break prt_process_c2 if buf && buf->stage_idx == 0 && buf->tensor_id == 2 && idx == 0
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:485 if sparse_export_probe
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:491 if sparse_export_probe
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:514 if sparse_export_probe
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4682 if segment_idx == 0 && ctx->stage_id == 0 && progress_sbatch == 0
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:5386 if seg_idx == 0
continue
```

## Result

This was not a workload pass. It confirmed that the new run can again connect to `gdbserver --once` with GDB as the first TCP client and can stop on debug markers, but it did not reach the previously confirmed export-alias frontier before the GDB session was lost.

Confirmed observations:

- Guest reached `[gdbserver] phase=listening` at `172.16.0.2:2345`, then `[gdbserver] phase=inferior` for PID `229`.
- GDB connected through the SSH tunnel and stopped in the dynamic loader.
- The source breakpoint at the sink monitor (`prt_runtime.c:5386`) fired too early. Its condition referenced optimized-out `seg_idx`, so GDB stopped with `Error in testing condition for breakpoint 11: value has been optimized out`.
- At that early sink-loop stop:
  - `rt->fatal_error = 0`
  - `rt->stop_requested = 0`
  - `sink_count = 1`
  - thread 1 was in `prt_runtime_run()`
  - thread 2 was in `sched_setaffinity()` through `stage_bind_current_thread()`, which is a transient stop caused by whole-process GDB suspension, not standalone evidence of an affinity deadlock.
- The guest-side marker then stopped at `PRT_GDB_MARKER_SITE_WORKER_BEFORE_BUILD_STAGE_TASK`:
  - `site_id = 23`
  - `segment_idx = 0`
  - `global_stage_id = 0`
  - `local_stage_id = 0`
  - `subbatch_id = 0`
  - `manager_id = 0`
  - `aux0 = 8` (`task.tile_count`)
  - `aux1 = 0` (`task.manager_ids[0]`)
  - `line = 4416`
- `g_prt_debug_state` at that stop showed phase `PRT_DEBUG_PHASE_GEMM_PREP`, manager `0`, opcode `2`, and RR config `0`.

## Negative Observation

After continuing from `before-build-stage-task`, no later source breakpoint fired before manual interrupt. The configured source breakpoints were mostly placed in the later export-sync / C2 retire path, so this round does not prove `build_stage_task_desc()` itself is stuck.

The next manual interrupt did not stop cleanly. A second Ctrl-C produced:

```text
Disconnected from target.
```

This reproduces the current control limitation: long-running target execution plus Ctrl-C can break the remote GDB session for this pipeline workload. It is useful for choosing future breakpoint strategy, but not valid evidence that the guest program completed or failed.

## Updated Frontier

The conservative frontier for this run is:

- reached worker stage0/subbatch0 before `build_stage_task_desc()`;
- did not observe `build_stage_task_desc()` return in this session;
- did not observe `prt_gemm_*`, `sync_stage_export_aliases()`, `prt_process_c2()`, worker done, or sink progress in this session.

Because the previous run already proved tensor2 export-alias DMA returned successfully, this run is best interpreted as a breakpoint coverage/control issue, not as a regression that invalidates the previous export-alias evidence.

## Next GDB Round

Use a fresh `gdbserver --once` run. Do not rely on Ctrl-C for stack sampling. Pre-place denser breakpoints around the currently under-instrumented section:

```gdb
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4409
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4417
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4423
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4431
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4450
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4560
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4571
```

Keep the sink-loop breakpoint disabled unless it is unconditional and manually inspected after a known safe stop. Avoid conditions on optimized-out loop locals such as `seg_idx` at `prt_runtime.c:5386`.

Do not use DMA doneflag as proof. Completion evidence must be a blocking wait return, `hw_dma_fence()`, `prt_dma_wait()` / `dma_submit_wait_annotated_scoped()` return, or RR scope release.
