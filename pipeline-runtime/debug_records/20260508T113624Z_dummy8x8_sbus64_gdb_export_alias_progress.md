# 2026-05-08T11:36:24Z dummy8x8 sbus64 GDB export-alias progress

## Context

- Hardware: AGFI `agfi-077451484fe3b63c3`, AFI `afi-07989ce9ce725a690`.
- Config: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Run host: `i-0c555be2bc1992b27`, private IP `192.168.1.103`.
- Result dir: `sims/firesim/deploy/results-workload/2026-05-08--10-46-44-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`.
- GDB log: `pipeline-runtime/debug_records/artifacts/20260508T113624Z_dummy8x8_sbus64_gdb_export_alias_progress/gdb-interactive.log`.
- Live artifacts: `pipeline-runtime/debug_records/artifacts/20260508T113624Z_dummy8x8_sbus64_gdb_export_alias_progress/`.

This round intentionally used direct interactive GDB commands instead of a helper script. The helper is not required for this phase; dynamic GDB is better while the frontier is still moving.

## Result

This was not a workload pass, but it moved the confirmed frontier forward.

Confirmed not stuck in these earlier regions:

- `stage_prepare_exec_views()`: fixed-load tensor `1000001` returned through the fixed-load end path.
- `prt_spm_bind_vpages_ctx()`: returned `rc=0`.
- `runtime_flush_stage_spm_xlate()` / manager0 SPM xlate flush: returned `rc=0`.
- `build_stage_conv_desc()` / worker task build: returned `rc=0`, `task.op_kind=PRT_STAGE_OP_CONV`, `tile_count=8`, `manager_ids=0..7`.
- `prt_gemm_conv_run()`: returned `rc=0`.

New export-alias evidence:

- Entered `sync_stage_export_aliases()` at `prt_runtime.c:2651` for `stage=0`, `tensor=2`, `manager=0`.
- Entered first alias target:
  - `target_kind="address"`
  - `target_seq=0`
  - `target_slot=3`
  - `dst_addr=0x3ff6dd4400`
  - `src_size=65536`
  - `pages->size=64`
- In `dma_copy_spm_pages_to_host_linux()`:
  - First direct V2P returned `rc=0`.
  - First destination physical address was `dst_pa=0x104a93400`.
  - First page submit/wait returned `rc=0`.
  - Interrupt sample during the first export showed forward progress at `i=35`, `remaining=28672`, `rc=0`.
  - Conditional breakpoint later hit loop boundary with `i=63`, `remaining=0`, `rc=0`, proving the 64 KiB first alias export completed.
  - `dma_batch_scope_release()` returned.
  - `copy_tensor_pages_to_model_alias_target(... target_kind="address" ...)` returned `0`.
- Entered second alias target:
  - `target_kind="address2"`
  - `target_seq=1`
  - `target_slot=3`
  - `dst_addr=0x3ff75f6400`
  - `src_size=65536`
  - `seen_count=2`
  - Returned `rc=0`.
- `copy_tensor_pages_to_model_aliases()` reached its cleanup path at `prt_runtime.c:2081`.
- Returned to `sync_stage_export_aliases()` at `prt_runtime.c:2663` with `rc=0` and `emit_deep=0`.

The previous suspected frontier, tensor2 export DMA, is therefore no longer the best explanation for the current stall. Tensor2's two model alias targets both completed in this GDB run.

## Negative Observation

After continuing from `sync_stage_export_aliases()` with a temporary breakpoint on the function tail (`prt_runtime.c:2707`), the breakpoint did not hit quickly. A later Ctrl-C did not stop cleanly on the first attempt; the second Ctrl-C disconnected GDB from the target:

```text
Disconnected from target.
```

The FireSim simulator was still running afterward, so this is a GDB/debug-control limitation in the current session, not proof that the guest completed.

## Updated Frontier

The current frontier is after the first tensor2 alias synchronization returned to `sync_stage_export_aliases()` at `prt_runtime.c:2663`.

Most likely next regions:

- remaining iterations of the `stage->num_export` loop in `sync_stage_export_aliases()`;
- export audit/post-processing if enabled, although this run showed `emit_deep=0` and `emit_audit` was optimized out at the sampled line;
- return from `sync_stage_export_aliases()` back to `stage_worker_main()` and the following worker progress update;
- a later stage or subbatch, if stage0 export returns successfully.

## Next GDB Round

Use a fresh gdbserver run. Avoid relying on Ctrl-C after a long `continue`; pre-place later breakpoints instead.

Suggested breakpoints:

```gdb
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2550
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2566
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2657
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2663
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2707
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4571
```

At each hit, record:

```gdb
bt 8
info locals
print rc
print stage_id
print global_stage_id
print subbatch_id
```

If GDB can still evaluate `stage`, also inspect:

```gdb
print stage->num_export
print stage->exports[0].tensor_id
print stage->exports[1].tensor_id
```

Do not use DMA doneflag as proof. Use return from `prt_dma_wait()`, `dma_submit_wait_annotated_scoped()`, and RR scope release evidence.
