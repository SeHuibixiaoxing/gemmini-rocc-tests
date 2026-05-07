# 20260507T190350Z - dummy8x8 sbus64 token2 helper abort on optimized-out local

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Run host: `i-06d9a19446b224da2`, private IP `192.168.1.226`.
- Workload results dir: `sims/firesim/deploy/results-workload/2026-05-07--18-49-32-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

## Command

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=2400 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_token2_frontier.sh \
  192.168.1.226 172.16.0.2:2345 32345
```

## Result

The run reached:

1. `segment-begin` marker for segment 0.
2. `dma-wait-return` marker for segment 0, local stage 0, tensor 0, page 0, token 1, rc 0.
3. `prt_dma.c:3832`, immediately after `dma_gdb_marker_wait_return()`.
4. `prt_dma.c:2092`, immediately after `prt_dma_wait()` returned to `dma_submit_wait_annotated_scoped()`.

The helper then aborted before token2 because GDB selected an inlined/optimized-out frame at `prt_dma.c:2092`; `print tok.id` failed with:

```text
value has been optimized out
```

The abort is a host-side helper failure, not evidence that token2 failed. A reconnect attempt with GDB to the same `gdbserver --once` session failed with:

```text
Remote communication error. Target disconnected: Connection reset by peer.
```

This means the current live session is no longer useful for continuing the token2 test.

## Evidence

Artifacts are archived under:

`debug_records/artifacts/20260507T190350Z_dummy8x8_sbus64_token2_helper_optimized_out_abort/`

Important files:

- `gdb/marker-stop-token1-abort/pairdummy-cfg32-marker-stop.gdb.log`
- `gdb/resume-after-abort-reset-by-peer/resume-after-2092.gdb.log`
- `guest/uart-tail.txt`
- `guest/debugfs-combined.txt`

## Follow-up

The helper was hardened in commit `f4f7fc0` by replacing fragile `print <local>` commands with `info args` / `info locals` and selecting the non-inlined frame before dumping locals at `prt_dma.c:2092`.

Next run should restart the workload and re-run the hardened helper from a fresh `gdbserver --once` session.
