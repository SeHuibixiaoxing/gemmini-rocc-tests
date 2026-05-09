# 20260509T133235Z - sbus64 site23 pre-marker interrupt disconnect

## Goal

Re-run the `dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` gdbserver path with
the historical `site=23 worker-before-build-stage-task` marker filter restored.
The intent was to avoid the previous late `GEMM_PREP` miss and re-enter the
known segment2 build-task window before walking the post-flush ladder.

No guest runtime logs, deep logs, breadcrumbs, debug triggers, or DMA probes
were enabled.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host:
  - Instance: `i-034efcb85c01d84a9`
  - Private IP: `192.168.1.161`
- Runtime binary SHA256:
  `152443db8982e3b056f8e417861efa10f4c850d3cc3ad74d685a9047631f46c6`
- Local/remote image SHA256:
  `52a8c037e4bce54496ed844417d85b1b673124da4b90801478c3fa849d8a1304`
- Effective guest env SHA256:
  `f6f76b03dec54e964ffc58b7467ac2eee8271227bd64c77a8153c372b353cdc2`
- Evidence directory:
  `tmp/firesim-aws-f2/gdb-live/20260509-1324-sbus64-site23-postflush/`

Effective debug settings:

```text
NO_DMA=0
GDBSERVER=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=23
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
guest sparse/deep/audit/checkpoint/breadcrumb/periodic sync disabled
probe tier 1
```

## GDB Observations

GDB was the first TCP client to `gdbserver --once`; the SSH tunnel itself did
not probe the guest TCP endpoint.

`gdbserver` reached the inferior:

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=203
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=216
```

The current ELF symbols were validated after attach:

```text
&g_prt_gdb_marker_state = 0x59368
prt_gdb_marker_stop = 0x146de
```

GDB set:

```gdb
break prt_gdb_marker_stop
continue
```

The `site=23` marker did not hit during the observed window. An interactive
GDB `Ctrl-C` first only echoed `^C` and did not return to a prompt. A second
`Ctrl-C` returned:

```text
Disconnected from target.
```

No PC or all-thread stack was captured.

## Interpretation

This run confirms that the program did enter the inferior, but it did not reach
`segment=2/global_stage=3/local_stage=1/subbatch=0` `site=23` before entering a
remote-interrupt-unresponsive path. This is not evidence of a post-flush
regression.

The stronger positive record remains:
`20260509T061047Z_sbus64_live_gdb_build_task_flush_frontier.md`, which proved
the same AGFI route could pass fixed-load DMA, SPM binds, manager `4/5/6/7`
SPM xlate flush, and return to `stage_prepare_exec_views()`.

Current reliable frontier for this round:

```text
gdbserver inferior started
site=23 worker-before-build-stage-task was not reached
interactive remote interrupt did not produce a stop prompt
```

## Next Debug Step

Do not spend another F2 run waiting only for `site=23` or a late `GEMM_PREP`
condition. The next gdbserver run should pre-place earlier stops before
`continue`, for example:

- `prt_main_entry`;
- `prt_runtime_run`;
- `stage_worker_main`;
- `prt_gemm_conv_run`;
- `gemm_issue_task` / `gemm_issue_conv_task` source-line breakpoints;
- `prt_debug_state_set_worker` filtered only by concrete arguments such as
  `segment_idx==2`, or by early phase windows after a stable stop.

If the first early stop shows execution entering stage0/early Gemmini compute,
walk the issue/fence boundary before the custom instruction path, rather than
waiting for segment2.

## Cleanup

Evidence was copied before teardown. The run farm was terminated with:

```text
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-terminaterunfarm-20260509-133131
```

`terminaterunfarm` reported instance `i-034efcb85c01d84a9` terminated. The AWS
active-f2 check returned empty, and the stale local `firesim runworkload`
process was killed.
