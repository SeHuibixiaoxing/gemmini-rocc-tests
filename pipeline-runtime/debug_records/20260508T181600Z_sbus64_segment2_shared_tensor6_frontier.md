# 2026-05-08 18:16 UTC - sbus64 segment2 shared tensor6 frontier

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Runworkload tmux session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-173005`
- Run host: `192.168.1.165`
- EC2 instance terminated after this round: `i-032f8227dfd865917`

The GDB session used the correct FireMarshal-packaged ELF:

```sh
.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

`main` was verified at `0x12060`, with `main+10` jumping to
`prt_main_entry` at `0x1214a`.

## Breakpoint strategy

This round started with broad function-entry breakpoints, then disabled noisy
ones after they proved earlier segments were healthy.

Relevant breakpoints:

```gdb
break prt_action_bind_topology
break runtime_flush_spm_xlate
break prt_gemmini_spm_xlate_flush
break pthread_create
break stage_worker_main
break prt_pipebuf_wait_full
break sync_stage_export_aliases
break exit
break abort
```

Line breakpoints were avoided.

## Confirmed non-frontiers

Segment 0:

- `prt_action_bind_topology(action_id=1, segment_idx=0)` returned `0`.
- `runtime_flush_spm_xlate()` returned.
- One worker was created.
- The worker consumed entry `buffer_id=1 / tensor_id=0`, where `full[0]=1`.
- The worker exited and the runtime advanced to segment 1.

Segment 1:

- `prt_action_bind_topology(action_id=2, segment_idx=1)` returned `0`.
- Its worker was created and exited.
- The runtime advanced to segment 2.

Segment 2 setup:

- `prt_action_bind_topology(action_id=3, segment_idx=2)` returned `0`.
- `action->spm_source.in_stage_count = 8`
- `action->spm_source.weight_count = 4`
- `action->alias_page_count = 1482`
- `action->spm_xlate.pte_count = 1482`
- `action->acc_source.all_count = 12`
- Segment2 SPM xlate flush was not the permanent stall:
  `prt_gemmini_spm_xlate_flush(manager_id=0..11)` all returned `0`.
- Segment2 workers started. GDB observed at least:
  - stage 0 worker, `action_id=3 / segment_idx=2`
  - stage 1 worker, `segment_idx=2`
  - stage 2 worker, `segment_idx=2`

## Current frontier

The most specific observed frontier is segment 2 stage 2 waiting for tensor 6:

```text
Thread 6 hit prt_pipebuf_wait_full(
  buf=0x96f48,
  idx=0,
  timeout_ns=10000000
)

buf->segment_idx = 2
buf->stage_idx = 2
buf->tensor_id = 6
buf->buffer_id = 9
buf->kind = PRT_BUF_C4_SHARED_NO_RING_PAIR
buf->is_entry = 1
buf->full[0] = 0
buf->full[1] = 0
buf->subbatch_offset = 0
buf->cmd_acc[0] = 8
buf->cmd_acc[1] = 8
buf->slot_pages[0].size = 512
```

For comparison, segment 2 stage 0 and stage 1 entry waits were healthy:

```text
stage0 entry:
  buffer_id=1 tensor_id=0 kind=PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN full[0]=1 subbatch_offset=1

stage1 entry:
  buffer_id=5 tensor_id=3 kind=PRT_BUF_C1_ENTRY_DRAM_OR_DEPEN full[0]=1 subbatch_offset=1
```

GDB also observed segment 2 stage 0 entering and returning successfully from:

```text
sync_stage_export_aliases(segment_idx=2, stage_id=0, global_stage_id=2, subbatch_id=0) -> 0
```

This is not the producer for tensor 6. Static mapping shows tensor 6 is produced
by segment 2 stage 1 and consumed by segment 2 stage 2.

## Static mapping for tensor 6

Mapping file:

`rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.ours2.yaml`

Segment 2 stage 1:

```yaml
globalStageId: 3
entryTensorIdList: [3, 2]
entryTensorTypeList: [DRAM, DRAM]
exportTensorIdList: [6]
exportTensorTypeList: [SHARED_SPM]
entryBufferIdList: [5, 6]
exportBufferIdList: [7]
```

Segment 2 stage 2:

```yaml
globalStageId: 4
entryTensorIdList: [6, 4]
entryTensorTypeList: [SHARED_SPM, ALL_RINGBUFFER]
exportTensorIdList: [7]
exportTensorTypeList: [DRAM]
entryBufferIdList: [9, 10]
exportBufferIdList: [11]
```

Buffer bindings:

```yaml
bufferBindingIdList: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13]
bufferBindingTensorIdList: [0, 4, 1000004, 1000005, 3, 2, 6, 1000006, 6, 4, 7, 1000007, 4]
bufferBindingStageLocalIdList: [0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 4294967295]
bufferBindingIsEntryList: [1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0]
bufferBindingSlotCountList: [1, 0, 1, 1, 1, 1, 2, 1, 2, 0, 1, 1, 2]
bufferBindingPagesPerSlotList: [64, 0, 1, 64, 64, 64, 512, 8, 512, 0, 64, 1, 64]
bufferBindingAliasGroupIdList: [0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0]
```

The expected runtime data path is:

1. Stage 1 consumes DRAM entries tensor 3 and tensor 2.
2. Stage 1 computes tensor 6.
3. Stage 1 marks export `buffer_id=7 / tensor_id=6` full.
4. Stage 1 calls `prt_process_c4()` for the shared pair.
5. `prt_process_c4()` should set stage 2 entry `buffer_id=9 / tensor_id=6`
   full.
6. Stage 2's `prt_pipebuf_wait_full(buffer9, idx0)` should return.

The observed stall means one of steps 2-5 did not complete, or the condition
variable wait did not wake/timeout as expected.

## Ctrl-C behavior

After waiting in `finish` from:

```text
prt_pipebuf_wait_full(buf=0x96f48, idx=0, timeout_ns=10000000)
```

for roughly 90 seconds of host time, a first GDB Ctrl-C did not recover control.
A second Ctrl-C disconnected the target:

```text
Disconnected from target.
```

No live post-stall all-thread backtrace was captured. This reinforces the rule:
for this area, break before the suspected shared-pair handoff instead of
depending on Ctrl-C after the wait region.

## Next debugging action

Start a fresh `gdbserver --once` run and place breakpoints before the shared
handoff:

```gdb
break prt_process_c4
break prt_pipebuf_wait_full
break sync_stage_export_aliases
```

Use conditions or manual filtering for:

- `prt_process_c4`: `pair->pre_export->segment_idx == 2`,
  `pair->pre_export->stage_idx == 1`,
  `pair->pre_export->tensor_id == 6`,
  `pair->nxt_entry->stage_idx == 2`,
  `pair->nxt_entry->tensor_id == 6`.
- `prt_pipebuf_wait_full`: `buf->segment_idx == 2 && buf->buffer_id == 9`.
- `sync_stage_export_aliases`: `segment_idx == 2 && stage_id == 1`.

At `prt_process_c4`, inspect:

```gdb
p *pair->pre_export
p *pair->nxt_entry
p *pair->tag
finish
p pair->pre_export->full[0]
p pair->nxt_entry->full[0]
p *pair->tag
```

This should decide whether the bug is in stage1 compute/export not reaching
`prt_process_c4`, in `prt_process_c4` state transition, or in the wait/wakeup
behavior for stage2's entry buffer.
