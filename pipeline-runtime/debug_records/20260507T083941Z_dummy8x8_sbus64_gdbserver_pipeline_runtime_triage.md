Debug category: gdbserver / pipeline-runtime / cfg32 NIC / dummy8x8 sbus64

# 20260507T083941Z - current AGFI gdbserver matrix PASS, pipeline-runtime hang triage in progress

## Context

This record captures the first valid GDB connection on the fresh 08:29 UTC
dummy8x8/sbus64/cfg32 NIC run after the stale same-tag watchdog problem was
removed.

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Target:
  `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`
- Build result:
  `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`
- Run host: `i-0b80c2a7436346724` / `192.168.1.62`
- Run farm tag: `pairbertb8d12s64gdbcfg32nicnt`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- FireSim run log:
  `sims/firesim/deploy/logs/2026-05-07--08-29-49-runworkload-2O6UEE6RINEI3WZI.log`
- Result directory:
  `sims/firesim/deploy/results-workload/2026-05-07--08-29-49-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

The run uses the restored known-good SimpleNIC plusargs:

```text
+simplenic-relaxed-required-bytes=1
+simplenic-empty-switch-poll-interval=1024
+simplenic-token-debug=0
+cpu-managed-stream-debug=0
+heartbeat-polling-interval=100000000
```

## Guest Bringup

The guest booted Linux, mounted the root filesystem, and reached the expected
gdbserver state:

```text
running /etc/init.d/S99run
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=222
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=235
```

The guest info file later agreed with UART:

```text
phase=inferior
guest_ipv4=172.16.0.2
pid=235
gdbserver_pid=222
```

No TCP port probe was used before GDB. The first TCP client to
`172.16.0.2:2345` was the cross-GDB session below.

## GDB Command

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  192.168.1.62 172.16.0.2:2345 32345
```

Result:

```text
[pairdummy-gdb-expect] expect_rc=0
[pairdummy-gdb-expect] PASS
```

Transcript:

```text
tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-expect-20260507T083941Z-192_168_1_62-172_16_0_2/pairdummy-cfg32-triage.expect.log
```

## Covered GDB Matrix

This run covered the intended remote-gdbserver smoke matrix on the current
AGFI:

- `target remote`
- `info threads`
- `thread apply all bt`
- register reads
- disassembly at `$pc`
- inferior variable read/write through `g_prt_debug_filter.rr_cfg_id`
- inferior memory read/write through `x/wx $prt_gdb_filter_addr` and
  `set {unsigned int}($prt_gdb_filter_addr) = ...`
- `break prt_main_entry`
- `continue` to `prt_main_entry`
- `next` from the early software-only entry point
- multiple software breakpoints on pipeline-runtime functions
- `continue` to a later pipeline-runtime breakpoint
- backtrace at breakpoint
- Ctrl-C / SIGINT recovery while the inferior was running
- post-interrupt thread, register, and disassembly reads
- `detach`

Observed markers:

```text
GDB_MARK_CONNECTED
GDB_MARK_MEMORY_RW_DONE
GDB_MARK_HIT_MAIN_ENTRY
GDB_MARK_NEXT_DONE
GDB_MARK_HIT_FIRST_BREAK
GDB_MARK_INTERRUPT_BEGIN
GDB_MARK_INTERRUPT_DONE
GDB_MARK_DETACH_OK
```

Variable / memory mutation evidence:

```text
$1 = 0xffffffff
set variable g_prt_debug_filter.rr_cfg_id = 0x5a5aa5a5
$2 = 0x5a5aa5a5
0x56328 <g_prt_debug_filter+48>: 0x5a5aa5a5
set {unsigned int}($prt_gdb_filter_addr) = 0xa5a55a5a
0x56328 <g_prt_debug_filter+48>: 0xa5a55a5a
set variable g_prt_debug_filter.rr_cfg_id = $prt_gdb_old_rr_cfg
$3 = 0xffffffff
```

## First Runtime Breakpoint

The first later runtime breakpoint was in SPM translation reset during
`prt_runtime_init()`:

```text
Breakpoint 8, prt_gemmini_spm_xlate_program(manager_id=0, ptbr_pa=0,
  pte_count=0, page_shift=10, range_base=0, range_size=0, enable=0)
  at pipeline-runtime/src/prt_rerocc.c:419
```

Backtrace:

```text
#0 prt_gemmini_spm_xlate_program(...) at src/prt_rerocc.c:419
#1 prt_gemmini_spm_xlate_reset(...) at src/prt_rerocc.c:447
#2 runtime_bootstrap_spm_xlate(...) at src/prt_runtime.c:355
#3 prt_runtime_init(...) at src/prt_runtime.c:4697
#4 prt_main_entry(...) at src/main.c:329
```

This confirms that runtime initialization reached the ReRoCC/SPM translation
bootstrap path on the current hardware.

## Ctrl-C Triage Point

After all breakpoints were disabled, the expect helper continued the inferior
for two seconds and sent Ctrl-C. The inferior stopped in a host-visible file
write from the YAML loader:

```text
Program received signal SIGINT, Interrupt.
#0 write() from libc.so.6
#1 prt_write_fd_all_impl(...) at include/prt_progress.h:161
#2 prt_guest_write_all_impl(...)
#3 prt_guest_prefixed_vlog_impl(...)
#4 prt_progress_log_impl(fmt="yaml pipeline parse line=%u indent=%d text=%.160s")
#5 prt_load_pipeline_yaml(...) at src/prt_yaml_loader.c:1011
#6 prt_runtime_run(...) at src/prt_runtime.c:4903
#7 prt_main_entry(...) at src/main.c:341
```

The buffer being written was:

```text
[prt-progress] yaml pipeline parse line=179 indent=6 text=entryTensorIdList: [3, 2]
```

This is not a final hang classification, because the Ctrl-C was deliberately
sent after only two seconds for the smoke matrix. It proves that after the
first runtime breakpoint, execution advanced out of `prt_runtime_init()` and
into `prt_runtime_run()` / YAML parsing.

## Live Runtime State After Detach

After detach, the FireSim run was still live. At the time of this record:

- UART still only showed boot and gdbserver phase messages, because workload
  stdout was captured into guest files rather than UART.
- The guest sparse log was size `4353` and ended at:

```text
[prt-progress] init dma-backend begin backend=0
[prt-progress] dma-completion-pool before-prefault ptr=0x5c000 size=4096 page_bytes=4096
[prt-progress] dma-completion-pool after-prefault ptr=0x5c000 size=4096 page_bytes=4096
[prt-progress] dma-completion-pool before-mlock ptr=0x5c000 size=4096
[prt-progress] dma-completion-pool after-mlock rc=0 errno=0 ptr=0x5c000 size=4096
```

- `bertmini-batch8.status` still reported `state=running`.
- The host watchdog saw progress from `guest_sparse=3387` to
  `guest_sparse=4353`, then resumed idle monitoring.

This creates the current narrow triage question:

1. The GDB stack proves the inferior reached `prt_load_pipeline_yaml()` during
   the smoke test.
2. The guest sparse log visible through debugfs still ends much earlier, at
   the DMA backend completion-pool mlock message.
3. The next targeted GDB run should avoid immediate detach and should hold
   control after `prt_runtime_run()` or after YAML parsing, then interrupt
   after a longer runtime window to identify whether the real card point is
   filesystem/log writeback, YAML load, runtime scheduling, DMA submit/wait,
   ReRoCC scope acquire/release, Gemmini fence, or wrapper wait.

## Interpretation

The current AGFI is not blocked at gdbserver bringup. On this fresh run it
passes the same remote-GDB capability matrix that was previously recovered on
the dummy8x8/sbus64/cfg32 NIC bitstream:

- remote attach works;
- multiple software breakpoints work;
- `next` works;
- thread/backtrace/register/disassembly inspection works;
- inferior variable and memory mutation work;
- Ctrl-C recovers control;
- detach works.

The remaining issue is pipeline-runtime progress after the GDB smoke. The
best current lead is very early runtime I/O/progress logging versus YAML
loading, with the last durable guest log line still in DMA backend init. A
second targeted GDB session on a fresh workload is needed because
`gdbserver --once` was consumed by this smoke and then detached.
