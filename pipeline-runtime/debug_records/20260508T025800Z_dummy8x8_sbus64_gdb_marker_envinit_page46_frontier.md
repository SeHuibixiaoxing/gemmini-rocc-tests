# 2026-05-08 02:58 UTC: dummy8x8 sbus64 gdbserver marker miss, doneflag constraint, page46 frontier

## Hardware and workload

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Results dir:
  `sims/firesim/deploy/results-workload/2026-05-08--02-24-55-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Run host: `i-0f327caf647e4c858`, private IP `192.168.1.244`

## GDB helper result

Command launched:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_INITIAL_MARKER_TIMEOUT=1200 \
PRT_GDB_STEP_MARKER_TIMEOUT=300 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh \
  192.168.1.244 172.16.0.2:2345 32345
```

The helper connected as the first TCP client and set `prt_gdb_marker_stop`, but
did not hit `segment-begin-env`. Guest log and breadcrumb showed that the
runtime had already progressed far beyond segment begin, so this is not evidence
that the runtime failed before segment 0.

Static root cause for the marker miss:

- The old helper wrote `g_prt_gdb_marker_filter` immediately after
  `target remote`.
- `prt_runtime_init()` later called `prt_gdb_marker_init_from_env()`.
- The fixed guest profile keeps marker env disabled, so runtime env init
  overwrote the helper's `enabled=1` setting.

A host-side helper fix was committed:

- rocc-tests: `e6033d1` (`Wait for marker env init in safe GDB helper`)
- Gemmini pointer: `a5ff603`
- top-level pointer: `7fe49de3`

This run should not be used as a valid marker-chain pass/fail result. The helper
script was also edited while the old shell instance was still running, so the
old shell reported a parse failure after the remote connection closed. The
captured GDB transcript remains useful only for proving the early marker miss.

## Doneflag constraint

The doneflag rule is now recorded as a hard constraint:

- `docs/constraints/hard_constraints.md` states that DMA doneflag is known bad
  as completion semantics.
- `docs/reference/linux_dma_guardrails.md` states that doneflag polling cannot
  be DMA completion evidence.
- Root `AGENTS.md` now repeats the same repository-level rule.

Static confirmation in current code:

- `dma_blocking_wait_poll_timeout_enabled()` returns `0`.
- `dma_blocking_wait()` therefore cannot use `dma_blocking_wait_poll_doneflag()`
  to bypass `hw_dma_fence()`.
- doneflag paths remain as telemetry / completion-flag allocation and logging,
  but not as completion authority.

## Runtime frontier from breadcrumb

Breadcrumb artifact:

- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/bertmini-batch8.breadcrumb.decoded.txt`

Decoded summary:

- `update_count=16789`
- `last_kind=dma`
- `last_phase=dma_page_begin`
- `segment=0`
- `local_stage=0`
- `subbatch=3`
- `tensor=2`
- `manager=0`
- `page=46`
- request:
  - `src=0x40603400`
  - `dst=0x104df1c00`
  - `bytes=0x400`
  - `timeout_ns=0x12a05f200` (`5s`)

Nearby breadcrumb evidence shows prior pages completing:

- page 44: `dma_submitwait_after_cleanup`, then `dma_page_after_accounting`
- page 45: `dma_submitwait_after_cleanup`, then `dma_page_after_accounting`
- page 46: `dma_page_direct_path_decided`, then `dma_page_begin`

No later `dma_submitwait_after_wait`, `dma_submitwait_after_cleanup`, or
`dma_wait_before_fence` breadcrumb was captured for page 46 in the saved ring.
The next useful GDB target is therefore around export tensor2 page46
`DMA_EXPORT_PAGE_SUBMIT_BEGIN` / `prt_dma_submit()` /
`dma_submit_wait_annotated_scoped()`, not the old segment-begin marker.

## Resource cleanup

- `i-0f327caf647e4c858` reached `terminated`.
- A final EC2 query for `f2.6xlarge`, `f2.12xlarge`, and `f2.48xlarge` in
  `pending/running/stopping/stopped/shutting-down` returned no instances.
- Stale local runworkload tmux sessions and polling Python processes for this
  dummy8x8 gdbserver config were stopped after EC2 termination.

## Artifacts

- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/expect-driver.stdout`
- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/expect-driver.stderr`
- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/gdb-transcript.log`
- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/bertmini-batch8.breadcrumb.bin`
- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/bertmini-batch8.breadcrumb.decoded.txt`
- `debug_records/artifacts/20260508T025800Z_dummy8x8_sbus64_gdb_marker_envinit_page46_frontier/runworkload-tail.log`
