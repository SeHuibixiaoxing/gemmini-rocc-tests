# 2026-05-08T10:36Z dummy8x8/sbus64 post-detach sink-progress stall capture

## Summary

This record captures the live state after the fixed-load page63 GDB frontier
test detached.

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, 4c12p12, sbus64, cfg32, NIC, no TraceIO
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Run host: `i-0e335f0087f382ab7`, private IP `192.168.1.137`
- Result directory:
  `sims/firesim/deploy/results-workload/2026-05-08--10-17-45-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

The guest status still reported:

```text
state=running
exit_code=
```

The guest sparse log had not advanced beyond:

```text
[prt-progress] stage-fixed-load-sparse phase=end segment=0 stage=0 slot=0 tensor=1000000 rc=0 pages=1 bytes=1024 lazy=0 reuse=0 dma=0
[prt-progress] stage-fixed-load-sparse phase=begin segment=0 stage=0 slot=1 tensor=1000001 pages=64 bytes=65536 lazy=0 reuse=0 dma=0
[prt-progress] rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1
[prt-progress] segment=0 sink-progress=0/8 elapsed_ms=1053 fatal=0 stop=0
```

The host watchdog confirmed no sparse/uart progress for several minutes while
the FireSim heartbeat continued to advance. This points away from an FPGA or
host run-farm death and toward a guest software/hardware interaction stall in
the worker path.

## Interpretation

The previous GDB transcript proved that fixed-load tensor `1000001` page 63
returned through `prt_dma_submit()`, the blocking wait path, and RR scope
release, then stopped at the `stage-fixed-load-sparse phase=end` log call-site
with GDB locals:

- `slot=1`
- `tensor_id=1000001`
- `local_bytes=65536`
- `rc=0`
- `need_flush=1`

However, this capture shows that the corresponding slot-1 end log did not land
in the sparse log after detach. Therefore the next frontier is narrower than
"after fixed-load": it begins at the slot-1 end log call-site and the immediate
`need_flush` path:

- `PRT_PROGRESS_LOG("stage-fixed-load-sparse phase=end ...")`
- `prt_spm_bind_vpages_ctx()`
- `runtime_flush_stage_spm_xlate()`
- return from `stage_prepare_exec_views()`
- `build_stage_conv_desc()` / `build_stage_task_desc()`
- `worker-after-build-stage-task`
- first `prt_process_c2()` export for tensor `2`

The GDB transcript also hit `dma_copy_spm_pages_to_host_linux()` for
`tensor_id=2`, so export may become active close to this boundary.

## Cleanup

The run farm was terminated after evidence capture:

```text
terminaterunfarm log:
sims/firesim/deploy/logs/2026-05-08--10-36-56-terminaterunfarm-5V401LKIBU6C5UN7.log

AWS state:
i-0e335f0087f382ab7 shutting-down
User initiated (2026-05-08 10:36:57 GMT)
```

No running `f2.*` instances were found immediately after termination.

## Artifacts

Artifacts are under:

`pipeline-runtime/debug_records/artifacts/20260508T103605Z_dummy8x8_sbus64_postdetach_sink_progress_stall/`

Key files:

- `guest-sparse-log.snapshot.txt`
- `guest-status.snapshot.txt`
- `uartlog.tail.txt`
- `host-watchdog.log`
- `runworkload.pane.log`
- `aws_instance_status_before_terminate.json`
- `aws_instance_status_after_terminate.json`
- `terminaterunfarm.log`

## Next GDB target

Start a fresh gdbserver workload and stop at `worker-before-build-stage-task`
for segment 0 / stage 0 / subbatch 0. From there, use source-line breakpoints
around:

- `prt_runtime.c:2240`
- `prt_runtime.c:2246`
- `prt_runtime.c:2264`
- `prt_runtime.c:2287`
- `prt_runtime.c:2318`
- `prt_runtime.c:2330`
- `prt_runtime.c:3767`
- `prt_runtime.c:3773`
- `prt_runtime.c:4418`
- `prt_runtime.c:4423`
- `prt_scheduler.c:456`
- `prt_dma.c:912`

The immediate question for the next run is whether the worker stops in logging,
SPM xlate bind/flush, descriptor construction, or the first export DMA path.
