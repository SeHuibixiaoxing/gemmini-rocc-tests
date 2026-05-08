# 2026-05-08T14:31Z - sbus64 dummy8x8 GDB-only post-segment frontier and GDB disconnect

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
- Run host: `i-05f77b48d7369dab4`, private IP `192.168.1.177`
- Runworkload tmux:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-140719`

## Setup

This rerun followed `20260508T135842Z_sbus64_dummy8x8_gdbonly_live_stage0_subbatch7_watchdog.md`.
The only intended change was operational: the FireSim host watchdog timeout was
raised so interactive GDB stops would not be killed by the old 600-second idle
limit.

```bash
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
FIRESIM_RUNWORKLOAD_WATCHDOG_SECONDS=21600 \
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.177
```

The remote image freshness check passed:

```text
local_sha256=6798addb95c2cb687b787574438ce8ced317ecbe1682288ff61dd9466f429c1c
remote_sha256=6798addb95c2cb687b787574438ce8ced317ecbe1682288ff61dd9466f429c1c
profile=pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly
```

Guest file logs, breadcrumb, periodic sync, audit/deep/checkpoint logs, DMA
export probes, and child proc diag remained disabled. The evidence below is
from live GDB stops, with UART used only for gdbserver state and cleanup status.

## Breakpoint Strategy

To reduce GDB perturbation, this run did not stop at early worker-loop sites.
Initial breakpoints were only:

```gdb
break prt_runtime.c:4633 if b && b->subbatch_offset >= 7
break prt_runtime.c:4682 if export_bufs[0] && export_bufs[0]->subbatch_offset >= 7
break prt_runtime.c:4688 if ctx && ctx->stage_id == 0
```

After stage0 exit, these were disabled and high-level breakpoints were added:

```gdb
break prt_runtime.c:5419
break prt_runtime.c:5493
break main.c:342
break main.c:347
```

## Live Result

Confirmed stage0 final-subbatch progress:

```text
Breakpoint worker-done:
  ctx->stage_id = 0
  export_bufs[0]->subbatch_offset = 7
  export_bufs[0]->full[0] = 0
  export_bufs[0]->state_epoch = 14
  export_bufs[0]->cmd_running[0] = 0
  export_bufs[0]->dma_token_live[0] = 0

Breakpoint C2:
  ctx->stage_id = 0
  b->subbatch_offset = 7
  idx = 0
  b->full[idx] = 1
  b->state_epoch = 15
  b->cmd_running[idx] = 0
  b->dma_token_live[idx] = 0
  b->dram_base_addr[idx] = 274724643840

Breakpoint worker-done after C2 return:
  export_bufs[0]->subbatch_offset = 8
  export_bufs[0]->full[0] = 0
  export_bufs[0]->state_epoch = 16
```

Stage0 worker exit also hit:

```text
ctx->stage_id = 0
ctx->stop = 1
rt->fatal_error = 0
rt->stop_requested = 1
```

The main thread then hit the segment complete site:

```text
prt_runtime.c:5419
PRT_PROGRESS_LOG("segment=%u threaded backend complete target_subbatch=%u", ...)
rt->fatal_error = 0
rt->stop_requested = 1
```

This proves:

- stage0 subbatch7 C2 returned;
- stage0 reached subbatch offset 8;
- stage0 worker exited normally with no fatal error;
- `prt_runtime_run()` reached the segment complete branch at line 5419.

After continuing from line 5419, no high-level breakpoint at `runtime end`,
`main runtime-run-end`, or `runtime_destroy` hit within about 60 seconds.
Ctrl-C did not return a stack on the first wait. A second Ctrl-C produced:

```text
^CDisconnected from target.
```

A controlled GDB reconnect attempt through a fresh SSH tunnel failed:

```text
Remote debugging using :32351
Remote communication error.  Target disconnected: Connection reset by peer.
No threads.
The program is not being run.
```

UART stayed at:

```text
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=216
```

and heartbeat stayed at:

```text
18102140819, 958
```

## Interpretation

The current live frontier is no longer stage0 compute/C2/worker exit. Under
GDB-only observation, those all passed.

The strongest current frontier is:

```text
prt_runtime_run()
  after segment complete at prt_runtime.c:5419
  before runtime end at prt_runtime.c:5493
  before main.c:342 runtime-run-end
```

The exact line inside that interval is not yet known because Ctrl-C disconnected
the GDB session before a stack could be collected. The next run should avoid
manual interrupt as the primary locator and instead place breakpoints at the
individual post-segment cleanup steps:

- `runtime_release_topology(rt)` around `prt_runtime.c:5423`
- `prt_action_release(rt, &action)` around `prt_runtime.c:5424`
- `prt_runtime_clear_thread_action(rt)` around `prt_runtime.c:5426`
- the outer post-loop cleanup at `prt_runtime.c:5434-5440`
- `runtime_assert_page_allocator_idle(rt, "post_run_release")` at `prt_runtime.c:5440`
- `prt_trace_run_end(rt)` / `prt_trace_dump(rt)` at `prt_runtime.c:5468-5471`
- final `runtime_release_topology` / `prt_action_release` / page allocator idle
  around `prt_runtime.c:5478-5484`

Important caveat: after GDB disconnected, the inferior may have been left stopped
or gdbserver may have terminated its connection state. The frozen heartbeat after
disconnect is therefore not by itself proof of a post-segment software deadlock.
The proof is limited to the line interval reached before disconnect and the lack
of subsequent high-level breakpoint hits before the failed interrupt.

## Cleanup

Artifacts were copied before terminating the run:

```text
pipeline-runtime/debug_records/artifacts/20260508T143100Z_sbus64_gdbonly_post_segment_disconnect/
```

Included:

- `uartlog.txt`
- `heartbeat.csv`
- `runworkload.pane.log`
- `host-watchdog.log`
- `reconnect-gdb.log`
- `reconnect-tunnel.log`

The run farm was then terminated with:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

AWS reported `i-05f77b48d7369dab4` in `shutting-down`, and the local runworkload
tmux wrapper for this run was killed after termination to avoid stale watchdog
state in the next run.
