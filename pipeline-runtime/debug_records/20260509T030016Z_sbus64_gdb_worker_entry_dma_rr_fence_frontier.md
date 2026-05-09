# 20260509T030016Z - sbus64 gdb worker-entry DMA/RR fence frontier

## Context

- Date: 2026-05-09 UTC
- Purpose: continue low-perturbation pipeline-runtime hang localization with live remote gdbserver.
- Hardware:
  - AGFI: `agfi-077451484fe3b63c3`
  - AFI: `afi-07989ce9ce725a690`
  - Config family: `pairmanager_dummy8x8_4c12p12_sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  - `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  - `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload results:
  - `sims/firesim/deploy/results-workload/2026-05-09--02-38-37-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Run host:
  - Instance: `i-0f324ec436e0a7e11`
  - Private IP: `192.168.1.241`
- Local tmux sessions:
  - Runworkload: `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-023836`
  - Tunnel: `prt-gdb-tunnel-20260509-0307`
  - GDB: `prt-gdb-live-20260509-0307`
- Runtime marker env:
  - `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
  - `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry`
  - `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2`
  - `PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3`
  - `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1`
  - `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH` unset
- Logging constraints:
  - Guest sparse/deep/audit/checkpoint/breadcrumb/periodic sync logs disabled.
  - GDB used instead of adding guest file logs, because prior runs showed log output moves the hang.
  - Doneflag polling remains disabled and must stay disabled; DMA completion is being tested through wait/fence paths.

## GDB Bring-Up

The guest reached:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
```

The local SSH tunnel was opened only after `phase=listening`, so GDB was the first client to the `gdbserver --once` endpoint.

GDB connected successfully:

```text
target remote :32361
Remote debugging using :32361
```

Initial breakpoint:

```gdb
break prt_gdb_marker_stop
continue
```

## Pre-Marker Observation

Before the worker-entry marker fired, a Ctrl-C while continuing showed the program was still in startup artifact validation:

```text
#0 strncmp()
#1 starts_key(... "others_spm_tensor_util" ...)
#2 parse_mapping_file(...)
#3 prt_validate_gemmini_artifacts(...)
#4 prt_runtime_run(... prt_runtime.c:5066)
```

This was not a model execution hang. Continuing later reached `pthread_join` at `prt_runtime.c:5413`, and the first two segments reached `prt_runtime.c:5415` with:

```text
rt->fatal_error = 0
rt->stop_requested = 1
run_rc = 0
```

So segment 0 and segment 1 completed normally. Earlier `g_prt_debug_state` values such as `segment=0/1`, `phase=RR_ACQUIRE`, `wait_phase=ENTRY_C1_PROCESS` were stale last-progress state from successful worker completion, not the actual hang.

## Worker-Entry Marker Hit

The intended marker did fire:

```text
Thread 3 hit Breakpoint 1, prt_gdb_marker_stop()
```

Marker state:

```text
site_id = 4                    # worker-entry
segment_idx = 2
global_stage_id = 3
local_stage_id = 1
subbatch_id = UINT32_MAX
manager_id = 4
tensor_id = UINT32_MAX
page_idx = UINT32_MAX
token_id = UINT32_MAX
rc = 0
aux0 = 2
aux1 = 1
line = 4198
```

Threads at this stop:

```text
Thread 1: prt_runtime_run -> nanosleep at prt_runtime.c:5408
Thread 3: stage1 at worker-entry marker, prt_runtime.c:4191
Thread 4: stage0 in C1 fixed-load DMA submit/wait path
Thread 5: stage2 waiting for input pipebuf full, prt_runtime.c:4315
```

The global debug state at the marker already showed downstream stage2 waiting on tensor 6:

```text
segment_idx = 2
global_stage_id = 4
local_stage_id = 2
subbatch_id = 0
phase_id = 3                  # WAIT
wait_phase_id = 110           # ENTRY_FULL
tensor_id = 6
pipebuf_kind = 3
```

This matches earlier evidence that segment2 stage2 is waiting on the tensor6 shared/input path produced by segment2 stage1.

## Stage0 DMA Context

At the worker-entry marker, Thread 4 was concurrently in stage0 C1 fixed-load DMA:

```text
stage_worker_main
  prt_process_c1
  prt_dma_copy_dram_to_spm_pages_prefix
  dma_copy_host_to_spm_pages_linux
  dma_submit_wait_annotated_scoped
  prt_dma_wait
  dma_blocking_wait
```

Token at `dma_blocking_wait`:

```text
id = 5289
stage_idx = 0
tensor_id = 0
manager_id = 0
rr_cfg_id = 0
rr_opcode_id = 2
debug_page_idx = 24
debug_src_addr = 4376081408
debug_dst_addr = 1073747968
debug_bytes = 1024
debug_done_flag_va = 385024
debug_done_flag_pa = 4347342848
done = 0
hw_done_flag = 0
rr_scope_valid = 1
rr_scope_external = 1
```

The stage0 DMA `hw_dma_fence()` entry was hit at `prt_dma.c:2459`. Single-stepping showed:

```text
2459 __asm__ volatile("fence" ::: "memory");
2460 ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);
```

After allowing execution to continue, Thread 4 reached `dma_token_fence_scope`, which proves the DMA `hw_dma_fence()` returned for token 5289. The current evidence does not support "DMA doneflag polling hang" or "DMA hw fence never returns" for this token.

## Stage1 DMA Context

After the worker-entry marker continued, Thread 3 entered the stage1 C1 fixed-load path:

```text
prt_host_virt_to_phys
  dma_copy_host_to_spm_pages_linux
  prt_dma_copy_dram_to_spm_pages_prefix
  prt_process_c1
  stage_worker_main
```

Context at the `prt_host_virt_to_phys` stop:

```text
stage_idx = 1
tensor_id = 3
manager_id = 4
vaddr = 0x3ff6df6800
```

Token at stage1 `dma_blocking_wait`:

```text
id = 5290
stage_idx = 1
tensor_id = 3
manager_id = 4
rr_cfg_id = 2
rr_opcode_id = 2
debug_page_idx = 0
debug_src_addr = 4376254464
debug_dst_addr = 1077936128
debug_bytes = 1024
debug_done_flag_va = 385028
debug_done_flag_pa = 4347342852
done = 0
hw_done_flag = 0 initially
rr_scope_valid = 1
rr_scope_external = 1
```

After execution reached `dma_token_fence_scope`, the same token showed:

```text
hw_done_flag = 1
```

This proves the stage1/tensor3/page0 DMA `hw_dma_fence()` also returned and refreshed the completion flag.

## Current Frontier

Both active DMA waits advanced past `hw_dma_fence()` and reached the shared ReRoCC scope fence path:

Stage0 token 5289:

```text
dma_token_fence_scope(tok=0x3ff6d76ff8)
  prt_rr_fence_scope(scope=0x3ff6d76f38)
scope = {
  valid = 1,
  cfg_id = 0,
  stage_id = 0,
  manager_id = 0,
  opcode_id = 2
}
```

Stage1 token 5290:

```text
dma_token_fence_scope(tok=0x3ff6d35ff8)
  prt_rr_fence_scope(scope=0x3ff6d35f38)
scope = {
  valid = 1,
  cfg_id = 2,
  stage_id = 1,
  manager_id = 4,
  opcode_id = 2
}
```

The code path is:

```c
int prt_rr_fence_scope(prt_rr_scope_t *scope) {
  if (scope->valid) {
    rr_fence(scope->cfg_id);
  }
  return PRT_OK;
}
```

After disabling the `prt_rr_fence_scope` entry breakpoint and continuing, Ctrl-C was sent but GDB did not regain a prompt in the observed window. The last confirmed executable frontier is therefore after both workers reached `prt_rr_fence_scope`, with the likely blocker in or immediately after `rr_fence(cfg_id)` for cfg0 and/or cfg2.

Important nuance: this record proves the frontier moved past both DMA `hw_dma_fence()` calls. It does not yet prove by a source-line single-step that the PC is inside the exact `rr_fence()` instruction. The next run should narrow this by stopping at `prt_rerocc.c:378`, stepping over `rr_fence(scope->cfg_id)`, and checking whether control reaches line 379 (`rrf-e`).

## Interpretation

Most likely current blocker:

- ReRoCC shared scope fence in `prt_rr_fence_scope()`, entered from DMA wait cleanup:
  - stage0 fixed-load token 5289, tensor0 page24, manager0/cfg0/opcode2
  - stage1 fixed-load token 5290, tensor3 page0, manager4/cfg2/opcode2

Deprioritized by this run:

- Startup mapping parse: observed earlier but later completed.
- Segment0/segment1 runtime execution: both reached normal segment completion.
- Target worker-entry not reached: false; marker did fire for segment2/global3/local1.
- DMA doneflag polling: disabled and not used.
- DMA `hw_dma_fence()` for the two observed tokens: both advanced to shared RR fence.

## Next Narrowing Step

Use a fresh or recovered GDB session with fewer active breakpoints:

```gdb
set pagination off
break prt_gdb_marker_stop
continue

# After marker hit:
disable 1
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:378
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:379
continue
```

At line 378:

```gdb
p *scope
next
```

Expected interpretations:

- `next` reaches line 379: `rr_fence()` returned; continue looking at release/pipebuf publish.
- `next` does not return and Ctrl-C cannot recover: exact blocker is `rr_fence(cfg_id)` in `prt_rr_fence_scope`.
- GDB lands repeatedly on cfg0 and cfg2: shared fence pressure is concurrent between stage0 and stage1 fixed-load DMA cleanup, so consider serializing/reordering shared ReRoCC fence or reducing simultaneous manager/cfg reuse for the reproducer.

