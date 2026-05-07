# 20260507T120514Z dummy8x8 sbus64 exact token546 watchdog termination

## Purpose

Try to re-run the token-546 `dma_blocking_wait()` path trace after the
token-540 batch2 control run completed cleanly.

## Hardware and workload

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy8x8, `4c12p12`, `sbus64`, `cfg32`, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `192.168.1.71`
- EC2 instance: `i-0b35baea158234d9c`
- Workload actually launched: `--batch 8`

The intended reduced-batch override was not present in the captured
`runworkload` command file. This run must therefore be treated as a batch8
attempt, not as a batch2 reproducer.

## GDB command

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_CONDITION='tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546' \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_POST_HIT_MODE=path_trace \
PRT_GDB_PATH_TRACE_TIMEOUT=120 \
PRT_GDB_PATH_TRACE_MAX_STOPS=10 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.71 172.16.0.2:2345 32345
```

No `nc`, telnet, curl, browser, or other probe touched guest
`172.16.0.2:2345`; host GDB was the first TCP client.

## GDB result

GDB connected and set the conditional breakpoint:

```text
Breakpoint 1 at 0x1552e:
  file .../pipeline-runtime/src/prt_dma.c, line 3506
  stop only if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546 (host evals)
```

It never reported a breakpoint hit before the remote connection closed:

```text
Continuing.
Remote connection closed
```

The helper exited with:

```text
timeout waiting for DMA frontier breakpoint
```

This is not evidence that token 546 was skipped. The run host was terminated by
the FireSim-side live watchdog before the GDB frontier was reached.

## FireSim/watchdog result

The runworkload tmux session did not exit normally before manual stale-session
cleanup. Its host watchdog log shows the actual termination source:

```text
[prt-host-watchdog] host idle timeout reached after 1065s (hb_idle=626s)
[prt-host-watchdog] calling terminaterunfarm --forceterminate
```

The AWS state reason was therefore the expected FireSim termination path:

```text
Client.UserInitiatedShutdown: User initiated shutdown
```

The SSH tunnel closed because the run host was terminated:

```text
Connection to 192.168.1.71 closed by remote host.
```

## Captured context

The watchdog artifacts show this run had not reached the DMA frontier.

Breadcrumb decode:

```text
update_count=182
last_kind=runtime
last_phase=runtime_init_done
last_update_ns=5172930000
```

`triage_prt_capture.py` reports:

```text
frontier=spm-xlate-release mgr=11 cfg=31 phase=restore-end
sparse_dma_export_wait: n/a
sparse_dma_fixed_wait: n/a
```

Sparse log tail:

```text
[prt-progress] yaml pipeline parse line=1031 indent=2 text=stages:
[prt-progress] yaml pipeline parse line=1032 indent=2 text=- - accUtil: 4
[prt-progress] yaml pipeline stage-push line=1032 segment=8 local_stage=0 acc=4
[prt-progress] yaml pipeline parse line=1033 indent=6 text=dramBypassList:
[prt-progress] yaml pipeline parse line=1034 indent=6 text=- [1, 1, 1, 1]
[prt-progress] yaml pipeline parse line=1035 indent=6 text=entryTensorDoubleBufferList: [0, 0]
[prt-progress] yaml pipeline parse line=1036 indent=6 text=entryTensorIdList: [27, 26]
[prt-progress] yaml pipeline parse line=1037 indent=6 text=entryTensorTypeList: [DRAM, DRAM]
```

The guest reached `gdbserver` and started the inferior, but there is no
captured `dma_blocking_wait()` context and no token-546 token dump.

## Interpretation

This run is inconclusive for the token-546 DMA hang:

- it was batch8, not the intended batch2 reduced reproducer;
- the conditional breakpoint was set at `dma_blocking_wait()`, but no hit was
  observed;
- the watchdog killed the run due to lack of guest-visible file progress;
- captured breadcrumb/sparse state is still in initialization/YAML parse, not
  in export DMA wait.

The next clean reproducer should:

1. Verify `TARGET_BATCH=2` with `workflow.sh show` before launch.
2. Confirm the `runworkload` command file contains `TARGET_BATCH=2` or the
   rendered guest command contains `--batch 2`.
3. Avoid relying only on a late exact conditional breakpoint while the program
   is still in init. First use a broader early breakpoint or live guest logs to
   confirm segment0 execution has begun.
4. Raise or disable the live idle timeout only for the narrow run, because the
   expected GDB wait can suppress guest file progress.

## Artifacts

```text
pipeline-runtime/debug_records/artifacts/20260507T120514Z_dummy8x8_sbus64_batch8_exact_token546_watchdog/
```

Key files:

- `local/expect-driver.stdout`
- `local/expect-driver.stderr`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-115652.host-watchdog.log`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-115652.command.sh`
- `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-115652-192.168.1.71-host-watchdog-20260507T122331Z.guest-sparse-log.txt`
- `breadcrumb.decoded.txt`
- `breadcrumb.all.decoded.txt`
- `triage.txt`
