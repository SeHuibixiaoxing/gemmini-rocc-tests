# 2026-05-09T02:28Z sbus64 worker-entry marker hit and post-entry Ctrl-C stall

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `192.168.1.247`
- Instance: `i-03288b90526d40d79`
- ELF:
  `generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- ELF sha256:
  `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Local evidence:
  `tmp/firesim-aws-f2/pipeline-runtime-gdb/live-20260509-0207-worker-entry/`
- Final capture:
  `tmp/firesim-aws-f2/pipeline-runtime-gdb/live-20260509-0207-worker-entry/final-capture/`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH unset
PIPELINE_RUNTIME_GUEST_LOG_ENABLE=0
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0
PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0
PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0
PIPELINE_RUNTIME_BREADCRUMB_ENABLE=0
CAPTURE_PERIODIC_SYNC_ENABLE=0
```

`subbatch` was intentionally unset because `worker-entry` records `subbatch=UINT32_MAX`; filtering on
subbatch 0 would prevent the hit.

UART confirmed the runner configuration:

```text
[bertmini] runner-gdb-marker-config enable=1 site=worker-entry segment=2 global_stage=3 local_stage=1 subbatch=any manager=any tensor=any page=any token=any
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

## Interactive GDB Path

GDB was the first TCP client to `gdbserver --once`; no `nc`, telnet, curl, or port probe was used.

```bash
ssh -f -N -L 32361:172.16.0.2:2345 ubuntu@192.168.1.247
/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb \
  -q /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
(gdb) target remote :32361
(gdb) break prt_gdb_marker_stop
(gdb) continue
```

The target stopped in the dynamic loader first, then ran under GDB. Early Ctrl-C while parsing artifacts
worked and produced a stack; later Ctrl-C after continuing past the worker-entry marker did not return a
prompt during the observation window.

## Important Observations

### Artifact validation was not the stable hang

The first Ctrl-C while waiting for `worker-entry` stopped in mapping YAML parsing:

```text
#0 strncmp () from libc.so.6
#1 starts_key(...) at pipeline-runtime/src/prt_gemmini_artifacts.c:237
#2 parse_mapping_file(...) at pipeline-runtime/src/prt_gemmini_artifacts.c:1015
#3 prt_validate_gemmini_artifacts(...) at pipeline-runtime/src/prt_gemmini_artifacts.c:1178
#4 prt_runtime_run(...) at pipeline-runtime/src/prt_runtime.c:5066
```

After continuing, GDB hit the post-validate breakpoint at `prt_runtime.c:5074`, then reached
`segment-begin` for segment 0 and later segment 2. Therefore the early mapping stack was a point-in-time
interrupt, not the durable deadlock.

### Segment 2 boundary was reached

A conditional stop at `prt_runtime_gdb_marker()` confirmed:

```text
site_id=2 (segment-begin)
segment_idx=2
aux0=3   # segment 2 stages
aux1=1   # segment 2 subbatch size
```

This proves the live run crossed init, artifact validation, segment 0, and segment 1, then entered the
target segment.

### Target worker-entry marker hit

The configured marker hit correctly in the runtime's own marker path:

```text
Thread 6 "rerocc_pipeline" hit Breakpoint 1, prt_gdb_marker_stop()
print g_prt_gdb_marker_state
site_id = 4
segment_idx = 2
global_stage_id = 3
local_stage_id = 1
subbatch_id = 4294967295
manager_id = 4
rc = 0
aux0 = 2   # entry_count
aux1 = 1   # export_count
line = 4198
```

Before letting the marker stop run, the same worker context was inspected at `stage_worker_main:4191`:

```text
ctx->stage_id = 1
ctx->action->segment_idx = 2
ctx->action->pipeline_segment_ref->stages[ctx->stage_id].stage_id = 3
ctx->action->pipeline_segment_ref->stages[ctx->stage_id].layer_id = 3
ctx->action->pipeline_segment_ref->stages[ctx->stage_id].acc_util = 4
exec->stage_thread_count = 3
exec->stage_acc_ids[ctx->stage_id] = 4
exec->stage_dma_ids[ctx->stage_id] = 4
exec->stage_tile_counts[ctx->stage_id] = 4
entry_bufs[0] = 0x96600
entry_bufs[1] = 0x96918
export_bufs[0] = 0x96c30
```

This validates both the marker filter and the current mapping for the target stage.

### Thread snapshot at target marker

At the marker stop, the main thread was in the sink-progress watchdog loop:

```text
Thread 1:
#0 clock_nanosleep()
#1 nanosleep()
#2 prt_runtime_run(...) at pipeline-runtime/src/prt_runtime.c:5408
```

A segment 2 stage 0 worker was already processing an entry and was in the host-to-SPM DMA path:

```text
Thread 4:
#0 pread64() from libc.so.6
#1 prt_host_virt_to_phys(...) at pipeline-runtime/src/prt_page_table.c:260
#2 dma_copy_host_to_spm_pages_linux(...) at pipeline-runtime/src/prt_dma.c:818
#3 prt_dma_copy_dram_to_spm_pages_prefix(...) at pipeline-runtime/src/prt_dma.c:2834
#4 prt_process_c1(...) at pipeline-runtime/src/prt_scheduler.c:294
#5 stage_worker_main(...) at pipeline-runtime/src/prt_runtime.c:4236
```

Other segment 2 workers were at or around `worker-entry` / `runtime_stage_global_id()`.

## Current Frontier

This run moves the reliable live-GDB frontier forward from "late marker did not hit" to:

```text
runtime init passed
artifact validation passed
segment 2 began
segment 2 local stage 1 / global stage 3 worker-entry hit
stage 0 was already in host-to-SPM DMA translation/copy path
main thread was waiting for sink progress
continuing after this point made Ctrl-C fail to return a GDB prompt during the observation window
```

The current suspected runtime window is after worker-entry and around entry processing / host-to-SPM DMA for
segment 2, not earlier artifact parsing or topology bind. The stack specifically points at the
`prt_process_c1 -> prt_dma_copy_dram_to_spm_pages_prefix -> dma_copy_host_to_spm_pages_linux -> prt_host_virt_to_phys`
path for stage 0 in this run.

## Debugger Notes

A mistake in this session was adding extra conditional breakpoints directly on `prt_gdb_marker_stop()` for
site 21/22/23/24. That function only runs after the runtime marker filter decides to stop, so it is not a
reliable way to observe markers outside the current environment-selected site. For subsequent runs, use one
of these instead:

- change the environment marker site and rerun, or
- break on the source line for the desired call site, or
- break on `prt_runtime_gdb_marker()` with conditions on the function arguments `site_id`, `segment_idx`,
  `global_stage_id`, and `local_stage_id`.

The session also demonstrated that GDB conditions on optimized local variables such as `seg_idx` can be
unreliable. Function arguments and fields reachable from live pointers were more dependable.

## Cleanup

Evidence was captured before teardown:

```text
gdb-pane-final.txt
gdb.log
uartlog
uartlog.tail
heartbeat.csv
heartbeat.tail
```

FireSim teardown:

```bash
cd sims/firesim
source sourceme-manager.sh --skip-ssh-setup
cd deploy
firesim terminaterunfarm --forceterminate \
  -c config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml \
  -a config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml \
  -r ../../firesim-staging/sample_config_build_recipes.yaml
```

Termination log:

```text
/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-09--02-25-25-terminaterunfarm-UZ2IK4VQXROVRIDN.log
```

`terminaterunfarm` requested termination of `i-03288b90526d40d79`. A follow-up AWS query showed the F2
instance in `shutting-down`; no local GDB or SSH tunnel remained. Stale local runworkload wrappers for this
run were killed after FireSim teardown had been issued.
