# 20260509T130846Z - sbus64 gdb-only GEMM_PREP miss and SIGINT lost

## Goal

Re-enter the known `dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` live-GDB
window with low guest logging and check whether the run reached
`segment=2/global_stage=3/local_stage=1/subbatch=0` `GEMM_PREP`.

This round intentionally did not add guest-side runtime logs.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Run host: `192.168.1.33`
- Instance: `i-0011699a30abf2fb0`
- Binary SHA256:
  `152443db8982e3b056f8e417861efa10f4c850d3cc3ad74d685a9047631f46c6`
- Remote image SHA256:
  `80b18d4952fcfe99d3c7eb42ab06da2b903a08cc758f2eae4f1f3564554cab5d`
- Evidence directory:
  `tmp/firesim-aws-f2/gdb-live/20260509-1256-sbus64-flush-ladder/`

Effective workload profile:

```text
NO_DMA=0
GDBSERVER=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0
guest sparse/deep/audit/checkpoint/breadcrumb/periodic sync disabled
probe tier 1
```

## Observations

GDB was the first TCP client to the guest `gdbserver --once` endpoint. The
inferior started and worker threads appeared:

```text
[New Thread 217.240]
[New Thread 217.245]
[New Thread 217.248]
[New Thread 217.250]
[New Thread 217.249]
```

The armed argument breakpoint was:

```gdb
break prt_debug_state_set_worker if segment_idx==2 && global_stage_id==3 && local_stage_id==1 && subbatch_id==0 && phase_id==4
```

That breakpoint did not hit in the observed window. A later external `SIGINT`
to the non-interactive GDB process did not return a useful GDB prompt or stack;
the local GDB process was ended and the `gdbserver --once` session was consumed.

Run-host heartbeat showed the simulator was still alive at least once:

```text
18005629546, 954
```

UART reached `gdbserver` `phase=listening` and `phase=inferior`, but the
low-noise profile did not provide guest sparse logs for a runtime frontier.

## Interpretation

This is not a post-flush frontier and should not be read as evidence that
`runtime_flush_stage_spm_xlate()` regressed. It only proves that this particular
breakpoint strategy missed the target `GEMM_PREP` boundary or that execution
entered an earlier remote-interrupt-unresponsive path before the target
condition.

The earlier record
`20260509T061047Z_sbus64_live_gdb_build_task_flush_frontier.md` remains the
stronger positive evidence for the same AGFI route: that run reached
`runtime_flush_stage_spm_xlate(stage_id=1)`, crossed manager `4/5/6/7`
`prt_gemmini_spm_xlate_flush()`, and returned to `stage_prepare_exec_views()`.

## Lessons

- Do not jump directly to the late `GEMM_PREP` conditional breakpoint as the
  only first stop.
- For the next GDB round, start from earlier worker or segment boundaries, then
  dynamically arm the post-flush ladder after the target worker/thread is known.
- Avoid TLS conditions on this remote target; `qGetTLSAddr` is unreliable here.
- Avoid non-interactive external `SIGINT` as the primary stack-capture method.
  Prefer live tmux GDB with pre-armed boundary breakpoints.

## Cleanup

The run farm was terminated with:

```text
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-terminaterunfarm-20260509-130639
```

`terminaterunfarm` reported instance `i-0011699a30abf2fb0` terminated, and the
AWS active-f2 check returned empty immediately after.
