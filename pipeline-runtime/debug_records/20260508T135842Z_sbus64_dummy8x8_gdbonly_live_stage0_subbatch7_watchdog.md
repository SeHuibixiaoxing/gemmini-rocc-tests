# 2026-05-08T13:58Z - sbus64 dummy8x8 GDB-only live run reached stage0 subbatch7 before watchdog kill

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Run host: `i-0c1e160390ced75bc`, private IP `192.168.1.212`
- Runworkload tmux:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-132934`

## Observation Discipline

This run intentionally used the `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`
profile:

- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=0`
- `CAPTURE_PERIODIC_SYNC_ENABLE=0`
- `PIPELINE_RUNTIME_CHILD_PROC_DIAG_ENABLE=0`
- stdio capture remained on UART only

The purpose was to avoid the earlier failure mode where guest file logging,
breadcrumb mmap traffic, or periodic sync moved the apparent stall. The live
frontier below is therefore based on remote GDB stops and stack/variable reads.
The host watchdog copied guest files when it terminated the run, but those files
are not used as the primary frontier evidence.

## Commands

After UART reported `[gdbserver] phase=listening`, the only first TCP client was
GDB through an SSH tunnel:

```bash
ssh -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  -N -L 32349:172.16.0.2:2345 ubuntu@192.168.1.212

.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex 'set print thread-events off' \
  -ex 'target remote :32349'
```

Initial breakpoints:

```gdb
break pipeline-runtime/src/prt_runtime.c:4205
break pipeline-runtime/src/prt_runtime.c:4353
break pipeline-runtime/src/prt_runtime.c:4417
break pipeline-runtime/src/prt_runtime.c:4514
break pipeline-runtime/src/prt_runtime.c:4560
break pipeline-runtime/src/prt_runtime.c:4633
break pipeline-runtime/src/prt_runtime.c:4682
continue
```

After stage0/subbatch0 passed, early breakpoints were disabled and the C2 /
worker-done breakpoints were narrowed with conditions on `subbatch_offset`.

## Live GDB Result

GDB first stopped in the dynamic loader, then continued into runtime startup.
A manual Ctrl-C before worker creation showed a slow artifact parse, not a
runtime stall:

```text
parse_u32_scalar("- layer_id: 34")
parse_mapping_file(... gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml ...)
prt_validate_gemmini_artifacts(...)
prt_runtime_run(...)
prt_main_entry(...)
```

The worker thread then reached the stage0 pipeline. Important live stops:

- `stage_wait_exports_ready(stage_id=0, subbatch=0, export_count=1,
  shared_pair_count=0)` at `prt_runtime.c:3574`; it later returned `rc=0`.
- `build_stage_task_desc(stage_id=0)` and `build_stage_conv_desc(stage_id=0)`;
  these were slow under GDB single-stepping but were not hard stalls.
- `prt_gemm_conv_run()` entry at `prt_runtime.c:4514`.
- export-sync region at `prt_runtime.c:4562`, after Gemmini compute returned.
- `prt_process_c2()` at `prt_runtime.c:4633`.
- worker-done at `prt_runtime.c:4682`.

For stage0/subbatch0, the compute descriptor was:

```text
op_kind = PRT_STAGE_OP_CONV
split_kind = PRT_LAYER_SPLIT_OC
num_managers = 8
manager_ids = {0,1,2,3,4,5,6,7}
tile_count = 8
conv: N=1, IH=256, IW=1, IC=256, OC=256, OH=256, OW=1, K=1, G=1, stride=1
input  = 0x3ff6d95400
weight = 0x3ff6d85400
bias   = 0x3ff6d85000
output = 0x3ff6da5400
```

C2 entry state at subbatch0:

```text
b->kind = PRT_BUF_C2_EXPORT_DRAM_OR_DEPEN
b->subbatch_offset = 0
b->full[0] = 1
b->cmd_running[0] = 0
b->dma_token_live[0] = 0
b->dram_base_addr[0] = 274724643840
```

Stage0/subbatch3, the previous file-log-sensitive frontier, was explicitly
passed:

```text
worker-done hit: ctx->stage_id = 0
export_bufs[0]->subbatch_offset = 3
export_bufs[0]->full[0] = 0
export_bufs[0]->state_epoch = 6

C2 entry hit: b->subbatch_offset = 3
b->full[0] = 1
b->cmd_running[0] = 0
b->dma_token_live[0] = 0
b->state_epoch = 7
```

Stage0 then advanced further:

```text
worker-done hit: export_bufs[0]->subbatch_offset = 4
worker-done hit: export_bufs[0]->subbatch_offset = 7
export_bufs[0]->full[0] = 0
export_bufs[0]->state_epoch = 14
```

The last observed stop before connection loss was a conditional C2 breakpoint
after raising the condition to `b->subbatch_offset >= 7`. The GDB connection was
then closed before the value could be printed.

## Termination Cause

The GDB disconnect is not evidence of a clean target exit. The host watchdog
terminated the F2 run farm:

```text
[prt-host-watchdog] hb='18061954217, 956' idle=627s hb_idle=627s
[prt-host-watchdog] host idle timeout reached after 627s (hb_idle=627s)
[prt-host-watchdog] calling terminaterunfarm --forceterminate
```

AWS confirmed the run host entered `shutting-down`:

```text
i-0c1e160390ced75bc f2.6xlarge shutting-down 192.168.1.212
```

This matters because interactive GDB stops pause the target and therefore pause
heartbeat progress. The default host watchdog idle timeout is 600 seconds, so a
long GDB stop sequence can kill an otherwise still-debuggable run.

## Current Frontier Interpretation

Confirmed negative evidence:

- The no-log/no-breadcrumb run did not stall before `stage_worker_main`.
- Artifact parsing was slow but eventually progressed.
- stage0/subbatch0 passed export-ready, task build, Gemmini compute, C2, and
  worker-done.
- stage0/subbatch3 C2 and worker-done passed under GDB-only observation.
- stage0 reached at least worker-done with `subbatch_offset=7`.

Not yet proven:

- Whether subbatch7 C2 returns.
- Whether stage0 exits cleanly after the final subbatch.
- Whether the next stall is stage-finish, main-thread join/teardown, a later
  segment/stage, or a higher-level pipeline-runtime synchronization path.

Best current statement: the earlier guest-file-log frontier around
stage0/subbatch3 C2 is not the current GDB-only hard stall. The live frontier
has moved past it to at least stage0/subbatch7, but the run was killed by the
host watchdog before a final stack could be collected.

## Next Run Changes

For interactive GDB runs, launch `runworkload` with a larger host idle timeout,
for example:

```bash
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run <run-host-ip>
```

Do not disable the watchdog entirely unless there is another explicit cleanup
guard. The correct next GDB strategy is:

1. Keep guest file logs and breadcrumb disabled.
2. Set only late conditional breakpoints, starting at subbatch7 C2 return /
   worker-done and stage-worker loop exit.
3. Avoid `next` / `finish` as stall proof because remote single-step can be
   extremely slow on this target.
4. If no breakpoint hits for a bounded time, use Ctrl-C once and inspect
   `info threads` / `thread apply all bt`, then continue or terminate manually.
