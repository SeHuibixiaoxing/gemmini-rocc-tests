# 20260509T061047Z - sbus64 live GDB moved build-task frontier past SPM xlate flush

## Goal

Continue the live interactive GDB session from the stable
`worker-before-build-stage-task` frontier and walk the next call ladder inside
`build_stage_task_desc()` for the
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` F2 profile.

This was intentionally a live GDB round, not a primary batch-GDB helper round.
The goal was to test whether the hang window was in fixed-load DMA, SPM page
binding, or per-manager SPM xlate flush.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host:
  - Instance: `i-024d110341f94486d`
  - Private IP: `192.168.1.57`
  - Instance type: `f2.6xlarge`
  - FireSim cluster tag: `pairbertb8d12s64gdbcfg32nicnt`
- Workflow:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--05-27-26-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- GDB tmux:
  `prt-gdb-sbus64-buildtask2`
- SSH tunnel tmux:
  `prt-gdb-tunnel-sbus64-buildtask2`, local `:32365` to guest
  `172.16.0.2:2345`
- GDB transcript:
  `tmp/firesim-aws-f2/gdb-live/20260509-0537-sbus64-buildtask-inner/gdb_tmux.log`
- GDB binary:
  `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb`
- ELF:
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`

## Entry State

The round started from the previously confirmed marker:

```text
site=23 worker-before-build-stage-task
segment=2
global_stage=3
local_stage=1
subbatch=0
manager=4
```

GDB entered Thread 5 in `build_stage_task_desc(stage_id=1)`:

```text
task = {
  stage_id = 1,
  acc_id = 4,
  manager_ids = {4, 5, 6, 7},
  tile_count = 4,
  split_kind = PRT_LAYER_SPLIT_OC
}
```

The task chose the conv path, not the resadd path:

```text
build_stage_conv_desc(stage_id=1)
  -> stage_prepare_exec_views(rt, stage_id=1, layer=0x669e8)

layer.index = 3
layer.type = "conv"
layer.tensor_count = 4
```

## Live-GDB Observations

`stage_prepare_exec_views()` initialized with the expected action context:

```text
action_id = 3
segment_idx = 2
stage->tensor_id_count = 4
spm_xlate_enable = 1
page_size_bytes = 1024
watchdog = 600000 ms
tensors = {1000006, 2, 3, 6}
local page counts = {8, 64, 64, 512}
```

The first fixed tensor load returned successfully:

```text
prt_dma_copy_dram_to_spm_pages(
  stage_idx=1,
  tensor_id=1000006,
  manager_id=4,
  timeout_ns=5000000000,
  src=0x3ff6e26c00)
```

All 8 pages returned `rc=0`:

```text
page0 token 5586 src 0x104a14000 dst 0x404b0000 rc 0
page1 token 5587 src 0x1049b4000 dst 0x4051ac00 rc 0
page2 token 5588 src 0x1049b4400 dst 0x4061ac00 rc 0
page3 token 5589 src 0x1049b4800 dst 0x4071ac00 rc 0
page4 token 5590 src 0x1049b4c00 dst 0x4041b000 rc 0
page5 token 5591 src 0x1049b5000 dst 0x4051b000 rc 0
page6 token 5592 src 0x1049b5400 dst 0x4061b000 rc 0
page7 token 5593 src 0x1049b5800 dst 0x4071b000 rc 0
```

All observed SPM page-table binds returned `rc=0`:

```text
vpage_start=193 page_count=8
  first {ppn=4203, acc_id=4, local_page_idx=107}
  last  {ppn=7276, acc_id=7, local_page_idx=108}

vpage_start=201 page_count=64
  first {ppn=4112, acc_id=4, local_page_idx=16}
  last  {ppn=7199, acc_id=7, local_page_idx=31}

vpage_start=265 page_count=64
  first {ppn=4096, acc_id=4, local_page_idx=0}
  last  {ppn=7183, acc_id=7, local_page_idx=15}

vpage_start=329 page_count=512
  first {ppn=8192, acc_id=8, local_page_idx=0}
  last  {ppn=64, acc_id=0, local_page_idx=64}
```

`runtime_flush_stage_spm_xlate(stage_id=1)` was reached and the per-manager
flush wrapper returned successfully for managers 4, 5, 6, and 7. For each
manager, GDB verified that execution crossed the actual
`rerocc_gemmini_spm_xlate_flush()` instruction at `prt_rerocc.c:519` and
arrived at line 520.

Observed scope state:

```text
manager 4: rc=0, scope={valid=1,cfg_id=31,stage_id=UINT32_MAX,manager_id=4,opcode_id=3}, prev_binding=0x1, wrapper return=0
manager 5: rc=0, scope={valid=1,cfg_id=31,stage_id=UINT32_MAX,manager_id=5,opcode_id=3}, prev_binding=0x1, wrapper return=0
manager 6: scope={valid=1,cfg_id=31,stage_id=UINT32_MAX,manager_id=6,opcode_id=3}, prev_binding=0x1, wrapper return=0
manager 7: scope={valid=1,cfg_id=31,stage_id=UINT32_MAX,manager_id=7,opcode_id=3}, prev_binding=0x1, wrapper return=0
```

The round was stopped after `runtime_flush_stage_spm_xlate()` returned to
`stage_prepare_exec_views()`:

```text
stage_prepare_exec_views(...) at prt_runtime.c:2319
2319        prt_log_gate_clear_context();
```

## Interpretation

This round moved the live frontier forward. The previous stable window was:

```text
worker-before-build-stage-task -> worker-after-build-stage-task
```

The current known-good sub-window now includes:

```text
build_stage_task_desc(stage_id=1)
  -> build_stage_conv_desc(stage_id=1)
  -> stage_prepare_exec_views(stage_id=1)
     -> first fixed tensor load for tensor 1000006
     -> SPM bind for the observed exec vpage ranges
     -> runtime_flush_stage_spm_xlate(stage_id=1)
        -> prt_gemmini_spm_xlate_flush(manager 4)
        -> prt_gemmini_spm_xlate_flush(manager 5)
        -> prt_gemmini_spm_xlate_flush(manager 6)
        -> prt_gemmini_spm_xlate_flush(manager 7)
```

Therefore, for this run, the active frontier is after the SPM xlate flush
returns to `stage_prepare_exec_views()` at `prt_runtime.c:2319`.

This rules out the following as the first observed stuck point for this
specific repro:

- fixed tensor load DMA for tensor `1000006`, pages 0 through 7;
- the observed SPM `prt_spm_bind_vpages_ctx()` calls;
- per-manager SPM xlate acquire scope for managers 4 through 7;
- the actual per-manager `rerocc_gemmini_spm_xlate_flush()` instruction for
  managers 4 through 7.

## Pitfalls Captured

- Conditional breakpoints on optimized locals such as `stage_id` can fail or
  fire with "optimized out" state. Prefer unconditional function/line stops and
  inspect arguments after the stop.
- `g_prt_debug_state` and `g_prt_gdb_marker_state` are global. They are useful
  for coarse gating, but can be overwritten by other worker threads. Combine
  marker hits with the selected thread and `set scheduler-locking on`.
- Keep lower-level breakpoints disabled once a path is ruled out. Leaving DMA,
  bind, and acquire-scope breakpoints enabled makes `advance` stop at internal
  callees instead of answering whether the higher-level instruction returns.
- When using `advance` to check whether a RoCC/custom instruction returns,
  first disable internal helper breakpoints that can intercept the `advance`.
- Do not treat DMA doneflag polling as pass/fail evidence. The useful evidence
  here was live call return, wrapper return value, and crossing the exact custom
  instruction line.
- Do not probe `gdbserver --once` with `nc`, telnet, curl, or port scanners.
  The first TCP client must remain GDB.

## Next Debug Target

Start the next fresh run after documenting, committing, and terminating this
F2 run farm. Re-enter the same marker window, then place the next ladder after
the current frontier:

```text
stage_prepare_exec_views() after runtime_flush_stage_spm_xlate return
build_stage_conv_desc() return from stage_prepare_exec_views()
build_stage_task_desc() return
worker-after-build-stage-task marker
```

If the next run reaches the same `prt_runtime.c:2319` frontier, continue with
`finish` from `stage_prepare_exec_views()` and record whether execution returns
to `build_stage_conv_desc()` or moves into the next conv descriptor setup path.
