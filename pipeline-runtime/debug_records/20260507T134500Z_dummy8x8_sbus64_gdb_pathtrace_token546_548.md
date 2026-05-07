# 20260507T134500Z dummy8x8 sbus64 gdb path trace token546-548

## Scope

- Hardware: AGFI `agfi-077451484fe3b63c3`, AFI `afi-07989ce9ce725a690`
- Config: dummy8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- Run host: `i-059b87d5cf6866d94`, private IP `192.168.1.88`
- Guest endpoint: `172.16.0.2:2345`
- Artifact directory: `pipeline-runtime/debug_records/artifacts/20260507T134000Z_dummy8x8_sbus64_gdb_pathtrace_token546_548/`

## Commands

Software/image/deploy path:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh marshal-build
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh marshal-install
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.88
```

GDB path trace:

```bash
PRT_GDB_POST_HIT_MODE=path_trace \
PRT_GDB_FRONTIER_CONDITION='tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546' \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_PATH_TRACE_TIMEOUT=180 \
PRT_GDB_PATH_TRACE_MAX_STOPS=20 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.88 172.16.0.2:2345 32345
```

Cleanup:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

## Result

The two confirmed software fixes were present in the image. `marshal-build`, `marshal-install`, `launch`, and `infrasetup` all passed. `infrasetup` flashed `agfi-077451484fe3b63c3` and passed FireSim driver readiness preflight.

The gdbserver workload booted Linux, initialized IceNet, mounted `iceblk`, brought up networking, and printed:

- `[gdbserver] phase=prelaunch ... guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345`
- `[gdbserver] phase=listening ... pid=222`
- `[gdbserver] phase=inferior ... pid=235`

GDB connected successfully as the first TCP client. This validates the cfg32 NIC gdbserver attach path on this bitstream/software image for this run.

The exact token546 frontier was hit:

- `tok->id = 546`
- `stage_idx = 0`
- `tensor_id = 2`
- `rr_manager_id = 0`
- `rr_scope_valid = 1`
- `rr_scope_external = 1`
- `completion_flag = 0x5d000`
- `x/wx completion_flag = 0x00000001`
- `src = 0x40702c00`
- `dst = 0x103af7000`
- `done_pa = 0x10235a000`
- `bytes = 1024`
- call stack: `dma_blocking_wait -> prt_dma_wait -> dma_submit_wait_annotated_scoped -> dma_copy_spm_pages_to_host_linux -> prt_dma_copy_spm_pages_to_dram_prefix -> copy_tensor_pages_to_model_alias_target(target=address2, target_seq=1) -> copy_tensor_pages_to_model_aliases -> sync_stage_export_aliases -> stage_worker_main`

## Path Trace Evidence

For token546, the path trace reached these breakpoints in order:

1. `before_poll_gate` at `prt_dma.c:3570`
2. `poll_done` location, resolved by GDB to `prt_dma.c:2435`
3. `before_shared_fence` at `prt_dma.c:3666`
4. `dma_token_fence_scope`
5. `prt_rr_fence_scope`
6. `after_shared_fence` at `prt_dma.c:3685`
7. `after_release` at `prt_dma.c:3723`
8. `dma_token_complete`
9. `return` at `prt_dma.c:3788`

The same complete path was then observed for token547.

The trace then entered token548 because `PRT_GDB_PATH_TRACE_MAX_STOPS=20`. The final stop was at the `poll_done` source location for token548:

- `tok->id = 548`
- `stage_idx = 0`
- `tensor_id = 2`
- `rr_manager_id = 0`
- `rr_scope_valid = 1`
- `rr_scope_external = 1`
- `tok->hw_done_flag = 0`
- `tok->completion_flag = 0x5d000`
- `x/wx tok->completion_flag = 0x00000001`
- `src = 0x40103000`
- `dst = 0x103af7800`
- `done_pa = 0x10235a000`
- `bytes = 1024`
- PC `0x1605c`, instruction `sw a5,40(s11)`

Important interpretation: token548 was stopped after loading the done flag and before storing it back into `tok->hw_done_flag`. Since memory already contained `0x1`, the GDB snapshot does not prove token548 DMA was stuck. It proves the debugger stopped exactly in the completion-flag refresh/writeback window.

## GDB Cleanup Issue

The helper attempted `detach`, but GDB timed out waiting for the prompt:

```text
Detaching from program: ... process 235
timeout waiting for gdb prompt
```

After the helper exited, local GDB/SSH-tunnel processes were gone, but the guest-side snapshot still showed:

- gdbserver pid `222`
- inferior pid `235`
- inferior `State: t (tracing stop)`
- `TracerPid: 222`

That means this run was polluted after the GDB trace by a failed detach/ptrace stop. It should not be used as evidence that pipeline runtime itself remained hung after token548. The FireSim run farm was terminated after artifact capture to avoid leaving a stopped target running.

## Current Conclusions

- The earlier token546 card point is no longer reproduced as a token546 failure in this software state.
- Token546 completed through done-flag polling, shared `rr_fence_scope`, release, `dma_token_complete`, and return.
- Token547 also completed through the same path.
- The observed token548 stop is a debugger artifact at the done-flag writeback boundary, not yet a confirmed hardware/runtime hang.
- The GDB helper should not path trace past the token of interest with a large fixed stop count. For token-local tracing, use a stop count of 9 for the current optimized ELF path, or teach the helper to stop/delete breakpoints after the target token returns.
- The helper also needs a safer cleanup path: delete/disable path breakpoints before detach, and treat detach timeout as a known state that requires terminating/restarting the run instead of assuming the inferior resumed cleanly.

## Follow-Up

Recommended next run:

1. Rerun the gdbserver workload on the same AGFI/software image.
2. Use `PRT_GDB_PATH_TRACE_MAX_STOPS=9` for exact token546 so GDB stops at token546 return and does not enter token547/548.
3. Add or use a helper mode that deletes all breakpoints before detach.
4. If the goal is to locate the next real runtime card point after token546, rerun with exact token548 or a later token and a token-local stop budget, but do not interpret a stopped poll/writeback window as a natural runtime hang.

