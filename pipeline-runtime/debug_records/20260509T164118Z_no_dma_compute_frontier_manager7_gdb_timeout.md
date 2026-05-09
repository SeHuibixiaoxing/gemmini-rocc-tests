# 20260509T164118Z - no-DMA compute frontier reached manager 7 under GDB

## Goal

Continue the requested no-DMA compute bisection with `gdbserver`, starting
after the `worker-gemm-run` marker for:

```text
segment=1 global_stage=1 local_stage=0 subbatch=3
```

The previous no-DMA GDB records had already moved the frontier past artifact
reading, task construction, stage SPM xlate flush, `rr_release(cfg31)`,
release readback, `RROPC3` restore, and `prt_gemmini_spm_xlate_flush()`.
This run split the compute path through manager issue, pointwise matmul,
fence, drain, and release.

## Configuration

- Date: 2026-05-09 UTC
- AGFI/AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Run host: `i-0bdeaf782e11e2961`, private IP `192.168.1.27`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Build recipes:
  `sims/firesim-staging/sample_config_build_recipes.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- RISC-V runtime SHA256:
  `ed592ca0530050c8bec12c4dbecf47879989f63a91286cb6ae92f336a3479850`

Important guest env:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
```

## Commands

The run was launched through the existing pairdummy cfg32 gdbserver workflow.
After UART showed gdbserver listening, GDB was the first TCP client:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_compute_frontier.sh \
  192.168.1.27 172.16.0.2:2345 32345
```

No `nc`, `curl`, `telnet`, or port scan touched guest
`172.16.0.2:2345`.

## Artifacts

Artifacts were copied to:

```text
pipeline-runtime/debug_records/artifacts/20260509T164118Z_no_dma_compute_frontier_manager7_gdb_timeout/
```

Key files:

- `gdb/pairdummy-cfg32-marker-frontier.expect.log`
- `gdb/frontier.gdb`
- `gdb/static-neigh.log`
- `local/runworkload.pane.log`
- `local/runworkload-manager.log`
- `local/elf.sha256`
- `runhost/uartlog.live`
- `runhost/heartbeat.live.csv`
- `runhost/sim-run.live.sh`
- `configs/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- `configs/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- `configs/sample_config_build_recipes.yaml`

## Evidence

The marker hit the intended no-DMA compute window:

```text
site_id = 5
segment_idx = 1
global_stage_id = 1
local_stage_id = 0
subbatch_id = 3
line = 4543
```

The run then progressed through the compute path. The transcript shows manager
0 through manager 6 reaching pointwise matmul/fence/drain/release milestones.
Manager 5 completed release readback and entered manager 6. Manager 6 reached
`tiled_matmul_nn_stride_auto` return and entered the second
`prt_rr_fence_scope` in `flush_scope_after_drain`.

At frontier timeout, the selected thread was already on manager 7:

```text
#0 prt_run_pointwise_matmul_fallback_strided_impl(
     stage_id=0, manager_id=7, conv=0x3ff6d76ea0,
     tiled_type=WS, in_stride=256, weight_stride=256, out_stride=256,
     emit_logs=1, scope=0x3ff6d76cc8)
   at pipeline-runtime/src/prt_gemmini_adapter.c:1029

#1 prt_run_pointwise_matmul_fallback_strided_scoped(...)
#2 conv_call_for_manager_sync_strided(...)
```

The timeout debug state was:

```text
segment_idx = 1
global_stage_id = 1
local_stage_id = 0
subbatch_id = 3
phase_id = 8
manager_id = 7
opcode_id = 3
rr_stage_id = 0
rr_manager_id = 7
rr_opcode_id = 3
rr_cfg_id = 1
last_rc = 0
event_seq = 94
```

This proves the run reached post-read compute work. It is not evidence that
no-DMA affected artifact/YAML/rootfs reading.

## Interpretation

This run does not prove a no-DMA compute hardware hang. The dense frontier GDB
file set many software breakpoints and printed `info locals`/backtraces at
each manager boundary. That overhead is high enough that a 900 second frontier
timeout can expire while the target is still making forward progress.

The useful conclusion is narrower:

- no-DMA reached `worker-gemm-run` for segment 1 stage 0 subbatch 3;
- no-DMA progressed through manager 0..6 compute milestones;
- no-DMA reached manager 7 in `conv_call_for_manager_sync_strided()`;
- the precise next boundary still needs a thinner GDB pass.

After `detach`, the run host heartbeat did not resume:

```text
heartbeat.csv last observed line: 17913273760,947
heartbeat.csv mtime: 2026-05-09 16:27:21 UTC
```

That is not treated as a no-DMA card hang because the timeout stopped in
ordinary C code around `tiled_matmul_nn_stride_auto()`, and the subsequent
detach/reconnect attempts may have affected the `gdbserver --once` inferior
state.

## GDB helper note

During the live run, the expect/GDB script first stopped at
`prt_rerocc.c:403` and tried to print an optimized-out `scope`, leaving GDB in
an interactive prompt inside the command list. The session was recovered by
injecting `delete 25` and `continue` into the expect PTY.

The local helper was then adjusted to avoid that fragile line and use
`prt_rerocc.c:402` instead:

```text
pipeline-runtime/scripts/gdb/sbus64_no_dma_segment1_stage0_compute_frontier.gdb
```

Offline syntax checks passed after temporarily excluding
`set scheduler-locking on` from batch ELF parsing; `bash -n` also passed for:

```text
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_compute_frontier.sh
```

## Next Step

Use a thinner no-DMA compute GDB helper instead of the dense frontier:

```text
worker-gemm-run marker
  -> conv_call_for_manager_sync_strided entry, print manager only
  -> tiled_matmul_nn_stride_auto call/return for manager 7 only if practical
  -> prt_rr_release_scope or release readback for manager 7
  -> prt_runtime.c:4545 return stop
```

Avoid per-manager `info locals` and broad backtraces. If the goal is to find a
real card stop, use fewer breakpoints with a longer timeout.

`doneflag` was not used as DMA completion or pass/fail evidence.

## Cleanup

The run farm was terminated with:

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
cd deploy
firesim terminaterunfarm --forceterminate \
  -c config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml \
  -a config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml \
  -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml
```

The target instance was:

```text
i-0bdeaf782e11e2961 / 192.168.1.27
```

An AWS follow-up initially showed `shutting-down`; a final no-active-F2 check
then returned an empty active `f2.*` list:

```text
[]
```
