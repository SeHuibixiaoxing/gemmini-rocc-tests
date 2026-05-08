# 2026-05-08T23:40Z sbus64 dummy8x8 worker-gemm-run marker timeout

## Scope

- Hardware: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Target: dummy Gemmini 8x8, 4 cores, 12 pairs, sbus64, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- ELF sha256: `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`
- Run host: `192.168.1.193`
- Instance: `i-078eee95603578f2d`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-marker-stop-20260508T232146Z-192_168_1_193-172_16_0_2/pairdummy-cfg32-marker-stop.gdb.log`

## Marker Filter

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PRT_GDB_MARKER_TIMEOUT=900
```

The image freshness check confirmed the active guest environment had guest sparse log, deep log, audit log, checkpoint log, breadcrumb, debug-trigger, DMA export probe, and DMA fixed-load probe disabled. The host waited for UART `[gdbserver] phase=listening`; GDB was the first TCP client to `172.16.0.2:2345`.

## Result

GDB connected and set the `prt_gdb_marker_stop` breakpoint:

```text
Breakpoint 1 at 0x14482: file .../pipeline-runtime/src/prt_debug_state.c, line 396.
```

The marker did not hit before the 900 second helper timeout:

```text
Cannot execute this command while the target is running.
Use the "interrupt" command to stop the target
and then try again.
[pairdummy-gdb-marker] gdb_rc=124
```

This is a negative result, but it is now much narrower than the earlier `worker-gemm-run` miss:

- `worker-before-exports-ready` hit for the same segment/stage/subbatch.
- `stage_wait_exports_ready()` returned `rc=0` for the same segment/stage/subbatch.
- `worker-after-exports-ready` hit for the same segment/stage/subbatch.
- `worker-before-build-stage-task` hit for the same segment/stage/subbatch.
- `worker-after-build-stage-task` hit for the same segment/stage/subbatch with `rc=0`.

Therefore this run does **not** implicate export waits or `build_stage_task_desc()` for this stage/subbatch. The next frontier is after `worker-after-build-stage-task` and before `worker-gemm-run`, or a hard-to-interrupt state reached in that region.

## Static Narrowing

The relevant source window is `stage_worker_main()` after the `worker-after-build-stage-task` marker and before the `worker-gemm-run` marker:

```text
prt_log_gate_set_context(...)
prt_log_gate_allow_deep_logs()
tensor_log_stage_entry_inputs(...)
prt_runtime_trigger_note_worker(..., "wrk-b", ...)
PRT_PROGRESS_LOG("worker stage=%u subbatch=%u begin ...")
gemm_begin_ns = prt_now_ns()
prt_trace_on_gemm_issue(rt)
prt_trace_log_event(... PRT_TRACE_EVT_GEMM_ISSUE ...)
prt_debug_state_set_worker(... PRT_DEBUG_PHASE_GEMM_RUN)
prt_runtime_gdb_marker(PRT_GDB_MARKER_SITE_WORKER_GEMM_RUN, ...)
```

Important compile-time observations from the tested ELF:

- `strings` shows `worker stage=%u subbatch=%u begin op=%u acc=%u dma=%u tiles=%u`.
- `strings` does **not** show `wrkrdy`, `wrk-tr-b`, `wrk-issue`, or `[prt-raw] wrk-gb`.
- This indicates `PRT_PROGRESS_LOG` is compiled in, while `PRT_MARKER_LOG`, raw progress, and hot progress are not present in this ELF.

With the runtime env used in this run:

- `PRT_PROGRESS_LOG` should call the progress path, see guest log disabled, see UART log disabled, and return without writing.
- `tensor_log_stage_entry_inputs()` should return early because both deep and audit logging are disabled.
- `prt_runtime_trigger_note_worker()` should return early because `PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=0`.
- `prt_trace_on_gemm_issue()` still takes `rt->state_lock` briefly.
- `prt_trace_log_event()` still records an in-memory event when `rt->trace_events` is allocated.

The most plausible static suspects in this narrowed window are therefore:

- a wait on `rt->state_lock` inside `prt_trace_on_gemm_issue()`;
- an unexpected non-noop progress path despite guest/UART logs being disabled;
- unexpected trigger/audit initialization or first-use behavior;
- an optimizer/source-line mismatch that makes the marker harder to reach than the source suggests, though the adjacent before/after-build markers were reliable.

## Process Cleanup

The run farm was terminated after helper timeout. Instance `i-078eee95603578f2d` was confirmed `terminated`.

Three stale local `firesim runworkload` tmux command processes from the previous marker runs remained after their F2 instances had already been terminated. They were killed to reduce process pressure:

- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-223051`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-225229`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-231336`

After cleanup there were no matching local GDB/tunnel/runworkload processes and no active `f2.*` or `z1d.*` EC2 instances.

## Next Step

The next GDB run should avoid the marker-only helper for this particular frontier. Use a command file that:

1. breaks at `worker-after-build-stage-task` for the same filter;
2. deletes that breakpoint after the hit;
3. sets a breakpoint on `pthread_mutex_lock`, or a temporary source/address breakpoint just before `prt_trace_on_gemm_issue()`;
4. continues with a bounded timeout;
5. if `worker-gemm-run` does not hit quickly, interrupts and collects `thread apply all bt`, registers, and the current `g_prt_debug_state`/`g_prt_debug_tls_state`.

This should distinguish a real block before `prt_trace_on_gemm_issue()` from a wait on `rt->state_lock` or another hard-to-interrupt region.
