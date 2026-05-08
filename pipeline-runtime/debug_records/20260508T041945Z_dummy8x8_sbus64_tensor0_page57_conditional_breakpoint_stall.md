# 20260508T041945Z dummy8x8 sbus64 tensor0 page57 conditional-breakpoint stall

## Context

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow: `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-0d9be4b18e77beac1`, private IP `192.168.1.183`
- Workload results dir: `sims/firesim/deploy/results-workload/2026-05-08--04-10-32-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Artifact bundle: `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260508T041945Z_dummy8x8_sbus64_early_stall_live/`

## GDB Test

Connected as the first TCP client to guest gdbserver at `172.16.0.2:2345`
through an SSH tunnel via the run-host private IP.

Helper:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02
PRT_GDB_FRONTIER_FUNC=dma_submit_wait_annotated_scoped
PRT_GDB_FRONTIER_CONDITION='stage_idx == 0 && tensor_id == 2 && debug_page_idx == 47'
PRT_GDB_FRONTIER_TIMEOUT=1200
PRT_GDB_POST_HIT_MODE=path_trace
PRT_GDB_PATH_TRACE_TIMEOUT=240
PRT_GDB_PATH_TRACE_MAX_STOPS=10
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.183 172.16.0.2:2345 32345
```

The helper attached and set the conditional source breakpoint, but the frontier
did not hit. A manual Ctrl-C sent to local GDB did not recover a prompt. After
capturing live artifacts, the helper process group was terminated and the run
farm was force-terminated.

## Observations

1. Sparse log stopped shortly after the first worker entered stage 0:

   ```text
   worker stage=0 ready entries=1 exports=1 isolate_pairs=0 shared_pairs=0 acc=0 dma=0 tiles=8
   rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1
   segment=0 sink-progress=0/8 elapsed_ms=1018 fatal=0 stop=0
   ```

2. Breadcrumb is more precise than the sparse log. It shows fixed-load input
   DMA advanced through page 56, then stopped at page 57 before entering
   `dma_submit_wait_annotated_scoped()`:

   ```text
   version=1 enabled=1 slot_count=64 update_count=1472
   last_kind=dma last_phase=dma_page_begin
   seg=0 gstage=0 lstage=0 sb=0 tensor=0 mgr=0 page=57
   src=0x104edd800 dst=0x40101c00 bytes=0x400 timeout_ns=0x989680 line=219
   ```

3. The last complete page-level breadcrumb before the stall was:

   ```text
   page=56 phase=dma_page_end
   page=56 phase=dma_submitwait_after_cleanup tok=57 rc=0
   ```

4. There is no `dma_submit_begin`, `dma_rr_postcheck`,
   `dma_program_begin`, or `dma_wait_before_fence` breadcrumb for page 57.
   Therefore this run does not prove a page57 DMA fence hang. It stopped
   before the first breadcrumb inside `prt_dma_submit()`.

5. The current breakpoint placement is a strong suspect. A GDB conditional
   source breakpoint on `dma_submit_wait_annotated_scoped()` traps on every DMA
   page and lets host GDB evaluate:

   ```gdb
   stage_idx == 0 && tensor_id == 2 && debug_page_idx == 47
   ```

   The observed breadcrumb frontier is exactly before the call for
   `tensor=0/page=57`, so the run may be stalled in GDB/gdbserver conditional
   breakpoint handling rather than in the runtime or accelerator path.

6. doneflag remains invalid as completion evidence. This run did not use
   doneflag polling as the completion decision; completion remains tied to the
   blocking `hw_dma_fence()` path.

## Artifacts

Captured files include:

- `bertmini-batch8.log`
- `bertmini-batch8.breadcrumb.bin`
- `breadcrumb.decoded.txt`
- `triage.txt`
- `bertmini-batch8.gdbserver.info`
- `bertmini-batch8.gdbserver.log`
- `prt-uartlog-live.txt`
- `prt-heartbeat-live.csv`
- `gdb/pairdummy-cfg32-dma-frontier.expect`
- `gdb/pairdummy-cfg32-dma-frontier.expect.log`
- `gdb/expect-driver.stdout`

The run farm was terminated with:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

FireSim reported termination for `i-0d9be4b18e77beac1`; AWS state moved to
`shutting-down` during cleanup.

## Tooling Changes After This Run

Two host-side helper updates were made for the next probe:

1. `run_pairdummy_cfg32_gdbserver_dma_frontier.sh` now tries Ctrl-C and captures
   a basic stack/register window when the initial frontier breakpoint times out.
2. `run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh` was added. It is
   intended for guest-side marker-gated stops at a single fixed-load page, then
   one-shot breakpoints on the immediate DMA submit/wait path.

## Interpretation

This run should be classified as a GDB-observation disturbance, not as a new
hardware or runtime DMA frontier. The page57 breadcrumb is useful because it
shows where the disturbance landed, but the hot conditional breakpoint makes
the evidence unsafe for claiming a real page57 DMA hang.

The previous page46 result remains the stronger runtime evidence:

- page46 tensor2 export completed through `hw_dma_fence()` and shared
  ReRoCC fence/release;
- the previous unresolved frontier was page47 tensor2 export;
- this run did not reach tensor2/page47 because GDB control appears to have
  stalled while filtering earlier fixed-load calls.

## Next Probe

Run a fresh gdbserver workload with a guest-side marker instead of a hot host
conditional breakpoint:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-fixed-load-submitwait-begin
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=0
PIPELINE_RUNTIME_GDB_MARKER_PAGE=57
```

After `infrasetup` patches `/firemarshal.env`, attach with:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FIXED_LOAD_PAGE=57 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

If the marker-gated run crosses page57 cleanly, return to tensor2/page47 using
the same marker-gated pattern. If it stalls before the immediate
`dma_submit_wait_annotated_scoped()` tbreak, then the issue is in the marker/GDB
control path. If it reaches `before hw_dma_fence` but not `after hw_dma_fence`,
then page57 becomes a real DMA wait frontier.
