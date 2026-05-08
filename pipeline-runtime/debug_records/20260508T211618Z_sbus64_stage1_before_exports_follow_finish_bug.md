# 2026-05-08T21:16Z sbus64 dummy8x8 stage1 before-exports marker hit; follow script over-finished

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.30`
- GDB transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T211618Z-192_168_1_30-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Valid evidence

The run again hit the `worker-before-exports-ready` marker for:

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

At the marker stop, all-thread backtrace showed:

- marker thread at `stage_worker_main()` around `prt_runtime.c:4345`;
- another worker in `prt_pipebuf_wait_full(buf=0x96f48, idx=0)` at `prt_runtime.c:4315`;
- another worker in the stage0 C1 path, inside `prt_host_virt_to_phys()` called from `dma_copy_host_to_spm_pages_linux()` and `prt_process_c1()`.

This confirms the moving-frontier pattern: stage1 can reach the pre-export-ready boundary while another worker is still doing early C1 load work and another waits for a downstream entry.

## Script problem

The revised follow script still had a control-flow bug:

```gdb
finish
finish
finish
```

The first `finish` returned to `stage_worker_main()` at line 4353:

```text
stage_worker_main (...) at prt_runtime.c:4353
4353    rc = stage_wait_exports_ready(...)
```

The second `finish` then attempted to finish `stage_worker_main()` itself. That made the batch script wait for the entire worker thread to return rather than setting the intended breakpoint at line 4360. After manual SIGINT, GDB reported:

```text
Error in sourced command file:
...sbus64_segment2_stage1_follow_before_exports_ready.gdb:28:
Disconnected from target.
```

Therefore this run does **not** prove that `stage_wait_exports_ready()` internally hangs. It proves the marker boundary is reproducible, and it exposes that the follow script was still wrong.

## Follow-up correction

The follow script has been changed again to avoid `finish` entirely:

- capture `$_thread` at the marker;
- set a thread-filtered temporary breakpoint on `stage_wait_exports_ready`;
- then step through the export-kind dispatch and C4 drain path using source-line breakpoints;
- keep safe GDB Python printing so optimized-out values are recorded without aborting the script.

The next run should determine whether the worker:

- enters `stage_wait_exports_ready`;
- sees the export buffer as C4/shared or some other kind;
- calls `prt_process_c4`;
- loops on `export-c4-drain`;
- or returns to line 4360.

## Cleanup

The run farm was terminated. Local stale `runworkload`, GDB, and SSH tunnel processes were cleaned up. Instance `i-013cf24b9a91c452b` was observed in `shutting-down`.
