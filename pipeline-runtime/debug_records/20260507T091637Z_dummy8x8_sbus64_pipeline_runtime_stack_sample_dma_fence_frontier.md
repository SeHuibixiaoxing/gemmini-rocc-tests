# 2026-05-07 dummy8x8 sbus64 pipeline-runtime stack sampling: DMA fence frontier

## Summary

This run used remote `gdbserver` without business breakpoints to sample the
current `pipeline-runtime` call stack. The generic remote GDB path remained
usable, but the workload did not complete. The strongest frontier is the
breadcrumb slot:

```text
kind=dma phase=dma_wait_before_fence
seg=0 gstage=0 lstage=0 sb=1 tensor=2 page=31 token=546 mgr=0
line=3467 src=0x40702c00 dst=0x103362000 done_pa=0x103217000
```

This corresponds to `prt_dma.c` immediately before `hw_dma_fence()` in
`dma_blocking_wait()`.

## Hardware and config

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Build result:
  `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`
- Type: `dummy8x8`, `4c12p12`, `sbus64`, `cfg32`, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-01ba0ada3ef706253`, private IP `192.168.1.84`

## Commands

Launch, setup, and run were started by the workflow helper:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.84
```

The stack sampling helper was run as the first TCP client to guest
`gdbserver --once`:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_SAMPLE_COUNT=4 \
PRT_GDB_SAMPLE_SECONDS=75 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_stack_sample.sh \
  192.168.1.84 172.16.0.2:2345 32345
```

No `nc`, telnet, browser fetch, or port probe was used before GDB.

## Artifacts

- Run log:
  `sims/firesim/deploy/logs/2026-05-07--08-56-47-runworkload-7AAF0UHC9IUXMCA7.log`
- Result directory:
  `sims/firesim/deploy/results-workload/2026-05-07--08-56-47-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- GDB transcript:
  `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-stack-sample-20260507T090513Z-192_168_1_84-172_16_0_2/pairdummy-cfg32-stack-sample.expect.log`
- Local breadcrumb copy:
  `tmp/firesim-aws-f2/pipeline-runtime-live/20260507T091637Z-stack-sample/bertmini-batch8.breadcrumb.bin`
- Termination log:
  `sims/firesim/deploy/logs/2026-05-07--09-17-43-terminaterunfarm-Z0IMHVUNKXSCZKAS.log`

## Observations

Initial attach stopped in the dynamic loader. The first timed interrupt
succeeded and showed mapping YAML parsing:

```text
value_after_colon
parse_mapping_file
prt_validate_gemmini_artifacts
prt_runtime_run
prt_main_entry
```

The second timed interrupt succeeded after the workload had moved into stage
execution. GDB showed:

- Thread 1 in `clock_nanosleep()` from `prt_runtime_run()`.
- Thread 2 in a progress-log `write()` reached from:
  `prt_rr_acquire_scope -> dma_batch_scope_acquire ->
  dma_copy_spm_pages_to_host_linux -> prt_dma_copy_spm_pages_to_dram_prefix ->
  copy_tensor_pages_to_model_alias_target -> sync_stage_export_aliases ->
  stage_worker_main`.

The third timed interrupt sent Ctrl-C but did not regain a GDB prompt before the
helper timeout:

```text
GDB_STACK_SAMPLE_3_CONTINUE
GDB_STACK_SAMPLE_3_INTERRUPT
continue
Continuing.
^C
timeout waiting for Ctrl-C/SIGINT stop
```

Guest durable text log last showed stage 0 tensor 2 export progress through the
first chunk of the second alias target:

```text
export-target-dispatch stage=0 tensor=2 target_seq=1 ... phase=begin
dma-export-host stage=0 tensor=2 phase=first-chunk-submitwait-begin page=0 ...
dma-export-host stage=0 tensor=2 phase=first-chunk-submitwait-end page=0 ... rc=0
```

The breadcrumb file is more precise than the durable text log. Decoding it gave:

```text
version=1 enabled=1 slot_count=64 update_count=8819
last_kind=dma last_phase=dma_wait_before_fence
last_slot seg=0 gstage=0 lstage=0 sb=1 tensor=2 mgr=0 page=31 rc=0 line=3467
frontier=dma/dma_wait_before_fence seg=0 gstage=0 lstage=0 sb=1 mgr=0 tensor=2 page=31 tok=546
```

`triage_prt_capture.py` classified this as:

```text
claim_class=stable_observation
disturbance_risk=low
recommended_next_probe=static-read-dma-page-boundary-then-page-trigger
verdict=frontier at dma/dma_wait_before_fence
```

## Interpretation

This is not a generic gdbserver failure. The same AGFI already passed the old
remote GDB smoke matrix in earlier records, and this run successfully connected,
continued, interrupted twice, read threads, and collected backtraces. The
failure is inside the target workload after stage execution begins.

The current runtime is using the blocking DMA backend:

```text
dma-backend using blocking_fence completion via hw_dma_fence + rr_fence_scope
```

Because `spm_xlate_enable=1` forces `sync_mode=blocking_debug`,
`prt_runtime_init()` also forces:

```text
rt->cfg.dma_backend = PRT_DMA_BACKEND_BLOCKING_FENCE;
rt->cfg.gemmini_mode = PRT_GEMMINI_MODE_BLOCKING_FENCE;
```

The command line had `export_dma_timeout_ms=0`. More importantly, this blocking
backend calls `hw_dma_fence()` before it can observe any timeout path, so a
blocked custom DMA fence can also make Ctrl-C ineffective.

Current narrowed blocker:

```text
stage 0 / global stage 0 / subbatch 1 / tensor 2 / manager 0 /
export DMA page 31 / token 546 / dma_blocking_wait before or inside hw_dma_fence()
```

The durable text log may lag behind this exact point because it is rootfs-image
backed and periodically synced. Do not treat the text log as the exact final PC.

## Cleanup

After collecting the transcript, guest status/log tail, gdbserver log/info,
UART tail, heartbeat, and breadcrumb, the run farm was terminated:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`firesim terminaterunfarm` reported success and selected instance
`i-01ba0ada3ef706253` for termination. AWS subsequently reported the F2 instance
in `shutting-down`; no running F2 instance remained for the active workload.

## Next step

Do not spend the next run on broad breakpoints. Use the frontier above to test a
narrower DMA wait hypothesis:

1. Preserve stack sampling as the default live triage method.
2. Add or enable page/token-focused DMA export probes around tensor 2 page 31
   and token 546.
3. Static-read the blocking DMA backend and decide whether this debug workload
   should avoid `hw_dma_fence()` for exports, or at least expose a fail-fast path
   before entering an uninterruptible custom fence.
4. Rebuild only the workload/rootfs if the change is software-only; no new AGFI
   is required for this observation path.
