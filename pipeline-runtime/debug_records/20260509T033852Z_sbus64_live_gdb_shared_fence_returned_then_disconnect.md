# 20260509T033852Z - sbus64 live GDB shared-fence returned, post-continue disconnect

## Context

- Date: 2026-05-09 UTC
- Purpose: continue low-perturbation pipeline-runtime hang localization with live interactive GDB over `gdbserver`.
- Hardware:
  - AGFI: `agfi-077451484fe3b63c3`
  - AFI: `afi-07989ce9ce725a690`
  - Config family: `pairmanager_dummy8x8_4c12p12_sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  - `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  - `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host:
  - Instance: `i-0c2aa349473c68061`
  - Private IP: `192.168.1.174`
- Results directory:
  - `sims/firesim/deploy/results-workload/2026-05-09--03-14-06-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Local sessions:
  - Runworkload: `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-031405`
  - Tunnel: `prt-gdb-tunnel-sbus64`
  - GDB: `prt-gdb-sbus64`
- GDB transcript:
  - `tmp/firesim-aws-f2/gdb-live/20260509-0322-sbus64-rrf/gdb_tmux.log`

## Constraints Held

- Guest sparse/deep/audit/checkpoint/breadcrumb/progress ping logs stayed disabled.
- GDB was used as the primary observation channel to avoid the known logging-induced hang movement.
- `gdbserver --once` was treated as single-client; no `nc`, `telnet`, or curl probe was used against guest port 2345.
- Doneflag polling remains a known-bad completion mechanism and was not used as pass/fail evidence. DMA completion must stay on the wait/fence path.

## Startup and Attach

UART confirmed the expected gdbserver sequence:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=202
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=215
```

The intended worker-entry marker fired:

```text
prt_gdb_marker_stop()
g_prt_gdb_marker_state = {
  site_id = 4,
  segment_idx = 2,
  global_stage_id = 3,
  local_stage_id = 1,
  manager_id = 4,
  rc = 0,
  aux0 = 2,
  aux1 = 1,
  line = 4198
}
```

## Slow Startup Observation

Before the worker marker, a Ctrl-C showed startup artifact validation, not model execution:

```text
prt_runtime_run
  prt_validate_gemmini_artifacts
    validate_stage_against_mapping_entries
      mapping_entry_matches
```

Observed context:

```text
seg = 2
stage = 0
entry_idx = 13525 / 16848
matches = 1
layout_matches = 71
```

Continuing progressed past this and reached the worker marker, so this is slow validation, not the current deadlock.

## ReRoCC Shared Fence Detail

The previous line-breakpoint plan was corrected because source breakpoints on `prt_rerocc.c:378/379`
were misleading under optimization and actually resolved around `prt_rr_release_scope`.

The reliable method used function entry plus disassembly:

```text
prt_rr_fence_scope = 0x29d5e
0x29d70: lwu  a1,4(a0)          # cfg_id
0x29d76: addi a0,a0,-2044       # csr 0x804
0x29d7a: jal  0x29012 <rr_swap_csr>
0x29d7e: fence
0x29d8a: ret
```

The cfg0 shared-fence path returned:

```text
scope = { valid = 1, cfg_id = 0, stage_id = 0, manager_id = 0, opcode_id = 2 }
rr_swap_csr(csr = 0x804, wdata = 0) returned
ordinary fence returned
prt_rr_fence_scope returned to dma_blocking_wait+634 at prt_dma.c:3777
```

The cfg2 shared-fence path also returned:

```text
scope = { valid = 1, cfg_id = 2, stage_id = 1, manager_id = 4, opcode_id = 2 }
rr_swap_csr(csr = 0x804, wdata = 2) returned
ordinary fence returned
prt_rr_fence_scope reached its return tail
backtrace:
  prt_rr_fence_scope
  dma_token_fence_scope
  dma_blocking_wait
  prt_dma_wait
```

This substantially weakens the earlier hypothesis that the observed blocker is simply
the first `rr_swap_csr` or ordinary `fence` inside `prt_rr_fence_scope`.

## Current Live End State

After disabling the noisy `prt_rr_fence_scope` breakpoint and continuing with scheduler locking off,
GDB was interrupted. The first Ctrl-C did not regain a prompt in the observed window. A second Ctrl-C
caused:

```text
Disconnected from target.
```

The FireSim process was still running on the F2 host and UART had no completion, PASSED, FAILED,
or fatal-error marker at the time of inspection.

Because this workload uses `gdbserver --once`, the inferior cannot be reliably reattached through the
same endpoint after this disconnect. The current run should be terminated before the next instrumented
rerun.

## Static Narrowing After This Run

`dma_blocking_wait()` has several distinct post-`hw_dma_fence()` boundaries:

1. `hw_dma_fence()`
2. `dma_token_fence_scope()` for external RR scope
3. optional release path for non-external scope
4. `dma_token_complete()`
5. `dma_trace_complete_once()`
6. `dma_gdb_marker_wait_return()`

The existing GDB marker coverage only exposes `DMA_WAIT_ENTER` and `DMA_WAIT_RETURN`, which is too
coarse for the current frontier. The next rerun should add or use low-output marker sites around the
post-fence phases, especially:

- after `hw_dma_fence()`
- before shared scope fence
- after shared scope fence
- after token complete
- wait-return

These should be GDB-only marker calls, not guest file logs, so the observation path stays close to the
current low-perturbation setup.

## Current Interpretation

Confirmed:

- The run reached segment2/global stage3/local stage1 worker entry.
- Artifact validation was slow but not stuck.
- At least one cfg0 and one cfg2 `prt_rr_fence_scope()` instance returned through both the CSR swap and ordinary fence.
- The exact hang frontier moved later than the observed shared-fence instances.
- GDB Ctrl-C became unreliable only after continuing past those observed returns.

Not yet proven:

- Whether a later shared-fence instance hangs.
- Whether the post-wait cleanup path (`dma_token_complete`, trace complete, marker return) hangs.
- Whether a pipebuf publish/consumer wait dependency is the next blocker.

Next run should use targeted GDB marker sites instead of broad `prt_rr_fence_scope` entry breakpoints.
