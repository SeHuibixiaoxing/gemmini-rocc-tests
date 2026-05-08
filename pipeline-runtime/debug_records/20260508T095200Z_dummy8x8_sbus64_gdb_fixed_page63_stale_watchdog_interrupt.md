# 2026-05-08T09:52Z dummy8x8/sbus64 fixed-load page63 GDB run interrupted by stale watchdog

## Summary

This run used the dummy Gemmini 8x8, 4c12p12, sbus64, cfg32, NIC, no-TraceIO bitstream:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host: `i-0a4626998527363ce`, private IP `192.168.1.35`
- Workload result directory:
  `sims/firesim/deploy/results-workload/2026-05-08--09-38-29-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

The corrected fixed-load marker configuration was used:

- Marker site: `dma-fixed-load-submitwait-begin`
- Segment/global/local/manager/page: `0/0/0/0/63`
- Tensor filter: `PIPELINE_RUNTIME_GDB_MARKER_TENSOR=1000001`
- `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`

The run reached the intended fixed-load path, not the scheduler C1 path. GDB confirmed:

- `site_id=27`
- `local_stage_id=0`
- `manager_id=0`
- `tensor_id=1000001`
- `page_idx=63`
- Backtrace includes `stage_prepare_exec_views()` -> `prt_dma_copy_dram_to_spm_pages_prefix()` -> `dma_copy_host_to_spm_pages_linux()`.

For the selected page, GDB observed:

- `dma_submit_wait_annotated_scoped()` entry for `stage_idx=0`, `tensor_id=1000001`, `debug_page_idx=63`.
- `prt_dma_submit()` entry with `src_addr=0x104a35000`, `dst_addr=0x40006000`, `bytes=1024`, `src_acc=0`, `dst_acc=0`.
- `dma_blocking_wait()` entry with token id `129`, `rr_manager_id=0`, `rr_scope_valid=1`, `rr_scope_external=1`.
- Completion flag pointer `0x5e000` already contained `0x00000001`.
- Before and after `hw_dma_fence()`, `hw_done_flag=1`.
- `dma_gdb_marker_wait_return()` returned `rc=0`.
- Fixed-load `submitwait-end` marker and `page-accounted` marker both hit with `rc=0`.
- Execution reached the fixed-load `dma_batch_scope_release()` call-site.

This means the chosen fixed-load page63 DMA did not hang in submit, wait, the local fence, shared fence, or page accounting. The next unresolved boundary is the batch/RR scope release and return to `stage_prepare_exec_views()`, but this run did not prove a hang there because the run host was externally terminated.

## Interruption root cause

The GDB transcript ended with:

```text
Remote connection closed
```

At the same time, AWS reported the run host in `shutting-down` and then `terminated`, with state transition reason:

```text
User initiated (2026-05-08 09:51:36 GMT)
```

The current run's pane log also showed:

```text
[prt-host-watchdog] no pending/running instances remain for pairbertb8d12s64gdbcfg32nicnt
```

Static host-side inspection found multiple stale local `firesim runworkload` managers and tmux sessions for the same `pairbertb8d12s64gdbcfg32nicnt` run farm tag:

- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-072530`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-083041`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-091224`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-093828`

These stale monitors shared the same run farm tag and could call `terminaterunfarm` against the active instance for a later run. They were killed after confirming there were no remaining running F2 instances. Post-cleanup process evidence is in the artifact directory.

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
  192.168.1.35 172.16.0.2:2345 32345
```

Cleanup after interruption:

```sh
tmux kill-session -t pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-072530
tmux kill-session -t pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-083041
tmux kill-session -t pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-091224
tmux kill-session -t pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-093828
```

Then orphaned local `firesim runworkload` Python processes for this same config/tag were killed.

## Artifacts

Artifacts are under:

`pipeline-runtime/debug_records/artifacts/20260508T095200Z_dummy8x8_sbus64_gdb_fixed_page63_stale_watchdog_interrupt/`

Key files:

- `pairdummy-cfg32-marker-stop.gdb.log`
- `pairdummy-cfg32-marker-stop.gdb`
- `fixed-load-page-frontier.gdb`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-093828.pane.log`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-093828.host-watchdog.log`
- `aws_instance_status.json`
- `post_cleanup_processes.txt`

## Next step

Rerun the same fixed-load frontier GDB test now that stale monitors have been cleared. The next helper should avoid the ambiguous `tbreak dma_batch_scope_release` multi-location breakpoint and instead use source-line breakpoints around:

- `prt_dma.c:905`
- `prt_rerocc.c:388`
- `prt_rerocc.c:404`
- `prt_dma.c:906`
- `prt_runtime.c:2231`
- `prt_runtime.c:2240`

This should disambiguate whether `prt_rr_release_scope()` returns and whether `stage_prepare_exec_views()` resumes after fixed-load copy.
