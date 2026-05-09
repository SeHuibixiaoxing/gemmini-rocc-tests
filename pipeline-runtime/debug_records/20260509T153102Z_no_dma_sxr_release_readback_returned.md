# 20260509T153102Z - no-DMA SPM xlate cfg31 release readback returned under GDB

## Goal

Use the requested no-DMA compute bisection path with `gdbserver` to split the
previous frontier:

```text
segment=1 local_stage=0 subbatch=3
spm-xlate-release mgr=6 cfg=31 phase=release-begin prev_opc3=0x1
```

into the two instructions inside `prt_rr_release_scope()`:

```c
rr_release(scope->cfg_id);
(void)rr_read_csr(CSR_RRCFG0 + scope->cfg_id);
```

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- Run host: `i-0e8c4fa4ed3ee2558`, private IP `192.168.1.93`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- RISC-V runtime SHA256:
  `ed592ca0530050c8bec12c4dbecf47879989f63a91286cb6ae92f336a3479850`
- Local/remote image SHA256:
  `1148ed6d91b2d29915424293e0ee7ac13ef2dbcc9a5052c9908538c67ce8b496`
- Guest env:
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`
  - `PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1`
  - `PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task`
  - `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1`
  - `PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0`
  - `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=0`

## Commands

The F2 workflow used:

```bash
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch

... infrasetup ...
... run 192.168.1.93 ...
```

GDB was the first TCP client to `gdbserver --once`:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_sxr_release.sh \
  192.168.1.93 172.16.0.2:2345 32345
```

No `nc`, `curl`, `telnet`, or port probe was used against the guest
`172.16.0.2:2345` endpoint.

## Evidence

Artifacts were copied to:

```text
pipeline-runtime/debug_records/artifacts/20260509T1520_no_dma_sxr_release_returned_gdb_error/
```

Key files:

- `gdb/pairdummy-cfg32-marker-stop.gdb.log`
- `gdb/pairdummy-cfg32-marker-stop.gdb`
- `runhost/uartlog`
- `runhost/bertmini-batch8.status`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-151153.pane.log`
- `local/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-terminaterunfarm-20260509-152337.pane.log`
- `SHA256SUMS`

## Observations

The marker hit exactly the intended no-DMA window:

```text
site_id = 23
segment_idx = 1
global_stage_id = 1
local_stage_id = 0
subbatch_id = 3
```

The target release scope was:

```text
valid = 1
cfg_id = 31
stage_id = 4294967295
manager_id = 6
opcode_id = 3
```

GDB then proved:

```text
--- rr_release(cfg31) write returned ---
...
--- about to read CSR_RRCFG31 after release ---
...
--- release readback returned; cfg31 release path is not the stuck instruction in this run ---
```

The disassembly confirms the relevant instructions:

```text
0x2a2ca <prt_rr_release_scope+144>: jal 0x294bc <rr_swap_csr>
0x2a2dc <prt_rr_release_scope+162>: lw a0,4(s1)
...
0x2a2ea <prt_rr_release_scope+176>: ...
```

Therefore the previous text-log frontier at `release-begin` is no longer
precise enough to blame either `rr_release(cfg31)` or the same-cfg
`rr_read_csr(CSR_RRCFG31)` readback.

## Limitation

The helper returned `gdb_rc=1` after the useful evidence because its final
temporary breakpoint condition referenced `scope`, which was optimized out at
the line-table stop for `scope->valid = 0`. The target did not reach the helper
`detach` command, so the run was terminated after artifact collection rather
than reused.

This does not invalidate the positive evidence above: the breakpoint at the
post-readback source line was reached.

## Updated Static Window

After `prt_rr_release_scope()` returns, `prt_spm_xlate_release_scope()` still
does:

```c
prt_spm_xlate_trigger_note("rst-b", scope, manager_id, cfg_id, PRT_OK);
...
PRT_PROGRESS_LOG("spm-xlate-release ... phase=release-end ...");
...
PRT_PROGRESS_LOG("spm-xlate-release ... phase=restore-begin ...");
rr_restore_opcode_binding(3U, prev_binding);
...
PRT_PROGRESS_LOG("spm-xlate-release ... phase=restore-end ...");
```

The next hardware-relevant candidate is now `rr_restore_opcode_binding(3U,
prev_binding)`, which writes `RROPC3` via `rr_write_csr()`.

## Cleanup

The F2 runfarm was terminated:

```text
i-0e8c4fa4ed3ee2558 -> terminated
```

A follow-up AWS query showed no active `f2.*` instances. Stale local
`firesim runworkload` manager processes and the runworkload tmux session were
removed after the instance reached `terminated`.

## Next Step

Use the patched GDB ladder to continue from the same marker through:

```text
release readback returned
release-end
restore-begin
rr_restore_opcode_binding(3, prev_binding)
restore-end
prt_gemmini_spm_xlate_flush return
```

Do not broaden guest logging before this narrower GDB follow-up. `doneflag` was
not used as pass/fail or DMA completion evidence in this run.
