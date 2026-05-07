# 20260507T142900Z dummy8x8 sbus64 token546 return with detach timeout

## Scope

- Hardware: AGFI `agfi-077451484fe3b63c3`, AFI `afi-07989ce9ce725a690`
- Shape: dummy8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-0a2ec5cd15450ed88`, private IP `192.168.1.85`
- Guest endpoint: `172.16.0.2:2345`
- Artifact directory: `pipeline-runtime/debug_records/artifacts/20260507T142400Z_dummy8x8_sbus64_gdb_pathtrace_token546_return_detach_timeout/`

This run kept the current stage-assigned DMA manager policy. No DMA manager
selection change was made.

## Commands

The run farm had already been launched and refreshed with the current image:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.85
```

GDB path trace:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_POST_HIT_MODE=path_trace \
PRT_GDB_FRONTIER_CONDITION='tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546' \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_PATH_TRACE_TIMEOUT=180 \
PRT_GDB_PATH_TRACE_MAX_STOPS=9 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.85 172.16.0.2:2345 32345
```

Cleanup:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` reported success. EC2 showed `i-0a2ec5cd15450ed88` in
`shutting-down` immediately after termination.

## Result

GDB connected successfully as the first TCP client to `gdbserver --once`.
The exact token546 condition was hit in thread 2:

- `tok->id = 546`
- `stage_idx = 0`
- `tensor_id = 2`
- `rr_manager_id = 0`
- `rr_scope_valid = 1`
- `rr_scope_external = 1`
- `hw_done_flag = 1`
- `done = 1`
- `status = 0`
- `completion_flag = 0x5d000`, memory value `0x00000001`
- `src = 0x40702c00`
- `dst = 0x1044f6000`
- `done_pa = 0x10235a000`
- `bytes = 1024`

The backtrace at return was:

```text
dma_blocking_wait
prt_dma_wait
dma_submit_wait_annotated_scoped
dma_copy_spm_pages_to_host_linux
prt_dma_copy_spm_pages_to_dram_prefix
copy_tensor_pages_to_model_alias_target(target=address2, target_seq=1)
copy_tensor_pages_to_model_aliases
sync_stage_export_aliases
stage_worker_main
```

The helper reached `dma_blocking_wait` line 3788 (`return PRT_OK`) with the
same token. Therefore the old suspected token546 card point is not reproduced
as a token546 failure in this software/hardware state.

## Path Evidence

The path trace reached the late token546 DMA wait path:

- `dma_blocking_wait` path stop at `before_shared_fence`
- `dma_token_fence_scope`
- `prt_rr_fence_scope`
- return from the shared fence path
- after-release/complete path
- `dma_token_complete`
- `dma_blocking_wait` return at `prt_dma.c:3788`

The breadcrumb file captured immediately after the GDB run still had its last
slot at `dma_wait_before_shared_fence`:

```text
frontier=dma/dma_wait_before_shared_fence seg=0 gstage=0 lstage=0 sb=1 mgr=0 tensor=2 page=31 tok=546
```

This is stale relative to the GDB transcript because the final GDB observations
show `tok->done=1`, `tok->status=0`, `hw_done_flag=1`, and PC at
`dma_blocking_wait` return.

The guest progress log also shows stage0/subbatch1 reached the same
`target=address2` export and the first chunk completed with `rc=0`.

## Detach Issue

After disabling and deleting breakpoints, the helper sent `detach`. GDB printed:

```text
Detaching from program: ..., process 235
timeout waiting for gdb prompt
```

The helper exited with `expect_rc=3`. The guest-side runner status still showed
the inferior in `State: t (tracing stop)` with `TracerPid: 222`. This run was
therefore polluted after the successful token546 path trace and must not be used
as evidence about natural pipeline-runtime progress beyond token546.

Local GDB, expect, and SSH tunnel processes were gone after the helper exited.
The run farm was terminated after artifact capture.

## Conclusions

- The two confirmed software fixes are compatible with reaching and completing
  the historical token546 path.
- Token546 is not the current runtime card point.
- The next real card point is after token546, likely in later pages/tokens of
  the same stage0/subbatch1 tensor2 export path or later pipeline progress.
- The current GDB helper cleanup remains a separate issue: `detach` can time
  out and leave the gdbserver inferior ptrace-stopped even after successful
  breakpoint evidence.

## Recommended Next Probe

Use a fresh run. Do not reuse this stopped inferior.

For the next localization pass, avoid exact token546. Better options:

1. Use a later token/page frontier, for example token `>= 547` or a condition on
   `stage_idx == 0 && tensor_id == 2 && tok->id >= 547`, then interrupt or path
   trace a small number of stops.
2. Use a no-business-breakpoint stack sample run and Ctrl-C after natural
   progress slows, so the first stack reflects the real current card point
   rather than the historical token546 window.
3. If keeping path trace, add a helper mode that quits by terminating/restarting
   the run after evidence capture instead of relying on `detach` to resume a
   production run.

