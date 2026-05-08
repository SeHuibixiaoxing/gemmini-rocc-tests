# 2026-05-08T10:30Z dummy8x8/sbus64 fixed-load page63 GDB frontier passed

## Summary

This run used the dummy Gemmini 8x8, 4c12p12, sbus64, cfg32, NIC,
no-TraceIO bitstream:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host: `i-0e335f0087f382ab7`, private IP `192.168.1.137`
- Workload result directory:
  `sims/firesim/deploy/results-workload/2026-05-08--10-17-45-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

The corrected fixed-load marker configuration was rerun after removing stale
local `firesim runworkload` managers and watchdogs that shared the same run farm
tag:

- Marker site: `dma-fixed-load-submitwait-begin`
- Segment/global/local/manager/page: `0/0/0/0/63`
- Tensor filter: `PIPELINE_RUNTIME_GDB_MARKER_TENSOR=1000001`
- `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`

GDB reached the expected fixed-load page:

- `site_id=27`
- `local_stage_id=0`
- `manager_id=0`
- `tensor_id=1000001`
- `page_idx=63`
- Backtrace:
  `stage_prepare_exec_views()` -> `prt_dma_copy_dram_to_spm_pages_prefix()` ->
  `dma_copy_host_to_spm_pages_linux()`.

For this page, GDB then stepped through the submit/wait/release frontier:

- Entered `dma_submit_wait_annotated_scoped()` for
  `stage_idx=0`, `tensor_id=1000001`, `debug_page_idx=63`.
- Entered `prt_dma_submit()` with
  `src_addr=0x104a35000`, `dst_addr=0x40006000`, `bytes=1024`,
  `src_acc=0`, `dst_acc=0`.
- Entered `dma_blocking_wait()` with token id `129`,
  `rr_manager_id=0`, `rr_scope_valid=1`, `rr_scope_external=1`.
- Returned from the wait path through `dma_gdb_marker_wait_return(rc=0)`.
- Hit fixed-load `submitwait-end` and `page-accounted` markers with `rc=0`.
- Entered the fixed-load batch scope release call-site.
- Entered `prt_rr_release_scope()` with
  `{valid=1, cfg_id=0, stage_id=0, manager_id=0, opcode_id=2}`.
- Hit the release readback breadcrumb in `prt_rr_release_scope()`.
- Observed the scope as invalid after release readback.
- Returned to `stage_prepare_exec_views()` at the fixed-load end log call-site
  for `tensor=1000001`, `rc=0`.

The known-bad DMA doneflag is not used as pass evidence here. The pass criterion
is that the blocking wait returned `rc=0`, the RR scope release path was reached
and cleared the scope, and execution resumed in `stage_prepare_exec_views()`.

## Conclusion

The current hang is not in the selected fixed-load page63 DMA submit, blocking
wait, local/shared fence path, RR scope release, or return to
`stage_prepare_exec_views()`.

After GDB detached, the guest sparse log continued beyond the fixed-load section
and reached:

```text
[prt-progress] stage-fixed-load-sparse phase=end segment=0 stage=0 slot=0 tensor=1000000 rc=0 pages=1 bytes=1024 lazy=0 reuse=0 dma=0
[prt-progress] stage-fixed-load-sparse phase=begin segment=0 stage=0 slot=1 tensor=1000001 pages=64 bytes=65536 lazy=0 reuse=0 dma=0
[prt-progress] stage-fixed-load-sparse phase=end segment=0 stage=0 slot=1 tensor=1000001 rc=0 pages=64 bytes=65536 lazy=0 reuse=0 dma=0
[prt-progress] segment=0 sink-progress=0/8 elapsed_ms=1053 fatal=0 stop=0
```

The next unresolved frontier is after fixed-load completion, likely in the first
segment worker/export/sink-progress path rather than in fixed-load host-to-SPM
DMA. One notable clue is that the GDB script unexpectedly hit
`dma_copy_spm_pages_to_host_linux()` for `tensor_id=2` before the final
fixed-load end call-site breakpoint; this suggests the export path becomes
active immediately after or concurrently with fixed-load completion and should be
the next breakpoint target.

## Commands

GDB helper:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FIXED_LOAD_STAGE=0 \
PRT_GDB_FIXED_LOAD_MANAGER=0 \
PRT_GDB_FIXED_LOAD_TENSOR=1000001 \
PRT_GDB_FIXED_LOAD_PAGE=63 \
PRT_GDB_MARKER_TIMEOUT=2400 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh \
  192.168.1.137 172.16.0.2:2345 32345
```

## Artifacts

Artifacts are under:

`pipeline-runtime/debug_records/artifacts/20260508T103000Z_dummy8x8_sbus64_gdb_fixed_page63_frontier_pass/`

Key files:

- `pairdummy-cfg32-marker-stop.gdb.log`
- `pairdummy-cfg32-marker-stop.gdb`
- `fixed-load-page-frontier.gdb`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-1016.pane.log`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-1016.host-watchdog.log`
- `uartlog.snapshot.txt`
- `guest-sparse-log.snapshot.txt`
- `guest-status.snapshot.txt`

## Next step

Inspect the live run after GDB detach to locate the new stall boundary. If the
run remains stuck after `segment=0 sink-progress=0/8`, start a fresh GDB-enabled
workload and move the marker/breakpoint set from fixed-load page63 to the
segment-0 export/sink path:

- `sync_stage_export_aliases()`
- `copy_tensor_pages_to_model_alias_target()`
- `dma_copy_spm_pages_to_host_linux()`
- `dma_submit_wait_annotated_scoped()` for export tensor/page filters
- segment sink-progress update sites around stage worker completion
