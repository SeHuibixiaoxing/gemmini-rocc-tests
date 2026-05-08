# 20260508T035930Z dummy8x8 sbus64 page46 pass, page47 hw-fence frontier

## Context

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-07bf86a7b0d229c8f`, private IP `192.168.1.135`
- Workload results dir: `sims/firesim/deploy/results-workload/2026-05-08--03-39-30-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Artifact bundle: `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260508T035930Z_dummy8x8_sbus64_page46_pass_page47_hwfence_frontier/`

## GDB Test

Connected as the first TCP client to guest gdbserver at `172.16.0.2:2345` through the run-host SSH tunnel.

Helper:

```sh
PRT_GDB_FRONTIER_FUNC=dma_submit_wait_annotated_scoped
PRT_GDB_FRONTIER_CONDITION='stage_idx == 0 && tensor_id == 2 && debug_page_idx == 46'
PRT_GDB_POST_HIT_MODE=path_trace
PRT_GDB_PATH_TRACE_MAX_STOPS=14
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.135 172.16.0.2:2345 32345
```

The helper exited `PASS`.

## Observations

1. The conditional breakpoint on `dma_submit_wait_annotated_scoped()` hit at:
   - segment `0`, subbatch `0`
   - stage `0`, tensor `2`
   - export page `46`
   - caller stack:
     `sync_stage_export_aliases -> copy_tensor_pages_to_model_alias_target -> prt_dma_copy_spm_pages_to_dram_prefix -> dma_copy_spm_pages_to_host_linux -> dma_submit_wait_annotated_scoped`

2. Page 46 did not hang.
   The path trace crossed:
   - `prt_dma_submit`
   - `dma_blocking_wait`
   - `hw_dma_fence`
   - completion flag refresh
   - `dma_token_fence_scope`
   - after shared fence / release
   - `dma_gdb_marker_wait_return(rc=0)`
   - return through `dma_submit_wait_annotated_scoped`

3. The true next live frontier moved to page 47:
   - GDB stopped at `hw_dma_fence()` for `debug_page_idx=47`
   - PC: `0x161c4 <dma_blocking_wait+348>`
   - stack still in `dma_copy_spm_pages_to_host_linux`, tensor `2`, page `47`
   - breadcrumb after capture:
     `last_kind=dma last_phase=dma_program_post_src`, page `47`, token `177`

4. Sparse log after detach only advanced by sink-progress lines:
   - `segment=0 sink-progress=0/8 elapsed_ms=4078`
   - `segment=0 sink-progress=0/8 elapsed_ms=5190`
   - `segment=0 sink-progress=0/8 elapsed_ms=6208`
   No export completion line was observed after page 47.

5. doneflag remains invalid as completion evidence.
   On page 46 the completion flag had already become `1`, and the valid pass criterion was still the blocking `hw_dma_fence()` plus shared fence/release path. This run does not change the known constraint that doneflag must not be used as a completion decision.

## Interpretation

The older page46 suspicion is retired for this hardware/software state. Page46 completed with `hw_dma_fence()` and shared ReRoCC fence/release. The current reproducible frontier is later: page47 export DMA after programming source, at or inside `hw_dma_fence()`.

Because the helper detached while stopped at the page47 `hw_dma_fence()` breakpoint and no later breadcrumb/log appeared, the next run should target page47 directly and distinguish:

- a real hang inside the DMA fence instruction/path;
- an artifact caused by detaching at an active `hw_dma_fence()` breakpoint;
- a later stall immediately after the fence that is not represented in the current breadcrumb capture.

## Next Probe

Use a fresh gdbserver workload. Set the first conditional breakpoint directly on:

```gdb
break dma_submit_wait_annotated_scoped if stage_idx == 0 && tensor_id == 2 && debug_page_idx == 47
```

Then trace only page47 with conditional breakpoints or marker-gated stops:

- submit return after token setup
- `dma_blocking_wait`
- before `hw_dma_fence`
- after `hw_dma_fence`
- before shared fence
- after shared fence
- after release
- `dma_submit_wait_annotated_scoped` return

Do not stop at a fixed max-stop count while the PC is still on `hw_dma_fence()` unless a final `continue` or clean detach-resume is explicitly verified by a later breadcrumb.

## Cleanup

After capturing artifacts, `terminaterunfarm` was launched for the run config. At record time AWS had moved `i-07bf86a7b0d229c8f` to `shutting-down`; follow-up verification should confirm no F2 instances remain.
