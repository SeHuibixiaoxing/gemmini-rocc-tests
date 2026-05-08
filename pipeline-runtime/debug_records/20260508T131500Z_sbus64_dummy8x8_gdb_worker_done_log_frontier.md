# 2026-05-08 13:15Z - sbus64 dummy8x8 live GDB worker-done/log frontier

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Instance: `i-0602fd2657c860372`, private IP `192.168.1.224`
- Local GDB tunnel: `32348 -> 172.16.0.2:2345`
- GDB log: `artifacts/20260508T131500Z_sbus64_dummy8x8_gdb_worker_done_log_frontier/gdb-interactive.log`

## What Was Tested

This was a live GDB boundary run with `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0`. The run used source/function breakpoints rather than the marker trap helper. The goal was to move the frontier after the earlier finding that stage0 tensor2 export aliases and subbatch0 C2 export completed.

The active breakpoint set confirmed:

- `build_stage_task_desc()` completed for stage0/subbatch0.
- `prt_gemm_conv_run()` completed for stage0/subbatch0 with all managers 0..7 returning through `conv_call_for_manager_sync_strided()`.
- `sync_stage_export_aliases()` completed for stage0/subbatch0.
- `prt_process_c2()` completed for stage0/subbatch0 and retired tensor2 export.
- stage0 then completed C2/worker-done for subbatch1, subbatch2, and subbatch3 according to GDB auto-prints.

## Key Evidence

GDB printed the last successful automatic boundary events:

```text
HIT c2-entry stage=0 tensor=2 idx=0 subbatch_offset=1 full=1
HIT worker-done stage=0 export0_offset=2 export0_full0=0
HIT c2-entry stage=0 tensor=2 idx=0 subbatch_offset=2 full=1
HIT worker-done stage=0 export0_offset=3 export0_full0=0
HIT c2-entry stage=0 tensor=2 idx=0 subbatch_offset=3 full=1
HIT worker-done stage=0 export0_offset=4 export0_full0=0
```

The guest progress log stops at:

```text
[prt-progress] c2-export stage=0 tensor=2 phase=copy-end idx=0 subbatch=3 rc=0 bytes=65536
[prt-progress] c2-export stage=0 tensor=2 phase=retire idx=0 rc=0 next_sbatch=4 full=0
```

It does **not** contain the immediately following source line:

```c
PRT_PROGRESS_LOG("worker stage=%u subbatch=%u done", ctx->stage_id, progress_sbatch);
```

That line is `pipeline-runtime/src/prt_runtime.c:4682`. The GDB breakpoint stopped before executing that line, printed `HIT worker-done ...`, then continued. No later C2, worker-done, or GDB prompt was observed.

Breadcrumb summary agrees that the last recorded low-level events are the subbatch3 tensor2 export DMA page completions and final RR release:

```text
last_update_ns=38350835000 update_count=18766 last_slot_idx=23 last_kind=rr last_phase=rr-release-end
mono=38350827000 ... kind=dma phase=dma-page-after-accounting ... sb=3 tensor=2 page=63 rc=0
mono=38350835000 ... kind=rr phase=rr-release-end ... sb=3 mgr=0 rc=0 line=404
```

Heartbeat continued to update after the no-output period:

```text
18113971082, 959
36470725933, 1887
55196678948, 2826
```

This weakens the earlier hypothesis that the whole simulator stopped immediately at target cycle `36470725933`; the run host was still executing FireSim. However, GDB Ctrl-C did not recover a prompt.

## Interpretation

The current narrow frontier is not stage0 subbatch4 compute. The strongest evidence places the frontier at stage0/subbatch3 after C2 retire and before or inside the `worker done` progress-log path.

This is likely an observability perturbation rather than a confirmed compute bug:

- `PRT_PROGRESS_LOG` writes to the guest log file when `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`.
- For a regular file, `O_NONBLOCK` does not provide reliable nonblocking semantics.
- The current code's best-effort `write()` loop can still block in the guest filesystem/block-device path before it returns.
- The stalled line is a hot-path progress log, not a DMA completion check or Gemmini fence.

This does not prove the runtime is otherwise correct. It proves the current live run is too perturbed by guest progress-file logging to distinguish a real pipeline stall from logging/IO backpressure.

## Artifacts

Stored under:

`pipeline-runtime/debug_records/artifacts/20260508T131500Z_sbus64_dummy8x8_gdb_worker_done_log_frontier/`

- `gdb-interactive.log`
- `bertmini-batch8.log`
- `bertmini-batch8.breadcrumb.bin`
- `breadcrumb-summary.txt`
- `bertmini-batch8.gdbserver.log`
- `bertmini-batch8.status`
- `heartbeat.csv`
- `uartlog`
- `runworkload-pane.log`
- `ec2-instance-i-0602fd2657c860372.json`

The run farm was terminated after artifact capture; EC2 state entered `shutting-down`.

## Next Step

Run the same workload with ordinary guest progress-file logging disabled and keep only low-disturbance observability:

- GDB source/function breakpoints for dynamic frontier movement.
- Memory/breadcrumb observation if needed.
- No guest progress-file log in the worker hot path.

If the stall moves past stage0/subbatch3 worker-done, then this record should be treated as evidence that the previous apparent stall was logging-induced. If it still stops at the same source line without guest logging, then re-check GDB stack/PC and nearby control flow.
