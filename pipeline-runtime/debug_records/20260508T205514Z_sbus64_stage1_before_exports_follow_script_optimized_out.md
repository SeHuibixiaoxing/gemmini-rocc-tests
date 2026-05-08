# 2026-05-08T20:55Z sbus64 dummy8x8 stage1 before-exports marker hit; follow script aborted on optimized-out args

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.111`
- GDB transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T205514Z-192_168_1_111-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Guest environment

The run used the low-disturbance GDB-only profile:

- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=uart`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
- Marker filter:
  - site `worker-before-exports-ready`
  - segment `2`
  - global stage `3`
  - local stage `1`
  - subbatch `0`

`gdbserver` was not probed with `nc`/`curl`; GDB was attached only after UART printed `[gdbserver] phase=listening`.

## Valid evidence

The marker hit:

```text
site_id=21
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=4
rc=0
aux0=1 export_count
aux1=1 shared_pair_count
line=4352
```

This confirms the segment2/stage1 worker completed its entry waits and reached the pre-export-readiness boundary in this run.

At the same stop, `thread apply all bt` showed:

- One thread in `stage_worker_main` for the marker stop at `prt_runtime.c:4345`.
- Another worker blocked in `prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000)` at `prt_runtime.c:4315`. This is the same stage2 entry wait shape observed in earlier records.
- Another worker was still inside stage0 Gemmini fallback compute:
  `sp_tiled_matmul_os -> tiled_matmul -> ... -> gemm_blocking_conv_run -> stage_worker_main`.

The run therefore does not prove that stage1 export propagation is broken. It proves the current frontier includes:

1. stage1 has reached before-export-readiness for segment2/globalStage3/localStage1/subbatch0;
2. stage2 is still waiting for its entry full at the same sampled moment;
3. another stage may still be computing, so scheduler interleaving remains relevant.

## Invalid / inconclusive evidence

The post-marker follow script then failed for a GDB-script reason:

```text
break stage_wait_exports_ready if stage_id == 1 && subbatch == 0
Error in testing condition for breakpoint 2:
value has been optimized out
...
break build_stage_task_desc if stage_id == 1
...
value has been optimized out
```

The optimized Linux build appears to inline or optimize the relevant helper arguments. The current script cannot safely use function-argument breakpoint conditions or direct `print stage_id` / `print task->...` commands at those call sites.

## Immediate correction

Replace the post-marker script with a thread-local, call-site-oriented script:

- capture `$_thread` at the marker stop;
- `finish` out of the marker helper stack back to `stage_worker_main`;
- use thread-filtered source-line breakpoints after the relevant calls;
- use safe Python wrappers around GDB `print` commands so `<optimized out>` values do not abort the entire batch script;
- only inspect function arguments where they are known to be live.

The next run should determine whether the segment2/stage1 worker:

- returns from `stage_wait_exports_ready`;
- builds the task descriptor;
- enters and returns from `prt_gemm_conv_run`;
- reaches export sync;
- calls `prt_process_c4` for tensor6 and sets the downstream stage2 entry full bit.

## Cleanup

The F2 run farm termination command was issued after GDB exited. Local stale `runworkload`, GDB, and SSH tunnel processes were killed. The instance `i-0976d5c2d3fd62844` was observed in `shutting-down`.
