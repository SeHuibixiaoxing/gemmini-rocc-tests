# 2026-05-08 07:58Z dummy8x8 sbus64 gdb frontier: export DMA narrowed

## Test target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-01b9ed3450e5d0aeb`, private IP `192.168.1.98`
- GDB helper: `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment1_subbatch7_frontier.sh`
- Artifact directory: `pipeline-runtime/debug_records/artifacts/20260508T075800Z_dummy8x8_sbus64_gdb_frontier_export_dma_page15_timeout/`

## Command

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=1800 \
PRT_GDB_FRONTIER_TIMEOUT=900 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment1_subbatch7_frontier.sh \
  192.168.1.98 172.16.0.2:2345 32345
```

The helper did not probe the guest TCP port with `nc`, `curl`, or `telnet`; GDB was the first client to `gdbserver --once`.

## Result

The helper connected to `gdbserver`, hit the configured `worker-gemm-run` marker for segment 1, global stage 1, local stage 0, subbatch 7, and installed the frontier breakpoints.

Important observations:

- `prt_gdb_marker_stop` was hit at `prt_runtime.c:4513`, with `aux0=1`, `aux1=8`, matching op kind and tile count.
- All 8 manager compute paths for the target subbatch reached `conv_call_for_manager_sync_strided`, `prt_run_pointwise_matmul_fallback_strided_impl`, `tiled_matmul_nn_stride_auto`, the pointwise subcall return, `prt_rr_fence_scope`, and the pointwise release breakpoint.
- The worker reached `prt_runtime.c:4515` after `prt_gemm_conv_run`.
- The worker reached `prt_runtime.c:4560`, then `sync_stage_export_aliases`.
- Therefore this run excludes the earlier suspicion that the target subbatch stalls inside the per-manager pointwise fallback itself.
- The run entered export sync and started copying tensor `3` through `copy_tensor_pages_to_model_alias_target -> prt_dma_copy_spm_pages_to_dram_prefix -> dma_copy_spm_pages_to_host_linux`.
- Under dense GDB breakpoints, export DMA pages `0..14` each hit submit, wait, and `dma_gdb_marker_wait_return rc=0`.
- The helper timeout fired while stopped at the `prt_dma_submit` entry breakpoint for `debug_page_idx=15`. This is an observer timeout, not direct proof that page 15 naturally deadlocks.

The guest runtime log still stopped at segment 1, subbatch 7 after the first opcode-2 acquire in export sync. The GDB transcript is more precise here because normal progress logging does not print every tensor-3 export page unless the focused probes are enabled.

## Caveats

- The frontier helper used too many always-on breakpoints. It materially slowed execution and eventually timed out while stopped at a software breakpoint. The page-15 stop is therefore a helper artifact.
- Breakpoint 11 used a source-level condition on optimized arguments and produced `value has been optimized out`; it captured some opcode-3 acquire calls as noise.
- One line breakpoint labelled `conv_call_for_manager_sync_strided return line 2593` resolved to `resadd_issue_scoped`, so this helper should be tightened before reuse.
- After Ctrl-C, detach left the run in an unhelpful state for further GDB because `gdbserver --once` had already been consumed. The F2 run farm was terminated after artifacts were captured.

## Current narrowed frontier

The most likely live stall region is no longer the per-manager pointwise compute path. It is the export-sync copy of tensor `3` for segment 1, stage 0, subbatch 7:

```text
stage_worker_main
  -> sync_stage_export_aliases
    -> copy_tensor_pages_to_model_aliases(tensor_id=3)
      -> copy_tensor_pages_to_model_alias_target(target_seq=0, target=address, slot=3)
        -> prt_dma_copy_spm_pages_to_dram_prefix
          -> dma_copy_spm_pages_to_host_linux
            -> dma_submit_wait_annotated_scoped
              -> prt_dma_submit / dma_blocking_wait / dma_gdb_marker_wait_return
```

Next debugging should reduce breakpoint density and target only:

- `export-sync-tensor` marker for segment 1, stage 0, subbatch 7, tensor 3.
- `dma-export-page-submit-begin/end` or `dma-wait-return` markers for tensor 3 around pages 12..20.
- `prt_rr_fence_scope` only when `scope->opcode_id == 2 && scope->manager_id == 0 && g_prt_debug_state.segment_idx == 1 && g_prt_debug_state.subbatch_id == 7`.

The next software experiment should also evaluate whether the per-page external RR shared fence inside `dma_blocking_wait` is required after `hw_dma_fence()`. The current path performs both a global DMA fence and a ReRoCC scope fence for each page when `tok->rr_scope_external=1`.
