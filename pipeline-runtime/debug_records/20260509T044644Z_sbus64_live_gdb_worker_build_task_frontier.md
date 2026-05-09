# 20260509T044644Z - sbus64 live GDB moved worker frontier to build_stage_task_desc()

## Goal

Continue the low-perturbation live-GDB worker-level localization after the
`token=5514` DMA wait path had been proven to return. The question for this
round was whether the `segment=2/global_stage=3/local_stage=1` worker could
leave entry processing, pass export readiness, and reach task construction.

This round used the existing live interactive GDB session. It did not use
batch GDB as the primary debugger.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host:
  - Instance: `i-01ce5e8e19da16450`
  - Private IP: `192.168.1.240`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--04-28-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Runworkload tmux:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-042848`
- GDB tunnel tmux:
  `prt-gdb-tunnel-sbus64-next`, local `:32363` to guest `172.16.0.2:2345`
- GDB tmux:
  `prt-gdb-sbus64-next`
- GDB transcript:
  `tmp/firesim-aws-f2/gdb-live/20260509-0434-sbus64-worker-next/gdb_tmux.log`
- Host watchdog:
  `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-042848.host-watchdog.log`
- Runtime image binary SHA:
  `b0c99c20e02c995c24d0f1fcaaba1c8e2a59b6f844c746cc94404fc7d407a73c`

## Symbol Validation

The live GDB session used the staged build ELF:

```text
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

The symbol sanity checks matched the expected binary:

```text
&g_prt_gdb_marker_state = 0x58360
prt_gdb_marker_stop = 0x1457a
```

The debugger set one unconditional breakpoint on `prt_gdb_marker_stop`. The
runtime marker filter was changed only while the target was stopped.

## Valid Live-GDB Observations

Initial stop:

```text
site_id=4 worker-entry
segment_idx=2
global_stage_id=3
local_stage_id=1
manager_id=4
rc=0
aux0=2
aux1=1
line=4198
```

Thread state at that stop included:

- target worker thread at `stage_worker_main()` worker entry;
- another worker in `hw_dma_fence() -> dma_blocking_wait() ->
  dma_submit_wait_annotated_scoped() -> dma_copy_host_to_spm_pages_linux() ->
  prt_process_c1()`;
- main thread in `prt_runtime_run()` thread creation/sleep path.

The marker filter was then changed live through the worker-level ladder for the
same `segment=2/global_stage=3/local_stage=1` context.

Observed marker sequence:

```text
site 19 worker-entry-process-return
  subbatch=0 manager=4 tensor=3 page=0 rc=0 aux0=0 aux1=0 line=4246
  stack: prt_runtime_gdb_marker -> stage_worker_main line 4241

site 20 worker-entry-full-return
  subbatch=0 manager=4 tensor=3 page=0 rc=0 aux0=0 aux1=0 line=4325
  stack: prt_runtime_gdb_marker -> stage_worker_main line 4320

site 21 worker-before-exports-ready
  subbatch=0 manager=4 rc=0 aux0=1 aux1=1 line=4352
  stack: prt_runtime_gdb_marker -> stage_worker_main line 4345

site 22 worker-after-exports-ready
  subbatch=0 manager=4 rc=0 aux0=1 aux1=1 line=4367
  stack: prt_runtime_gdb_marker -> stage_worker_main line 4360

site 23 worker-before-build-stage-task
  subbatch=0 manager=4 rc=0 aux0=4 aux1=4 line=4416
  stack: prt_runtime_gdb_marker -> stage_worker_main line 4409
```

After changing the runtime filter to `site=24 worker-after-build-stage-task`,
no `site=24` stop was observed during the live wait. The host watchdog showed no
new UART progress while idle time advanced into the hundreds of seconds, still
well below the configured finite idle timeout:

```text
idle_timeout_seconds=7200 live_idle_timeout_seconds=10800
...
idle=438s hb_idle=532s uart=20794
```

An interactive GDB `Ctrl-C` was sent after the `site=24` wait. The GDB pane
echoed `^C` but did not return to a GDB prompt during the short follow-up
window, so this run did not produce an all-thread stack at the final stuck
point.

## Source Window

The new live frontier is the small window in `stage_worker_main()` between
`worker-before-build-stage-task` and `worker-after-build-stage-task`:

```text
prt_runtime.c:4409  worker-before-build-stage-task marker
prt_runtime.c:4417  rc = build_stage_task_desc(rt, ctx->stage_id, ...)
prt_runtime.c:4423  worker-after-build-stage-task marker
```

Therefore this run proves the target worker reached the call to
`build_stage_task_desc()` and did not reach the post-call marker in the observed
window.

`build_stage_task_desc()` dispatches through:

```text
build_stage_task_desc()
  -> build_stage_conv_desc() or build_stage_resadd_desc()
  -> stage_prepare_exec_views()
```

On the RISC-V path, `stage_prepare_exec_views()` may issue fixed-tensor DMA via
`prt_dma_copy_dram_to_spm_pages()` and then may call
`runtime_flush_stage_spm_xlate()`. Those are the first internal sub-frontiers to
probe in the next run.

## Interpretation

This is a real卡点移动 relative to the previous worker-entry and token-5514 DMA
frontiers.

Confirmed for `segment=2/global_stage=3/local_stage=1/subbatch=0/manager=4`:

- entry processing returned;
- full entry wait returned;
- the worker reached the exports-ready check;
- exports-ready returned with `rc=0`;
- the worker reached the task construction boundary.

Not implicated for this observed worker/subbatch:

- entry pipebuf full wait;
- export readiness wait;
- the previously observed `token=5514` wait/fence/shared-fence/release/cleanup
  path.

Current active frontier:

- after `site=23 worker-before-build-stage-task`;
- before `site=24 worker-after-build-stage-task`;
- likely inside `build_stage_task_desc()` or a callee such as
  `stage_prepare_exec_views()`, fixed-tensor DMA load, SPM xlate bind/flush, or
  a hardware wait reached there.

The failed remote interrupt is important but not by itself a stack. It means
the next round should not rely on "wait until stuck, then Ctrl-C" to recover the
PC. It should pre-place lower-level internal breakpoints or markers inside the
`site=23 -> site=24` source window.

## Next Debug Target

Use a fresh `gdbserver --once` run and stop first at `worker-before-build-stage-task`
for the same filter. Then pre-place lower-level probes before continuing:

- `build_stage_task_desc(stage_id == 1)`;
- `build_stage_conv_desc(stage_id == 1)` and/or `build_stage_resadd_desc(stage_id == 1)`;
- `stage_prepare_exec_views(stage_id == 1)`;
- `prt_dma_copy_dram_to_spm_pages()` with `stage_idx == 1` when arguments are
  available;
- `runtime_flush_stage_spm_xlate(rt, 1)` or the closest available symbol.

If those stops are too frequent or optimized arguments are unavailable, add a
small runtime marker ladder inside `stage_prepare_exec_views()` around:

- fixed-load begin/end;
- each `prt_dma_copy_dram_to_spm_pages()` call;
- each `prt_spm_bind_vpages_ctx()` call;
- `runtime_flush_stage_spm_xlate()` begin/end.

Keep the run low-perturbation. Do not add guest file logging or broad breadcrumb
spam unless the internal marker ladder is still insufficient.

## Run End

The round was intentionally ended after `site=24` did not hit and interactive
GDB interrupt did not return a prompt. The F2 run farm must be terminated before
starting the next run.
