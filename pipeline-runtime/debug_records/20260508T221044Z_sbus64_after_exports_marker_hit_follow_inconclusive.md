# 2026-05-08T22:10Z sbus64 dummy8x8 after-exports marker hit; source-line follow inconclusive

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Marker filter:
  - site `worker-after-exports-ready`
  - segment `2`
  - global stage `3`
  - local stage `1`
  - subbatch `0`
- Run host: `192.168.1.144`
- Post-marker GDB script: `pipeline-runtime/scripts/gdb/sbus64_segment2_stage1_after_exports_to_gemm_probe.gdb`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T221044Z-192_168_1_144-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Valid evidence

The after-exports marker hit:

```text
site_id=22
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=4
rc=0
aux0=1 export_count
aux1=1 shared_pair_count
line=4367
```

This confirms the previous stage-wait probe result: segment2/globalStage3/localStage1/subbatch0 reaches and passes `stage_wait_exports_ready()` with `rc=0`.

At the marker stop:

- Thread 2 was at `stage_worker_main()` around `prt_runtime.c:4360`.
- Thread 4 was still in `prt_pipebuf_wait_full(buf=0x96f48, idx=0)` at `prt_runtime.c:4315`.
- Thread 3 was still in the stage0 C1 load path:
  `prt_host_virt_to_phys()` -> `dma_copy_host_to_spm_pages_linux()` -> `prt_process_c1()` -> `stage_worker_main()`.

## Inconclusive part

After the marker hit, the post-marker script set a temporary source-line breakpoint at `prt_runtime.c:4409` to catch the next pre-build-stage-task point:

```text
Temporary breakpoint 2 at 0x313ce: file .../prt_runtime.c, line 4409.
```

It did not produce a follow-up stop before manual SIGINT, and GDB then reported:

```text
...sbus64_segment2_stage1_after_exports_to_gemm_probe.gdb:37:
Disconnected from target.
```

This should **not** be interpreted as a confirmed runtime hang between lines 4367 and 4409. It may be a source-line/optimization breakpoint issue, an interaction with batch GDB after the marker stop, or real control-flow delay. The run only conclusively proves that the after-exports marker is reachable.

## Updated frontier

Ruled out:

- Permanent failure to reach `worker-before-exports-ready`.
- Permanent failure inside `stage_wait_exports_ready()` for this stage/subbatch.
- Permanent failure to reach `worker-after-exports-ready`.

Still open:

- Whether the worker reaches `worker-before-build-stage-task`.
- Whether `build_stage_task_desc()` returns.
- Whether the worker reaches `worker-gemm-run`.
- Whether the main stall is from stage0 C1 fixed-load/DMA progress, stage1 compute, or stage2 waiting on tensor6 entry.

## Next probe

Use the marker site itself instead of a source-line follow from after-exports:

```text
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

Then collect the marker state and all-thread backtrace. If that marker hits, follow with a short build/gemm probe or switch marker to `worker-after-build-stage-task` / `worker-gemm-run`.

## Cleanup

The run farm was terminated. Local `runworkload`, GDB, and SSH tunnel remnants were cleaned up. Instance `i-062884d7283674228` was observed in `shutting-down`.
