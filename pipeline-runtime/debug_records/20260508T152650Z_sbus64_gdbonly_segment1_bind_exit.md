# 20260508T152650Z sbus64 dummy8x8 GDB-only segment1 bind frontier

## Context

- Date: 2026-05-08 UTC.
- AGFI: `agfi-077451484fe3b63c3`.
- AFI: `afi-07989ce9ce725a690`.
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO.
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`.
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`.
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`.
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`.
- Run host: `i-02a91ace8afd8d3a8`, private IP `192.168.1.195`.
- FireSim run result dir: `sims/firesim/deploy/results-workload/2026-05-08--14-42-36-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`.
- Manager log: `sims/firesim/deploy/logs/2026-05-08--14-42-36-runworkload-GZ37V6JQ9LP2BL59.log`.
- FireSim run farm was terminated after evidence collection with `terminaterunfarm --forceterminate`.

## Observability Discipline

- `gdbserver` was reached through the NIC as the first TCP client to `172.16.0.2:2345`.
- Guest sparse/deep/audit/checkpoint logs, breadcrumb, debug trigger, DMA probes, and periodic sync were disabled.
- UART was used only for runner and `gdbserver` phase markers.
- No `nc`, `curl`, or generic port probe was used against guest port 2345.

## Commands

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS=7200 \
FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=14400 \
FIRESIM_RUNWORKLOAD_WATCHDOG_SECONDS=21600 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.195

ssh -i /home/ubuntu/firesim.pem -N \
  -L 32350:172.16.0.2:2345 ubuntu@192.168.1.195

.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

GDB connected with:

```gdb
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
target remote :32350
```

## Live GDB Findings

The previous working hypothesis was that execution stalled after `prt_runtime.c:5419` in post-segment cleanup. This run disproves that as the current hard frontier.

Observed segment0:

- Hit `prt_runtime.c:5419`, `segment threaded backend complete`.
- `action=0x69bb0`, `action_id=1`.
- `alias_page_count=193`, `spm_xlate.pte_count=193`, `spm_source.alloc_count=4`, `acc_source.all_count=8`.
- `runtime_release_topology(rt)` at `prt_runtime.c:5423` returned to `prt_runtime.c:5424`.
- `prt_action_release(rt, &action)` entered and returned to `prt_runtime.c:5425`.

Inside segment0 `prt_action_release`:

- `disable_action_spm_xlate()` ran for managers `{0,1,2,3,4,5,6,7}` and returned for all of them.
- For manager 0, GDB stepped through:
  - `prt_gemmini_spm_xlate_program(... enable=0)`.
  - `prt_spm_xlate_acquire_scope()`.
  - `rerocc_gemmini_spm_xlate_cfg/range/flush`.
  - `prt_spm_xlate_release_scope()`.
  - `rr_fence(cfg31)`.
  - `rr_release(cfg31)`.
  - `rr_read_csr(CSR_RRCFG31)`.
  - opcode restore.
- `prt_spm_unbind_vpages_ctx(... page_count=193)` returned.
- `prt_release_tensor_pages()` returned for alloc keys `{1895825408,1895825409,1895825410,1895825411}`.
- `prt_spm_xlate_ctx_release()` returned, including page-table slice release.
- alias window unmap returned.
- `prt_action_exec_destroy()` returned.

After segment0 cleanup:

- `prt_runtime.c:5425` was reached.
- The runtime continued into the next segment rather than proceeding directly to final runtime end.
- GDB later stopped in `build_topology_from_pipeline()` for `action_id=2`, `segment_idx=1`.
- Segment1 `build_topology_from_pipeline()` returned:
  - `pipebuf_count=2`.
  - `ringbuf_count=0`.
  - `stage_thread_count=1`.
- Segment1 reached `prt_runtime.c:5222`, `prepare-stage-spm end`.
- The inferior then exited with code `01` before the `bind-topology end` breakpoint at `prt_runtime.c:5226`.

## Updated Frontier

Current hard frontier is no longer segment0 post-segment cleanup.

The best current frontier is:

- Segment1, after `runtime_prepare_stage_spm_windows()` returns at `prt_runtime.c:5222`.
- Before successful return from `prt_action_bind_topology(rt, action)` at `prt_runtime.c:5226`.
- GDB reports inferior exit code `01`, so this looks like an ordinary software error path or explicit runtime failure, not an uninterruptible hardware wait.

Primary next suspect:

- `prt_action_bind_topology()` for `action_id=2`, `segment_idx=1`, with a one-stage/two-pipebuf/no-ring topology.

The next GDB run should put breakpoints inside `prt_action_bind_topology()` and its segment1 SPM/page/buffer binding subroutines, and print the exact `rc` before `PRT_GOTO_OUT_ON_ERR("action_bind_topology")` is taken.

## Cleanup

- Host GDB was exited.
- SSH local-forward process was killed.
- `terminaterunfarm --forceterminate` terminated `i-02a91ace8afd8d3a8`.
- Stale local `runworkload` manager processes from this run were killed after AWS reported no active F2 instance.
