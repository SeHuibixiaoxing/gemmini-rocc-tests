# 20260509T123908Z - sbus64 gdbonly stage1 flush pre-entry disconnect

## Goal

Continue the `dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` live-GDB path with
minimum guest logging. The intent was to re-enter the known
`segment=2/global_stage=3/local_stage=1/subbatch=0` window and follow
`build_stage_task_desc()` through the fixed-load and SPM xlate flush boundary.

This round followed the gdbserver-first debugging order from
`docs/testing/gdbserver_integration_sop.md`. It did not add new guest-side
runtime logs.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Run host: `192.168.1.182`
- Instance: `i-01f173a8cac50fee1`
- Binary SHA256:
  `152443db8982e3b056f8e417861efa10f4c850d3cc3ad74d685a9047631f46c6`
- Evidence directory:
  `tmp/firesim-aws-f2/gdbserver-tests/live-20260509-1214-gdbonly-seg2-stage1/`
- Key evidence files:
  - `gdb.log`
  - `host-watchdog.log`
  - `runworkload.pane.log`
  - `runhost/uartlog`
  - `runhost/heartbeat.csv`

The workload profile was the low-noise gdb-only profile:

```text
NO_DMA=0
GDBSERVER=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0
guest sparse/deep/audit/checkpoint/breadcrumb/periodic sync disabled
```

GDB was the first TCP client to the guest `gdbserver --once` endpoint.

## Confirmed Observations

TLS-based conditional breakpoints are not reliable for this target. A condition
that touched `g_prt_debug_tls_state` failed with:

```text
Remote target failed to process qGetTLSAddr request
```

The useful entry condition was a function-argument breakpoint:

```text
break prt_debug_state_set_worker if segment_idx==2 && global_stage_id==3 && local_stage_id==1 && subbatch_id==0 && phase_id==4
```

This hit Thread 5 at:

```text
segment=2 global_stage=3 local_stage=1 subbatch=0 phase=GEMM_PREP
```

The live call ladder reached:

```text
build_stage_task_desc(stage_id=1)
  -> build_stage_conv_desc(stage_id=1)
  -> stage_prepare_exec_views(stage_id=1, layer.index=3, layer.type="conv")
  -> prt_dma_copy_dram_to_spm_pages(stage_idx=1, tensor_id=1000006, manager_id=4)
```

The fixed tensor `1000006` load completed for all 8 observed pages. The
`dma_gdb_marker_token` breakpoint reported `rc=0`, `done=1`,
`hw_done_flag=1`, and `status=0`; observed token ids were in the
`5575..5589` range for pages 0 through 7.

The next confirmed source frontier was:

```text
runtime_flush_stage_spm_xlate(stage_id=1)
  prt_runtime.c:458
```

Current ELF address notes:

```text
runtime_flush_stage_spm_xlate inline stop: 0x2dc26
call runtime_collect_stage_gemmini_mgrs: 0x2dc6e
call prt_gemmini_spm_xlate_flush: 0x2dc8c
prt_gemmini_spm_xlate_flush entry: 0x2a768
rerocc_gemmini_spm_xlate_flush instruction: 0x2a7fc
after flush instruction: 0x2a800
return to stage_prepare_exec_views line 2341: 0x2dc32
prt_trace_on_gemm_issue lock call: 0x2ef24
```

Do not reuse older hard-coded addresses such as `0x3054e` with this ELF.

## Important Ambiguity

This run did **not** prove that execution hung inside
`runtime_collect_stage_gemmini_mgrs()` or before
`prt_gemmini_spm_xlate_flush()`.

After stopping at `runtime_flush_stage_spm_xlate()` the session armed
thread-specific breakpoints at the flush entry, the after-instruction address,
and the return address. On `continue`, GDB reported:

```text
Thread-specific breakpoint ... deleted - thread 5 no longer in the thread list.
Disconnected from target.
```

No `[sxf-entry]`, `[sxf-after-insn]`, or return breakpoint was observed before
the disconnect. This means the reliable frontier is "stopped at the inline
`runtime_flush_stage_spm_xlate()` entry before continuing"; the post-continue
disconnect must not be over-interpreted as a precise PC-level hardware stall.

The host watchdog continued to see the simulator alive and later heartbeat
progress, but guest file logs were disabled. UART showed the run only up to
`[gdbserver] phase=inferior`, so it cannot refine the runtime PC.

## Historical Comparison

The earlier record
`debug_records/20260509T061047Z_sbus64_live_gdb_build_task_flush_frontier.md`
used the same AGFI route and proved a stronger sub-window:

```text
runtime_flush_stage_spm_xlate(stage_id=1)
  -> prt_gemmini_spm_xlate_flush(manager 4)
  -> prt_gemmini_spm_xlate_flush(manager 5)
  -> prt_gemmini_spm_xlate_flush(manager 6)
  -> prt_gemmini_spm_xlate_flush(manager 7)
  -> return to stage_prepare_exec_views()
```

Therefore the next run should first check whether the current disconnect is a
GDB control/artifact issue or a real regression before changing runtime code.

## Lessons

- Avoid TLS conditional breakpoints on this remote target.
- Prefer argument-based conditions or explicit instruction addresses.
- Treat thread-specific breakpoint deletion as a separate GDB/inferior state
  observation, not as proof of the previous source line blocking.
- Keep broad DMA and helper breakpoints disabled after their path is ruled out;
  they add stop noise and can interfere with `advance`.
- Do not use doneflag polling as DMA completion evidence. The valid evidence in
  this round was live return from `prt_dma_wait` plus `hw_done_flag/status`
  telemetry at the GDB token marker.

## Next Step

Before another F2 run, inspect the current ELF and source locally and prepare a
narrower live-GDB ladder that avoids TLS and uses current addresses only:

```text
target prt_debug_state_set_worker(... GEMM_PREP)
break *0x2dc66  # before/around manager collection setup
break *0x2dc72  # after runtime_collect_stage_gemmini_mgrs returns
break *0x2dc88  # before loading manager id for flush loop
break *0x2dc8c  # call prt_gemmini_spm_xlate_flush
break *0x2a768  # flush wrapper entry
break *0x2a7fc  # exact custom instruction
break *0x2a800  # after custom instruction
break *0x2dc32  # return to stage_prepare_exec_views after flush
```

Recompute these addresses if the ELF changes.
