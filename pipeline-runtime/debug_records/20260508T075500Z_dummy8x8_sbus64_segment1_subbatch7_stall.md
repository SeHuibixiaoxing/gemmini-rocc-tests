# 2026-05-08T07:55Z - dummy8x8 sbus64 gdbserver run stalls at segment 1 stage 0 subbatch 7

## Scope

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-0e7c767e2f3ca0915`, private IP `192.168.1.179`
- Results dir:
  `sims/firesim/deploy/results-workload/2026-05-08--06-51-20-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Artifact snapshot:
  `pipeline-runtime/debug_records/artifacts/20260508T075500Z_dummy8x8_sbus64_segment1_subbatch7_stall/`

## Preceding pass

The same live run first passed the synthetic model prefault GDB marker test:

- `target remote` reached `prt_gdb_marker_stop`.
- marker site was `synthetic-model-prefault-begin`.
- GDB advanced prefault through page thresholds and reached `after-prefault`.
- GDB detached cleanly and the inferior continued.

That result is recorded separately in:

- `debug_records/20260508T070107Z_dummy8x8_sbus64_gdbserver_prefault_marker_pass.md`

## Negative result

After GDB detach, runtime continued into execution but stopped making forward
progress. A 35 second repoll showed no new guest log lines and no heartbeat
advance.

Stable log frontier:

```text
[prt-progress] stage-exec-views phase=end segment=1 stage=0 layer=1 rc=0 flush=1
[prt-progress] worker stage=0 subbatch=7 begin op=1 acc=0 dma=0 tiles=8
[prt-progress] conv-sync-strided stage=0 mgr=0 begin oc=32 out_dim=256x1 in_dim=256x1 in_stride=256 weight_stride=256 out_stride=256
...
[prt-progress] conv-sync-strided stage=0 mgr=7 flushed use_pointwise=1
[prt-progress] rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 csr=0x810 wdata=0x100
...
[prt-progress] rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1
[prt-progress] rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 csr=0x810 wdata=0x100
...
[prt-progress] rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1
```

There is no later:

- `worker stage=0 subbatch=7 compute-done`
- `worker stage=0 subbatch=7 done`
- runtime exit status

Subbatch 6 in the same segment completed, so this is not a generic inability to
enter segment 1 stage 0.

## DMA completion constraint check

This run did not use doneflag polling as DMA completion evidence.

The captured log includes:

```text
[prt-progress] init dma-backend begin backend=0
[prt-progress] dma-backend using blocking_fence completion via hw_dma_fence + rr_fence_scope
[prt-progress] init dma-backend end
```

Static code check also shows `dma_blocking_wait_poll_timeout_enabled()` returns
`0`, so the Linux blocking wait path cannot bypass `hw_dma_fence()` through
doneflag polling in the current source. Existing historical artifacts that show
`dma-wait-doneflag-poll phase=done` remain invalid DMA completion evidence, but
they are not the behavior of this run.

## Current interpretation

The earlier synthetic prefault blocker is ruled out. The current frontier is in
the segment 1 stage 0 worker, subbatch 7, after the eight pointwise conv manager
dispatches have at least reached `flushed use_pointwise=1`.

The last visible `opcode=2` RR acquire lines are not enough to prove the stall is
inside RR acquire: both visible acquire calls returned `rc=0`. Because marker and
raw logs are mostly routed to the deep log path and the current run used coarse
logging, the main log cannot distinguish among these nearby windows:

- late `prt_gemm_conv_run()` return path after the pointwise calls,
- `worker-export-sync` after `prt_gemm_conv_run()` has returned,
- an export/DMA helper that takes an opcode 2 scope before `compute-done`.

The next run should therefore use GDB as the primary frontier tool instead of
adding broad text logs.

## Next target

Restart with a marker at:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=7
```

After that marker hits, keep the same GDB session and set software breakpoints at
the following boundaries:

- `prt_gemm_conv_run` return / `worker-export-sync` marker boundary, to decide
  whether compute returned.
- `conv_call_for_manager_sync_strided` around the pointwise call, RR fence,
  `gemmini_fence`, and `flush_scope_after_drain`.
- `sync_stage_export_aliases`, `prt_dma_submit`, `dma_blocking_wait`, and
  `dma_gdb_marker_wait_return`, to decide whether the visible opcode 2 acquire
  belongs to export/DMA after compute.

Do not probe `172.16.0.2:2345` with `nc`, `curl`, or `telnet`; the first TCP
client to the next `gdbserver --once` listener must again be GDB.
