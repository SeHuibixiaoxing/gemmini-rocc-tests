# 20260509T042039Z - sbus64 live GDB token 5514 post-wait path passed, host watchdog terminated run

## Goal

Continue the low-perturbation live-GDB localization for the
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` F2 run. The specific question was
whether the real hang frontier remained inside the DMA wait post-fence path, and
whether the observed shared-fence path actually returned for the target worker.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host:
  - Instance: `i-011e30fe4d4ba888d`
  - Private IP: `192.168.1.238`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--03-49-41-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- GDB transcript:
  `tmp/firesim-aws-f2/gdb-live/20260509-0358-sbus64-postfence/gdb_tmux.log`
- Host-watchdog capture prefix:
  `tmp/firesim-aws-f2/captures/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-034940-192.168.1.238-host-watchdog-20260509T041650Z.*`

## Pitfall: Wrong ELF

Initial GDB used the stale ELF:

```text
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_pipeline_runtime-linux
sha256 d0210721793ffb2a062adc1b3276c53d541a9a5ef4a01d159c363524e2f57af8
```

The guest image actually contained the staged build ELF:

```text
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
sha256 b0c99c20e02c995c24d0f1fcaaba1c8e2a59b6f844c746cc94404fc7d407a73c
```

The wrong ELF made `break prt_gdb_marker_stop` resolve to `0x14628`, which is
inside `prt_gdb_marker_note` in the correct binary. That produced a misleading
stack through unrelated code and garbage marker state at `0x4c348`. This stop is
invalid and must not be used as evidence.

Recovered in the same live session with:

```gdb
symbol-file /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
delete breakpoints
p/x &g_prt_gdb_marker_state
disassemble prt_gdb_marker_stop
```

Correct symbol checks:

```text
&g_prt_gdb_marker_state = 0x58360
prt_gdb_marker_stop = 0x1457a
```

## Pitfall: Conditional Breakpoint on Marker Entry

A conditional GDB breakpoint on `prt_gdb_marker_note` is too intrusive for this
path. GDB still plants a software breakpoint at function entry, so every marker
call traps into GDB before the condition is evaluated. In this run, Ctrl-C during
that setup stopped at an unrelated `site_id=30 segment=0 stage=0 token=7` marker.

The lower-perturbation pattern is:

1. Let the program's own marker filter reject nonmatching contexts.
2. Break unconditionally on `prt_gdb_marker_stop`.
3. Dynamically edit `g_prt_gdb_marker_filter` only while the target is stopped.

## Valid Live-GDB Observations

After correcting the ELF and switching to `prt_gdb_marker_stop`, the target
worker-entry marker was valid:

```text
site_id=4
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=any
manager_id=4
aux0=2
aux1=1
line=4198
```

At that stop:

- Thread 3 was at `stage_worker_main()` worker entry.
- Thread 4 was in stage0 `dma_copy_host_to_spm_pages_linux()`.
- Thread 5 was waiting in `prt_pipebuf_wait_full()`.
- Thread 1 was in `prt_runtime_run()` sleep.

The marker filter was then changed live to follow DMA wait post-fence markers
for `segment=2/global_stage=3/local_stage=1`.

The first matched target token was:

```text
token_id=5514
manager_id=4
tensor_id=3
page_idx=0
subbatch_id=0
aux0=4371908608
aux1=1077936128
caller=dma_copy_host_to_spm_pages_linux -> dma_submit_wait_annotated_scoped
```

The same token reached the following markers in order:

```text
site 30 dma-wait-after-fence          line 3700
site 31 dma-wait-before-shared-fence  line 3776
site 32 dma-wait-after-shared-fence   line 3779
site 33 dma-wait-after-release        line 3820
site 34 dma-wait-after-complete       line 3859
site 35 dma-wait-after-trace-complete line 3875
site 13 dma-wait-return               line 3583 rc=0
site 25 dma-submitwait-after-wait     line 2143 rc=0
site 26 dma-submitwait-after-cleanup  line 2180 rc=0
```

## Interpretation

This is a real卡点移动 relative to the earlier shared-fence hypothesis.

Confirmed for observed target token `5514`:

- `hw_dma_fence()` returned.
- The external shared-fence path was entered.
- `dma_token_fence_scope()` returned, because `site=32` was reached.
- The release path after shared fence returned.
- `dma_token_complete()` returned.
- `dma_trace_complete_once()` returned.
- `dma_blocking_wait()` returned `PRT_OK`.
- The submit+wait wrapper reached after-cleanup.

Therefore the active frontier is no longer "target token hangs in
`hw_dma_fence()` / shared fence / DMA wait completion cleanup" for this observed
token. The remaining possibilities are a later token/page in the same worker, a
later worker phase after this DMA, or a different worker/pipebuf dependency.

Not proven:

- Whether a later token in `segment=2/global_stage=3/local_stage=1` hangs.
- Whether the worker reaches `worker-entry-process-return`, `entry-full-return`,
  `before/after-exports-ready`, `worker-gemm-run`, or `worker-export-sync`.
- Whether a later shared-fence instance hangs.

## Run End

The run did not finish normally. The host watchdog fired:

```text
[prt-host-watchdog] host idle timeout reached after 689s (hb_idle=626s)
```

It captured the guest state and called `terminaterunfarm --forceterminate`.
The GDB remote then reported:

```text
Remote connection closed
```

The watchdog capture reported the inferior in `State: t (tracing stop)` with
`TracerPid: 203`, so this close must be treated as host-side watchdog termination
of a live GDB session, not as workload pass/fail.

The F2 instance entered `shutting-down` after termination:

```text
i-011e30fe4d4ba888d 192.168.1.238 f2.6xlarge shutting-down
```

## Lessons for SOP

- Always verify the local ELF against the staged build/image before trusting any
  GDB breakpoint or stack. The correct default is
  `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`, not the stale
  source-tree `rerocc-linux-tests/rerocc_pipeline_runtime-linux`.
- Use `symbol-file` recovery if the wrong ELF is detected before losing the
  `gdbserver --once` connection.
- Trust `g_prt_gdb_marker_state` only after a valid stop in
  `prt_gdb_marker_stop`. A stop in `prt_gdb_marker_note` before state writes, or
  a stop caused by wrong symbols, can show stale or garbage state.
- Do not use high-frequency conditional breakpoints on `prt_gdb_marker_note`.
  Use the runtime marker filter plus one `prt_gdb_marker_stop` breakpoint.
- Long manual GDB stops freeze guest heartbeat. With the low-log profile, the
  host watchdog may see no progress and terminate the F2 after the idle timeout.
  Interactive live-GDB runs need a larger finite
  `FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS`.

## Next Step

Before the next F2 round:

1. Update the gdbserver SOP with the ELF check, marker-filter pattern, and
   host-watchdog rule.
2. Raise the current sbus64 live-GDB workflow idle timeout for interactive
   sessions.
3. Start the next run from a lower-frequency frontier:
   `segment=2/global_stage=3/local_stage=1`, worker-level markers such as
   `worker-entry-process-return`, `worker-entry-full-return`,
   `worker-before/after-exports-ready`, `worker-before/after-build-stage-task`,
   `worker-gemm-run`, and `worker-export-sync`.
