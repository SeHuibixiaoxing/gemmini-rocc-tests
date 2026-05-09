# 2026-05-09T17:10Z no-DMA compute thin GDB manager7 returned

## Summary

The thin no-DMA compute frontier run reached the configured
`worker-gemm-run` marker at `segment=1`, `global_stage=1`, `local_stage=0`,
`subbatch=3`, followed manager 7 through the pointwise/conv issue path, and
stopped immediately after `prt_gemm_conv_run()` returned.

Result: `prt_gemm_conv_run()` returned with `rc=0` and register `a0=0`.
This window is not the stuck point for the no-DMA compute bisection.

## Environment

- Chipyard commit: `0a0f0b1cae86153cec9466ca716249e5a6f8093e`
- Gemmini commit: `baffba9adf977b6e7cedf9ea19f614074b34cd12`
- rocc-tests commit at run start: `58eda1f4b9f33ceffe0837a024fc4014c406bbaa`
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Instance: `i-0e6eece903e7f2c9b`
- Run host: `192.168.1.21`
- Guest endpoint: `172.16.0.2:2345`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

## Commands

FireSim runworkload was launched through the tmux helper:

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
cd deploy
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh runworkload \
  pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace \
  firesim runworkload \
  -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml \
  -a /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml \
  -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml
```

GDB frontier command:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_TIMEOUT=3600 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_compute_thin.sh \
  192.168.1.21 172.16.0.2:2345 32345
```

The GDB helper uses:

- `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_compute_thin.sh`
- `pipeline-runtime/scripts/gdb/sbus64_no_dma_segment1_stage0_compute_thin.gdb`

## Key Evidence

UART confirms the intended profile:

- `no-dma compute enable: 1`
- `runner-gdb-marker-config enable=1 site=worker-gemm-run segment=1 global_stage=1 local_stage=0 subbatch=3`
- `gdbserver phase=listening`
- `gdbserver phase=inferior`
- `dummy-mode=1 skip-model=1 skip-input=1 skip-golden=1 no-dma-compute=1`

Important distinction: `no_dma_compute` itself only skips fixed-load DMA and
export DMA in runtime compute paths. The skipped model/input/golden loads in
this run came from the dummy workload profile, not from the no-DMA flag alone.

GDB reached the exact marker:

```text
site_id = 5
segment_idx = 1
global_stage_id = 1
local_stage_id = 0
subbatch_id = 3
line = 4543
```

Manager 7 milestones all fired:

- `conv_call_for_manager_sync_strided entry`
- `prt_run_pointwise_matmul_fallback_strided_impl entry`
- `tiled_matmul_nn_stride_auto call begin`
- `tiled_matmul_nn_stride_auto returned`
- `prt_rr_fence_scope entry` twice
- `prt_rr_release_scope entry`

The final temporary breakpoint stopped after the `prt_gemm_conv_run()` call at
`src/prt_runtime.c:4548`. The all-thread backtrace showed:

- `task.num_managers = 8`
- `task.manager_ids = {0, 1, 2, 3, 4, 5, 6, 7}`
- `task.tile_count = 8`
- `task.split_kind = PRT_LAYER_SPLIT_OC`
- `task.op_kind = PRT_STAGE_OP_CONV`
- `rc = 0`

Disassembly showed the stop was the instruction immediately after the call:

```text
0x30bb0 <stage_worker_main+7038>: jal 0x2363a <prt_gemm_conv_run>
=> 0x30bb4 <stage_worker_main+7042>: beqz a0,...
```

## Artifacts

Artifacts are under:

`pipeline-runtime/debug_records/artifacts/20260509T171000Z_no_dma_compute_thin_manager7_returned/`

Selected hashes:

```text
8932e4c078600dad4ee4cdbfc9e783045b74e6ab3ac792a7b09e5714550b2e77  gdb/pairdummy-cfg32-marker-frontier.expect.log
066ef2e0d633a46dc3df114dea208cd775515daf2d4c19f653879c19e81a0a24  runhost/uartlog.live
48572bb9f70ba6376f4bbbbe17d43179b301c4520ad85bdc50ffb1b9eb8b2456  local/elf.sha256
```

## Interpretation

This run rules out a hard hang in the no-DMA compute issue/fence/release window
for `segment=1/global_stage=1/local_stage=0/subbatch=3`, including manager 7.
The previous dense run timed out while still making manager progress, so the
dense timeout should be treated as GDB overhead, not as compute-stall evidence.

The next no-DMA frontier should move after the compute return:

1. `worker-export-sync` marker for the same segment/stage/subbatch.
2. `sync_stage_export_aliases()` return under no-DMA.
3. The post-compute pipebuf release/rotation loop.
4. The next subbatch or next-stage handoff.

Keep the GDB command file thin. Avoid `info locals` in hot manager loops and do
not probe the `gdbserver --once` port with non-GDB clients.

## Limitations

After detach, the FireSim workload did not visibly complete before cleanup, and
`heartbeat.live.csv` only contained the header. This does not invalidate the
GDB evidence that `prt_gemm_conv_run()` returned for the targeted window, but it
means this record is not a whole-workload pass.

Cleanup was initiated with `firesim terminaterunfarm --forceterminate`; follow-up
AWS polling must confirm no active `f2.*` instances remain.
