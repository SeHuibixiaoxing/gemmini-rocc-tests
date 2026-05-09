# 20260509T193535Z - no-DMA compute full pass on cfg32 gdbserver run

## Context

- Date: 2026-05-09 UTC
- Purpose: complete the requested no-DMA compute bisection on the current
  cfg32/NIC/gdbserver F2 target before returning to the real DMA path.
- AGFI: `agfi-077451484fe3b63c3`
- Run host: `i-096d267497e53747f`, private IP `192.168.1.240`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`

## Guest Env

The tested image had the low-noise gdbserver profile plus:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=4
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=2
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
```

`debug-preflight` reported probe tier 1: no breadcrumb, guest deep log, debug
trigger, fixed-load probe, or export probe was enabled. Local and remote image
freshness matched. The effective `/firemarshal.env` SHA was:

```text
589df50233a35adcbc521144a5972ed60adefbabc7f3062a396ceb8ac0df639f
```

## Result

The run completed naturally:

```text
Simulation complete.
*** PASSED *** after 22734035102 cycles
Script done on 2026-05-09 19:35:35+00:00 [COMMAND_EXIT_CODE="0"]
```

The FireSim runworkload tmux exit code was also `0`.

Result directory:

```text
sims/firesim/deploy/results-workload/2026-05-09--19-15-36-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/
```

Archived evidence:

```text
pipeline-runtime/debug_records/artifacts/20260509T1935_no_dma_full_pass_cfg32_gdbserver/
```

## GDB Notes

Two gdbserver handling lessons came out of this run:

- The first attach attempt used a stale `uartlog` listening line from an earlier
  run. The valid wait condition must filter for the current `Script started`
  block before accepting `[gdbserver] phase=listening`.
- A manual local `SIGINT` to the GDB process group disconnected the target
  instead of producing a backtrace. For future short-timeout frontier probes,
  set `PRT_GDB_MARKER_TIMEOUT` low before launch and let the expect harness send
  its own Ctrl-C.

The disconnected GDB session did not invalidate the no-DMA result: after the
disconnect, the guest continued and the FireSim simulation reached the normal
PASS marker.

Related archived failed attach evidence:

```text
pipeline-runtime/debug_records/artifacts/20260509T1936_no_dma_stage2_marker_gdb_disconnect_stale_uart/
```

## Conclusion

This is the strongest current bisection result: on the same cfg32/NIC/noTrace
AGFI, the no-DMA compute path can complete the full bertmini batch8 pairdummy
workflow. The active blocker should therefore be pursued in the real DMA path:
DMA submission/completion, blocking fence/wait authority, host-buffer/direct
DMA, or producer publication that depends on real DMA, not artifact reading,
Gemmini compute, SPM xlate, or generic no-DMA pipebuf control flow.
