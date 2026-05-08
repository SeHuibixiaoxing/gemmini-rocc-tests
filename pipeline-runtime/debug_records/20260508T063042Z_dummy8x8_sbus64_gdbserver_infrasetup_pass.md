# 2026-05-08 06:30 UTC: dummy8x8 sbus64 gdbserver infrasetup pass

## Target

- AGFI/AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host: `i-01f19222219df8306`, private IP `192.168.1.211`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

## Command

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin \
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
```

## Result

`infrasetup` completed with exitcode `0`.

Key log lines:

```text
[192.168.1.211] Flashing FPGA Slot: 0 with agfi: agfi-077451484fe3b63c3.
[192.168.1.211] Checking for Flashed FPGA Slot: 0 with agfi: agfi-077451484fe3b63c3.
[192.168.1.211] Preflighting FireSim driver readiness for slot 0 (attempt 1/3).
[192.168.1.211] FireSim driver readiness preflight passed for slot 0.
[firesim-tmux] finished at 2026-05-08T06:30:27Z with exit code 0
```

Full manager log:

```text
sims/firesim/deploy/logs/2026-05-08--06-28-44-infrasetup-QMPP73R79FS3NTPU.log
```

Notes:

- Network target audit passed before infrasetup; the generated target exposes
  both blkdev and NIC.
- The first infrasetup attempt was blocked by local image freshness and was
  repaired before this successful attempt.
- doneflag remains known-bad and was not used as a completion or pass signal.
