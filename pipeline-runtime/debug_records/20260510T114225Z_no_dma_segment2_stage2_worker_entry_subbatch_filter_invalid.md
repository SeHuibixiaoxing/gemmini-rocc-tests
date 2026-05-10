# 20260510T114225Z - no-DMA segment2/stage2 worker-entry subbatch filter invalid

## Context

This records the follow-up static audit after the
`segment2/global_stage4/local_stage2/worker-entry/subbatch0` gdbserver marker
run timed out.

The run used the cfg32/NIC/noTrace dummy8x8 gdbserver profile with no-DMA
compute enabled:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=4
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=2
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

Artifacts from that invalid run were archived under:

```text
pipeline-runtime/debug_records/artifacts/20260510T1139_no_dma_segment2_stage2_worker_entry_subbatch_filter_invalid/
```

The run farm instance was `i-013c9da514f644cbe` at `192.168.1.144`; termination
was issued and the instance reached `shutting-down`.

## Static Audit

`stage_worker_main()` emits `PRT_GDB_MARKER_SITE_WORKER_ENTRY` before the worker
loop computes `progress_sbatch` and before `prt_debug_state_set_worker()` writes
a concrete subbatch id into thread-local debug state.

Current source shape:

```text
prt_runtime.c:4249 worker-entry marker
  subbatch argument = PRT_DEBUG_U32_NONE

prt_runtime.c:4263 progress_sbatch = progress_stage_sbatch(...)
prt_runtime.c:4266 prt_debug_state_set_worker(..., progress_sbatch, ...)
```

`prt_gdb_marker_note()` replaces a `PRT_DEBUG_U32_NONE` subbatch argument with
`g_prt_debug_tls_state.subbatch_id`. At `worker-entry`, TLS still also contains
`PRT_DEBUG_U32_NONE`, so a marker filter requiring `subbatch=0` cannot match.

## Conclusion

The timeout is an invalid marker configuration result. It does not prove that
segment2/stage2 failed to create a worker thread, failed to enter
`stage_worker_main()`, or failed before C7.

For `worker-entry`, use:

```text
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=any
```

or leave the variable empty/unset. Use exact subbatch filters only for later
worker-loop markers where `progress_sbatch` is already known, such as
`worker-entry-process-return`, `worker-entry-full-return`,
`worker-before-exports-ready`, `worker-after-build-stage-task`, or
`worker-gemm-run`.

## Local Fixes

No runtime behavior was changed and no guest logs were added.

The stage2 C7 gdbserver helper was corrected locally:

- usage now documents `worker-entry` with `subbatch=any`;
- stale source line breakpoints were replaced with current thin boundaries:
  `prt_ring_wait_ready`, `prt_process_c7`, and `prt_runtime.c:4566`.

## Next Step

Before any new F2 run, rebuild/render the known-good-like gdbserver image and
confirm with `show`, `debug-preflight`, and `image-closure` that the effective
guest environment uses `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=any` or an empty
value for the `worker-entry` marker.
