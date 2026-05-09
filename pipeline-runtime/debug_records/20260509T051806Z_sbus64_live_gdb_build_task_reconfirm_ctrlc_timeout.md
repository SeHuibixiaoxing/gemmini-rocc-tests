# 20260509T051806Z - sbus64 live GDB reconfirmed build-task frontier

## Goal

Re-run the low-perturbation live interactive GDB flow for the
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` F2 profile and check whether the
previous frontier at `worker-before-build-stage-task` was stable, moved
forward to `worker-after-build-stage-task`, or was only a run-to-run artifact.

This round used a live interactive GDB session as the primary debugger. The
initial `riscv64-unknown-linux-gnu-gdb` command failed because that executable
was not on the shell `PATH`; the successful session used the absolute toolchain
path.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Run host:
  - Instance: `i-05d443c679fc8c9d3`
  - Private IP: `192.168.1.235`
  - FireSim cluster tag: `pairbertb8d12s64gdbcfg32nicnt`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Results directory:
  `sims/firesim/deploy/results-workload/2026-05-09--04-59-33-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Runworkload tmux:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-045931`
- Runworkload log:
  `sims/firesim/deploy/logs/2026-05-09--04-59-32-runworkload-7VO0XKZ6370A1ORN.log`
- Host watchdog:
  `tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-045931.host-watchdog.log`
- GDB tunnel tmux:
  `prt-gdb-tunnel-sbus64-buildtask`, local `:32364` to guest `172.16.0.2:2345`
- Successful GDB tmux:
  `prt-gdb-sbus64-buildtask`
- Successful GDB transcript:
  `tmp/firesim-aws-f2/gdb-live/20260509-0506-sbus64-buildtask/gdb_tmux2.log`

## Image and Symbol Validation

Remote and local image freshness matched:

```text
image sha256 = 5e70ad0653e3aacfe5e3e89f7c072bd0745d1f6e31698c2bc9d0e8f3ee3c92e5
runtime binary sha256 = b0c99c20e02c995c24d0f1fcaaba1c8e2a59b6f844c746cc94404fc7d407a73c
firemarshal env sha256 = b0a8585e1ed0890290d353991d69b6dd393c17567500fd8b2643eb6ba0d6a49b
```

The successful GDB session used:

```text
/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

Symbol sanity matched the staged ELF:

```text
&g_prt_gdb_marker_state = 0x58360
prt_gdb_marker_stop = 0x1457a
```

Guest UART showed the expected gdbserver phases:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=203
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

## Live-GDB Observations

The runtime filter was initially armed for:

```text
site_id=23 worker-before-build-stage-task
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=any
tensor_id=any
page_idx=any
token_id=any
```

The first bounded interrupt did not hit `site=23`. It stopped in an earlier
fixed-load DMA submit path:

```text
Thread 2:
#0 dma_blocking_submit() at prt_dma.c:3375
#1 dma_submit_wait_annotated_scoped(... stage_idx=0, tensor_id=0, debug_page_idx=26) at prt_dma.c:2115
#2 dma_copy_host_to_spm_pages_linux() at prt_dma.c:866
#3 prt_dma_copy_dram_to_spm_pages_prefix() at prt_dma.c:2834
#4 prt_process_c1() at prt_scheduler.c:294
#5 stage_worker_main() at prt_runtime.c:4236
```

The token state at that stop was:

```text
tok->id = 1632
tok->stage_idx = 0
tok->tensor_id = 0
tok->debug_page_idx = 26
tok->rr_manager_id = 0
tok->debug_src_addr = 0x104927c00
tok->debug_dst_addr = 0x40200c00
tok->debug_bytes = 1024
tok->debug_done_flag_pa = 0x103050000
tok->done = 0
tok->hw_done_flag = 0
tok->status = 0
req = {src_addr=4371676160, dst_addr=1075842048, bytes=1024, src_acc=0, dst_acc=0}
pc = 0x16e3e <dma_blocking_submit+1342>
```

This was not the final frontier. After a bounded `continue`, the thread exited
that early stage0 DMA path and GDB hit the intended worker marker:

```text
site_id=23 worker-before-build-stage-task
segment_idx=2
global_stage_id=3
local_stage_id=1
subbatch_id=0
manager_id=4
rc=0
aux0=4
aux1=4
line=4416

stack:
prt_gdb_marker_stop()
prt_gdb_marker_note(site_id=23, ..., line=4416)
prt_runtime_gdb_marker(... site_id=23)
stage_worker_main() at prt_runtime.c:4409
```

At the `site=23` stop, other live threads included:

- main thread in `clock_nanosleep() -> nanosleep() -> prt_runtime_run()`;
- one worker in `prt_host_virt_to_phys() -> dma_copy_host_to_spm_pages_linux()
  -> prt_process_c1()`;
- one worker waiting in `prt_pipebuf_wait_full()`.

The runtime filter was then changed in the same stopped GDB session to:

```text
g_prt_gdb_marker_filter.site_id = 24
```

GDB then continued waiting for `worker-after-build-stage-task`. `site=24` did
not hit in the observed window. An interactive `Ctrl-C` was sent. The GDB pane
only echoed `^C`; it did not return to a GDB prompt during the short follow-up
window, so no post-`site=23` all-thread stack was captured.

The host watchdog showed that the run was still below the finite live-GDB
timeout and that heartbeat later advanced, while UART and guest status did not
show a completion marker:

```text
idle_timeout_seconds=7200 live_idle_timeout_seconds=10800
guest_status=1678
uart=20882
heartbeat-progress hb='18211767372, 963'
```

## Interpretation

This round did not move the final frontier beyond the previous record. It
reconfirmed the same source window:

```text
prt_runtime.c:4409  worker-before-build-stage-task marker
prt_runtime.c:4417  rc = build_stage_task_desc(rt, ctx->stage_id, ...)
prt_runtime.c:4423  worker-after-build-stage-task marker
```

The early `stage0/tensor0/page26/token1632` fixed-load DMA stop was transient:
the run later reached `site=23`. It should not be treated as the current final
frontier unless a future run repeatedly stops there and fails to advance.

Confirmed for this run:

- the image and host GDB symbols were fresh and matched;
- `gdbserver --once` accepted the first GDB connection and started the inferior;
- the target reached `worker-before-build-stage-task` for
  `segment=2/global_stage=3/local_stage=1/subbatch=0/manager=4`;
- after retargeting the marker filter to `site=24`, the target did not reach
  `worker-after-build-stage-task` in the observed window;
- once the target entered the `site=23 -> site=24` window, interactive Ctrl-C
  again did not reliably return a stop prompt.

Therefore the current active user-space source window remains
`build_stage_task_desc()` or one of its callees. This round did not produce a
new PC inside that window because remote interrupt did not return after the
post-`site=23` continue.

## SOP Corrections From This Round

- Use the absolute cross-GDB path:
  `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb`.
  Do not assume `riscv64-unknown-linux-gnu-gdb` is on `PATH`.
- If the first interrupt before the target marker lands in an earlier DMA path,
  treat it as a sample, not as the frontier. Continue for a bounded window and
  see whether the intended marker is reached.
- Once `site=23` is hit, do not rely on "continue, wait until stuck, Ctrl-C" to
  recover a stack. In this profile that operation can leave GDB with only `^C`
  echoed and no prompt.
- The next run should pre-place or add lower-level stops while still at
  `site=23`, before continuing into `build_stage_task_desc()`.

## Next Debug Target

Start a fresh `gdbserver --once` run. Stop at
`worker-before-build-stage-task` for
`segment=2/global_stage=3/local_stage=1/subbatch=0`. While still stopped at
that marker, replace the marker stop with internal breakpoints inside the
`site=23 -> site=24` window, for example:

- `build_stage_task_desc()` with `stage_id == 1` when arguments are available;
- `build_stage_conv_desc()` and `build_stage_resadd_desc()`;
- `stage_prepare_exec_views()`;
- fixed-load DMA entry from inside `stage_prepare_exec_views()`;
- `runtime_flush_stage_spm_xlate()` or the nearest available SPM xlate flush
  symbol.

If optimized arguments make conditional function breakpoints unreliable, add a
small runtime marker ladder inside `stage_prepare_exec_views()` rather than
adding broad guest file logs.

## Run End

This round was ended after `site=24` did not hit and Ctrl-C did not return a GDB
prompt. The F2 run farm must be terminated before starting the next run.
