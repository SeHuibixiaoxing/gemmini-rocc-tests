# 20260508T045331Z dummy8x8/sbus64 page57 marker frontier pass

## Context

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-0970c0c54920551a9`, private IP `192.168.1.175`
- Workload results:
  `sims/firesim/deploy/results-workload/2026-05-08--04-43-39-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Artifact bundle:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260508T045331Z_dummy8x8_sbus64_page57_marker_frontier/`

## Guest Marker Setup

The run used a guest-side marker to avoid the hot GDB conditional breakpoint
that previously disturbed the run before page57:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-fixed-load-submitwait-begin
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=0
PIPELINE_RUNTIME_GDB_MARKER_PAGE=57
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0
```

The host GDB helper was:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FIXED_LOAD_PAGE=57 \
PRT_GDB_MARKER_TIMEOUT=1800 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh \
  192.168.1.175 172.16.0.2:2345 32345
```

GDB connected through an SSH tunnel as the first TCP client to the guest
`gdbserver --once` endpoint. No `nc`, `curl`, or `telnet` probe was used
against `172.16.0.2:2345`.

## Result

The workload completed successfully:

```text
[bertmini] PASS method=ours2
BERTMINI_PIPELINE_RUNTIME_PASS
[firemarshal] pipeline-runtime exited rc=0
```

`bertmini-batch8.status` reports:

```text
state=finished
exit_code=0
profile_id=pairdummy-sbus64-dummy8x8-fixed-v3
dummy_gemmini_mode=1
gdbserver_enable=1
dma_force_direct_enable=1
```

This means the earlier page57 stall from
`20260508T041945Z_dummy8x8_sbus64_tensor0_page57_conditional_breakpoint_stall.md`
was an observation disturbance from the hot GDB conditional breakpoint, not a
real fixed-load page57 DMA hang.

## GDB Evidence

The marker stop hit the exact requested semantic coordinates:

```text
site_id = 27
local_stage_id = 0
manager_id = 0
tensor_id = 0
page_idx = 57
aux0 = 0x104e02800
aux1 = 0x40101c00
line = 840
```

The immediate path then reached:

- `dma_submit_wait_annotated_scoped`
- `prt_dma_submit`
- `dma_blocking_wait`
- the line containing `hw_dma_fence()`

At `dma_blocking_wait` entry for this page:

```text
tok->id = 58
tok->stage_idx = 0
tok->tensor_id = 0
tok->debug_page_idx = 57
tok->rr_manager_id = 0
tok->rr_scope_valid = 1
tok->rr_scope_external = 1
tok->hw_done_flag = 0
tok->completion_flag = 0x5e000
*tok->completion_flag = 1
tok->debug_src_addr = 0x104e02800
tok->debug_dst_addr = 0x40101c00
tok->debug_done_flag_pa = 0x1032f9000
tok->debug_bytes = 1024
```

The completion flag value is telemetry only. It is not completion proof and was
not used as a pass criterion.

## Helper Limitation Found

The helper stopped at line 3683 in an inline `hw_dma_fence()` frame:

```text
#0 hw_dma_fence()
#1 dma_blocking_wait(... tok=0x3ff6d74ff8 ...)
```

The generated GDB command file then attempted to print `tok` while frame 0 was
selected, so GDB reported:

```text
No symbol "tok" in current context.
```

Despite this helper error, the guest later continued and the full workload
passed. The script has been fixed after this run to save `tok` in a GDB
convenience pointer at `dma_blocking_wait` entry and to run GDB in batch mode
so sourced command-file errors return nonzero.

## Breadcrumb Interpretation

The captured breadcrumb last slot is:

```text
last_kind=dma
last_phase=dma_wait_before_fence
tensor=0
tok=58
mgr=0
page=57
src=0x104e02800
dst=0x40101c00
aux0=0x1032f9000
aux1=0x989680
line=3648
```

This breadcrumb reflects the GDB stop before `hw_dma_fence()`, not the final
runtime frontier. The sparse log and status file prove the run completed after
the debugger disconnected.

## Cleanup

The run farm was terminated with:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

FireSim terminated `i-0970c0c54920551a9`; AWS reported the instance in
`shutting-down` state immediately after cleanup.

## Next Probe

Use the fixed helper on a fresh `gdbserver --once` run if after-fence evidence
for page57 is still desired. The higher-value next localization target is the
previous unresolved export frontier: tensor2/page47. That rerun should use a
guest-side marker such as `dma-export-page-submit-begin` or `dma-wait-enter`,
not a hot GDB conditional breakpoint.

Do not use doneflag polling as DMA completion logic. doneflag remains an
auxiliary observation signal only; valid pass evidence must come from the
blocking wait/fence path plus workload completion.
