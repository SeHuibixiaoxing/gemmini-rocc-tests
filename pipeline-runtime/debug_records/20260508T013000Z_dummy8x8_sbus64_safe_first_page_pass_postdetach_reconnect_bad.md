# 2026-05-08T01:30Z dummy8x8/sbus64 safe GDB helper reached first page accounting

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Runworkload session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-010900`
- Run host: `i-0d82294449ea8bfe8`, private IP `192.168.1.234`
- Guest endpoint: `172.16.0.2:2345`
- Runtime ELF SHA256:
  `062f9dcc6899f355f26d0444bf0d8bf3bc9abe2d146718ac6e076b0c82793a3a`

The guest image was configured with the initial marker stop:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=segment-begin
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1
```

## GDB command

The safe helper was run with expect-managed marker timeouts:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_INITIAL_MARKER_TIMEOUT=1200 \
PRT_GDB_STEP_MARKER_TIMEOUT=1200 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh \
  192.168.1.234 172.16.0.2:2345 32345
```

This helper uses GDB as the first TCP client to the guest `gdbserver` port and
does not use `nc`, `curl`, `telnet`, or a shell-level `timeout` kill.

## Result

The helper passed and printed `PRT_SAFE_SEQUENCE_PASS`. It reached the complete
early segment0 stage0 input DMA chain:

- `site_id=2`: segment-begin, segment0, rc=0.
- `site_id=3`: worker-create, segment0 stage0, `manager_id=0`, `aux0=0`, `aux1=8`.
- `site_id=4`: worker-entry, segment0 stage0, `manager_id=0`, `aux0=1`, `aux1=1`.
- `site_id=12`: first DMA wait enter, tensor0, token1, page0, rc=0.
- `site_id=13`: first DMA wait return, tensor0, token1, page0, rc=0.
- `site_id=25`: submitwait after wait, tensor0, token1, page0, rc=0.
- `site_id=26`: submitwait after cleanup, tensor0, token1, page0, rc=0.
- `site_id=28`: fixed-load submitwait end, tensor0, page0, rc=0.
- `site_id=29`: fixed-load page accounted, tensor0, page0, `aux0=1024`, `aux1=64512`, rc=0.

This is stronger than the older first-DMA result because it proves the first
fixed-load input page reaches the accounting site after the DMA wait/cleanup
sequence.

## Post-detach observation

Do not treat this run as proof that the workload naturally continued after GDB
detach.

After the helper reported detach, the run host stayed alive and FireSim heartbeat
advanced, but the guest image files did not show further runtime progress:

- `bertmini-batch8.status`: `state=running`
- `bertmini-batch8.log`: last coarse progress line was
  `dma-wait-doneflag-poll phase=done token=1 stage=0 tensor=0`.
- `bertmini-batch8.breadcrumb.decode.txt`: `update_count=464`, last slot
  `kind=dma phase=dma_submitwait_after_cleanup`, token1/page0.
- `bertmini-batch8.runner-proc.stage`: recorded `State: t (tracing stop)` and
  `TracerPid: 221` for the inferior at the time of the guest-side proc snapshot.

That proc snapshot may be stale, but it is enough to avoid claiming post-detach
autonomous runtime progress from this run.

## Reconnect attempt

A second connection attempt used GDB, not a raw TCP probe. It connected through a
new SSH tunnel, but it did not recover useful inferior state:

```text
Remote debugging using :32346
Ignoring packet error, continuing...
warning: unrecognized item "timeout" in "qSupported" response
Remote replied unexpectedly to 'vMustReplyEmpty': timeout
No threads.
No stack.
The program has no registers now.
```

The temporary reconnect expect script then exited with rc=1 because it attempted
to send `x/16i $pc` without escaping `$pc` for Tcl. The rc=1 is therefore a
helper-script bug after the useful evidence had already been collected; the
important result is that `gdbserver --once` was not reusable for stack sampling
after the first helper detached.

## Interpretation

- The current hardware and software can reach the first page accounting marker
  under GDB control on AGFI `agfi-077451484fe3b63c3`.
- The unresolved runtime window has moved past token1/page0 accounting.
- A `gdbserver --once` run should be considered single-use. After detach, do not
  plan on a later attach for stack sampling.
- For future marker walks, the helper should gather all required evidence in the
  first GDB session. If the goal is to let the workload run freely after the
  last marker, verify that with an explicit post-detach guest progress check in
  a fresh test.

## Next probe

Use a fresh run and extend the safe helper in one of these directions:

1. Page chain: page0 accounted -> page1 wait enter/return -> page1 accounted ->
   page2 accounted. This tests whether the immediate next fixed-load page is the
   hang boundary.
2. Wider page window: page0 -> page16 -> page24, using the same expect-managed
   timeout path, to validate the older token16/token24 evidence without external
   GDB kills.
3. If DMA-page probing remains too slow, add a no-DMA compute-only bisection run
   that marks fixed-load tensors ready and validates whether the next hang is in
   compute dispatch/export handling instead of DMA page movement.

## Artifacts

Artifacts are archived under:

```text
pipeline-runtime/debug_records/artifacts/20260508T011812Z_dummy8x8_sbus64_safe_first_page_pass/
```

Key files:

- `gdb-helper/expect-driver.stdout`
- `gdb-helper/pairdummy-cfg32-segment0-worker-dma-safe.expect.log`
- `gdb-reconnect-after-detach/reconnect.expect.log`
- `bertmini-batch8.log`
- `bertmini-batch8.status`
- `bertmini-batch8.gdbserver.log`
- `bertmini-batch8.breadcrumb.decode.txt`
- `bertmini-batch8.runner-proc.stage`
- `runworkload.pane.log`
- `runworkload.manager.log`
- `runhost-final-stat.txt`
- `aws-instance.json`

The F2 run host was terminated with:

```bash
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```
