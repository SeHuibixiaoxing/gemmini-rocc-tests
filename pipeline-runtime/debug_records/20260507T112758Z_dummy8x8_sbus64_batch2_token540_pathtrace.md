# 20260507T112758Z dummy8x8 sbus64 batch2 DMA path trace

## Purpose

Validate the new `gdbserver` path-trace helper on the current dummy8x8/sbus64
cfg32 NIC noTrace AGFI, and try to localize the DMA wait card point with a
small batch2 reproducer.

## Hardware and workload

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy8x8, `4c12p12`, `sbus64`, `cfg32`, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Effective override:
  `PAIRDUMMY_SBUS64_TARGET_BATCH=2`
- Mapping cache:
  `PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0`
- Run host: `192.168.1.151`
- EC2 instance: `i-0b1daa9ef7f059771`

## Commands

```bash
PAIRDUMMY_SBUS64_TARGET_BATCH=2 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch

PAIRDUMMY_SBUS64_TARGET_BATCH=2 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup

PAIRDUMMY_SBUS64_TARGET_BATCH=2 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh remote-freshness 192.168.1.151

PAIRDUMMY_SBUS64_TARGET_BATCH=2 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.151
```

GDB first-client command:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_CONDITION='tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 540 && tok->id <= 560' \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_POST_HIT_MODE=path_trace \
PRT_GDB_PATH_TRACE_TIMEOUT=120 \
PRT_GDB_PATH_TRACE_MAX_STOPS=12 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.151 172.16.0.2:2345 32345
```

No `nc`, telnet, curl, browser, or other port probe was used against guest
`172.16.0.2:2345`. Host GDB was the first TCP client.

## Result

The helper connected and returned `PASS`. The path-trace instrumentation is
usable, but this run did not reach the historical token-546 card point because
the window condition stopped at the earliest matching token, token 540.

Token 540 completed cleanly:

```text
token=540 stage=0 tensor=2 manager=0
rr_scope_valid=1 rr_scope_external=1
completion_flag=0x5d000 value=1
src=0x40102c00 dst=0x103d69800 done_pa=0x103264000 bytes=1024
timeout_ns=5000000000
```

Observed path:

```text
dma_blocking_wait entry
0x15670 before poll/fence decision
0x15bf4 poll timeout env check
0x15f56 done-flag poll start, emit_progress=0
0x15ce8 first poll loop
0x15de4 poll done branch
0x15e50 poll returned PRT_OK
0x15688 post-wait completion refresh
0x15882 external rr shared fence
0x159ea dma_token_complete(status=0)
0x15ad4 dma_blocking_wait return
```

This run therefore proves that:

- done-flag polling is active for these export DMA waits;
- token 540 has its completion flag visible before the poll path times out;
- the external `prt_rr_fence_scope()` call can return for token 540;
- token completion and function return are reachable under GDB path tracing.

## Caveat

After token 540 returned, the same breakpoint set caught token 541. The script
detached after the configured 12 path stops while token 541 was at
`dma_blocking_wait_poll_timeout_enabled()`. A read-only debugfs breadcrumb copy
30 seconds later still showed:

```text
last_phase=dma_wait_before_fence token=541 page=26
src=0x40202c00 dst=0x103d69c00 done_pa=0x103264000
```

This is not treated as a functional card point yet because the run was
intentionally interrupted/detached in the middle of token 541 with software
breakpoints installed. The next run must use an exact token-546 condition and
stop tracing at the token-546 return/timeout boundary.

## Follow-up

Next GDB command should use:

```text
tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546
```

and `PRT_GDB_PATH_TRACE_MAX_STOPS=10`, which is enough to trace from
`dma_blocking_wait` entry through the return path for one token without rolling
into token 547.

## Artifacts

```text
pipeline-runtime/debug_records/artifacts/20260507T112758Z_dummy8x8_sbus64_batch2_token540_pathtrace/
```

Key files:

- `local/expect-driver.clean.stdout`
- `local/pairdummy-cfg32-dma-frontier.clean.log`
- `local/remote-after-detach/breadcrumb.2.decoded.txt`
- `tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-*.pane.log`
