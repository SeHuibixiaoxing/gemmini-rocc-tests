# 20260507T164900Z dummy8x8/sbus64 token548 marker run stalled before DMA evidence

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Type: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-02d10994a4bf7d2af`, private IP `192.168.1.202`
- Top-level commit before run: `d844475f`
- `generators/gemmini` commit before run: `334f2b5`
- `gemmini-rocc-tests` commit before run: `bf8b9f2`

## Marker configuration

The guest image carried this marker filter:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-wait-return
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=2
PIPELINE_RUNTIME_GDB_MARKER_TOKEN=548
```

GDB helper:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=2400 \
PRT_GDB_MARKER_DELETE_AFTER_HIT=1 \
PRT_GDB_POST_MARKER_GDB_CMDS='...' \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  192.168.1.202 172.16.0.2:2345 32345
```

The post-marker commands were intended to prove token548 returned, then set a
conditional breakpoint for the next `stage0/tensor2` `dma_blocking_wait`.

## Result

This run did not reach the token548 DMA marker.

The initial remote GDB attach succeeded and GDB set the source breakpoint on
`prt_gdb_marker_stop()`:

```text
Reading symbols from .../build/rerocc-linux-tests/rerocc_pipeline_runtime-linux...
0x0000003ff7fec386 in ?? () from .../ld-linux-riscv64-lp64d.so.1
Breakpoint 1 at 0x141b4: file .../pipeline-runtime/src/prt_debug_state.c, line 332.
```

After `continue`, the GDB transcript received no marker hit, no Ctrl-C stop
reply, and no later RSP traffic. A manual `SIGINT` to the local GDB process also
failed to recover a stack or prompt.

Run-host evidence immediately before termination:

```text
FireSim-f2: R+ 99.9% CPU
heartbeat mtime: 2026-05-07 16:35:54 UTC
heartbeat tail: 18283561563, 965
tap0 gdbserver TCP: 172.16.0.1:48670 -> 172.16.0.2:2345
  Send-Q=1, rto=32512, backoff=7, bytes_received=17337
  bytes_sent=2819, bytes_acked=2725, bytes_retrans=94
```

Sparse guest log frontier:

```text
[prt-progress] artifacts mapping parse begin ... estimated_entries=16848
[prt-progress] artifacts mapping parse progress ... entries=14336 elapsed_ms=727
```

Breadcrumb frontier:

```text
version=1 enabled=1 slot_count=64 update_count=182
last_kind=runtime last_phase=runtime_init_done line=4785
filters segment=0 global_stage=0 local_stage=0 subbatch=any
```

The breadcrumb is not a good frontier for this run because it is filtered to
segment0/stage0 and does not cover artifact parsing or synthetic model
prefault. The sparse log may also lag if the guest stopped before later flushes.

## Artifacts

Captured under:

```text
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260507T164900Z_dummy8x8_sbus64_token548_marker_init_stall/
```

Key files:

- `gdb/pairdummy-cfg32-marker-stop.gdb`
- `gdb/pairdummy-cfg32-marker-stop.gdb.log`
- `guest/bertmini-batch8.log`
- `guest/bertmini-batch8.gdbserver.info`
- `guest/bertmini-batch8.gdbserver.log`
- `guest/bertmini-batch8.status`
- `guest/bertmini-batch8.runner-proc.stage`
- `guest/bertmini-batch8.breadcrumb.bin`
- `guest/breadcrumb.decode.txt`
- `runhost/state.txt`
- `runhost/heartbeat.csv`
- `runhost/uartlog`
- `manager/runworkload.pane.log`
- `local-ss-tnpi.txt`

The run farm was terminated with the pairdummy workflow's `terminate` command.
AWS returned no active `f2.*` instances after termination.

## Interpretation

- This run cannot be used as evidence for a token548 DMA wait hang. It did not
  produce a marker hit or a conditional DMA wait stop.
- It does reproduce the broader GDB-control problem seen in
  `20260507T144432Z_dummy8x8_sbus64_stack_sample_ctrlc_timeout.md`: after an
  initial successful attach/continue, a later stop request may not return over
  the guest NIC path.
- The visible sparse frontier is still in artifact mapping parse, while the
  previous `20260507T144432Z` run showed that the same initialization window can
  advance into synthetic model prefault before Ctrl-C becomes unreliable.
- Therefore the next probe should not jump directly to token548. It should split
  runtime initialization with earlier source markers or breakpoints:
  artifact-parse-done, synthetic-model-prefault begin/end, runtime-ready, then
  segment-begin.
- If the early init markers all pass, reintroduce the token-level DMA marker. If
  they do not, debug the initialization/GDB-control interaction before treating
  DMA token evidence as valid.

## Next probe

Add low-overhead GDB marker sites around these initialization boundaries:

1. layer mapping parse done
2. artifact validation done
3. synthetic model prefault begin
4. synthetic model prefault end
5. runtime-ready

Then run a fresh gdbserver workload with one early marker at a time. This gives a
binary-searchable path to determine whether the current unrecoverable interval is
still pure software initialization, guest NIC/GDB control under long runtime
execution, or the first hardware-facing pipeline segment.
