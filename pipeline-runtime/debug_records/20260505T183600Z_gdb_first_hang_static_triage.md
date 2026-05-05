# 20260505T183600Z - GDB first-hang static triage

## Goal

Continue static triage while the cfg32 NIC bitstreams are still building. This
checkpoint documents the first live-GDB classification tree for pipeline-runtime
hangs on the pending `12p4c128sbus32cfg + optimized DMA + current NIC` AGFI.

## Build State At Checkpoint

- Mainline cfg32 NIC build: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
  still alive on z1d host `i-0479b74dd4de8e428` / `192.168.0.60`.
  Vivado had completed the large synthesis `Finished Renaming Generated Ports`
  step, with no placement, route, AGFI, or AFI yet.
- no-TraceIO fallback: `pairdummy-cfg32-nic-notrace-20260505T171453Z`
  still alive in local GoldenGate. Intermediate output had reached
  `post-autocounter.fir/json`; `FireSim-generated.sv` was not present.
- No F2 runfarm was running.

## Static Review

Files read:

- `src/prt_runtime.c`
- `src/prt_gemmini_adapter.c`
- `src/prt_dma.c`
- `src/main.c`

Key findings:

- With `spm_xlate_enable=1`, runtime init forces the conservative
  `blocking_debug` / `blocking_fence` / `blocking_fence` path.
- In that path, normal worker execution does not enter
  `stage_overlap_prefetch_entries()`. First live hangs should not be classified
  as DMA/Gemmini overlap until GDB shows otherwise.
- The likely blocking boundaries are fixed-load DMA, SPM xlate flush, Gemmini
  issue/fence, export DMA, pipe/ring waits, or main waiting in `pthread_join()`
  while a worker is stuck inside one of the hardware waits.
- `hw_dma_fence()`, `gemmini_fence()`, and `prt_rr_fence_scope()` still have no
  software timeout around the actual custom-instruction wait.

## Artifact

Wrote:

- `docs/testing/pipeline_runtime_gdb_first_hang_static_triage_20260505.md`

## Interpretation

The current software tree remains a reasonable first target for live GDB. The
first attach should prioritize `info threads` and `thread apply all bt` before
single-stepping. If the PC is in a hardware wait, the next action is to inspect
stage/manager/token variables and hardware status. If the PC is in a pthread
condition wait, the next action is scheduler/buffer-state triage.

