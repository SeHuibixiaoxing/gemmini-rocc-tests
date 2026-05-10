# 20260510T121300Z - no-DMA segment2/stage2 worker-entry any hit

## Context

Purpose: rerun the segment2/stage2 no-DMA gdbserver marker after the static
audit showed that `worker-entry` cannot use an exact subbatch filter.

Target:

```text
AGFI: agfi-077451484fe3b63c3
run host: i-0c6463b5954ba38ec / 192.168.1.123
workflow: pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh
runworkload: pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260510-115137
runtime config: config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml
```

Guest environment was confirmed locally and remotely:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=4
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=2
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=any
TRACE_ENABLE=0
PIPELINE_RUNTIME_GUEST_LOG_ENABLE=0
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0
PIPELINE_RUNTIME_BREADCRUMB_ENABLE=0
```

Image/env:

```text
effective /firemarshal.env sha256:
ad0fcebd1ea67759cc9e854cc37aa4b7e241ef81543509187ce6934914e898a7

runtime binary sha256:
0d126d06d4186316961aff539e9f72670fe8eabbd13a457a79172842f87e3a56

remote image sha256:
5f413c75f1c7cc23dac1df0f15682d1c1f546d34088f11c1d1ae16fee01d3912
```

The `gdbserver --once` guest TCP port was not probed with `nc`, `curl`, or
`telnet`. The first connection was cross-GDB through the SSH tunnel.

## Result

Corrected `worker-entry/subbatch=any` hit:

```text
Thread 2 "rerocc_pipeline" hit Breakpoint 1, prt_gdb_marker_stop()

g_prt_gdb_marker_state:
  site_id = 4
  segment_idx = 2
  global_stage_id = 4
  local_stage_id = 2
  subbatch_id = 4294967295
  manager_id = 8
  aux0 = 2
  aux1 = 1
  line = 4256
```

Backtrace:

```text
prt_gdb_marker_stop()
prt_gdb_marker_note(... site_id=4 ... subbatch_id=4294967295 ...)
prt_runtime_gdb_marker(... line=4256 ...)
stage_worker_main() at prt_runtime.c:4249
```

This proves segment2/global stage4/local stage2 worker creation and entry. It
also directly confirms the previous static conclusion: `worker-entry` has no
concrete subbatch id, so exact `SUBBATCH=0` filtering is invalid there.

At marker hit, other threads were already active in segment2. One thread was in
the stage1 Gemmini pointwise path under `stage_worker_main():4572`, and the main
thread was in `prt_runtime_run():5489`.

## Helper Failure

The current C7 frontier helper then hit its C7 wait-enter breakpoint, but the
GDB commands block tried to print `g_prt_debug_tls_state`. Current remote
gdbserver failed that TLS lookup:

```text
Cannot find thread-local storage for Thread 216.244 ...
Remote target failed to process qGetTLSAddr request
```

That is a helper bug, not a runtime blocker. It interrupted the commands block
and left GDB at a prompt. The helper process was terminated to avoid leaving the
target stopped indefinitely.

The heartbeat at cleanup was:

```text
18292942991, 965
```

Because the target had been stopped by GDB/helper failure, this heartbeat value
is not a natural no-DMA runtime frontier.

## Artifact

Evidence was archived at:

```text
pipeline-runtime/debug_records/artifacts/20260510T1209_no_dma_segment2_stage2_worker_entry_any_hit_tls_helper_bug/
```

Included files:

- `gdb-helper/pairdummy-cfg32-marker-frontier.expect.log`
- `uartlog`
- `heartbeat.csv`
- `runworkload.pane.log`
- `runworkload.watchdog.log`
- `manager-runworkload.log`
- `runhost-status.txt`
- `aws-instance.json`

The F2 run farm was terminated after archival; the instance was observed in
`shutting-down` state.

## Local Fix

The helper was fixed after this run:

- removed `print g_prt_debug_tls_state`;
- removed misleading global debug-state prints from the C7 milestones;
- C7 milestones now use function arguments / pointed structures:
  `offset`, `*rb`, `*buf`, `*buf->ring`, plus a short `bt`.

No runtime behavior was changed.

## Debugging Custom Instructions More Effectively

The current difficulty is that Gemmini, ReRoCC, and DMA custom instructions can
enter waits where software interrupt, GDB single-step, and backtrace recovery
are unreliable. Practical mitigations:

1. Add explicit software-side phase markers immediately before and after every
   custom-instruction cluster. The marker should publish compact state into a
   non-TLS global snapshot before entering the instruction.
2. Prefer trap-marker bisection over stepping. Stop at coarse phase boundaries,
   then split the interval by adding one new marker, not by single-stepping
   through custom instructions.
3. Keep a no-DMA / no-custom-transfer execution mode as a control. It should
   preserve scheduler and pipebuf semantics while replacing hardware waits with
   deterministic software completion.
4. Add bounded hardware-facing counters/status snapshots that software can read
   after a timeout: outstanding DMA tokens, manager busy bits, ReRoCC queue
   head/tail, and last issued custom instruction metadata.
5. Build tiny bare-metal micro-repros per instruction family. Validate one DMA
   fence, one Gemmini fence, and one ReRoCC release path independently before
   composing them in the full runtime.
6. Avoid GDB features that require target-side TLS, heavy stack walking, or hot
   conditional breakpoints. Use function arguments, explicit debug snapshots,
   and one-shot breakpoints.
7. For performance comparison, never use GDB evidence. Use summary-only runtime
   counters/traces after a low-noise correctness profile has already completed.
