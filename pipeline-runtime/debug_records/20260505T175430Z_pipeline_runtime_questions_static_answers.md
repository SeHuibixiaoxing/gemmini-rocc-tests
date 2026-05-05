# 20260505T175430Z pipeline-runtime questions static answers

## Context

While the `cfg32_nic` and `cfg32_nic_notrace` F2 bitstreams are still building, I statically answered the design and risk questions listed in:

- `pipeline-runtime/docs/testing/问题.md`

This is a documentation/static-analysis checkpoint only. It does not claim new FPGA behavior.

## Build status during this checkpoint

- Mainline `pairdummy-cfg32-nic-mainline-20260505T132956Z`: tmux alive, no exitcode, remote Vivado still in synthesis/cross-boundary optimization on z1d build host `i-0479b74dd4de8e428` / `192.168.0.60`; no AGFI/AFI yet.
- noTrace `pairdummy-cfg32-nic-notrace-20260505T171453Z`: tmux alive, no exitcode, GoldenGate has entered `Starting MidasTransforms`; `FireSim-generated.sv` not present yet, so no generated-SV marker audit yet.

## Static facts recorded

- Action-level manager ownership is partly implemented:
  `prt_action_alloc_acc()` assigns stage manager sets, `prt_action_bind_topology()` installs them into `prt_action_exec_t`, and DMA submission now checks stage-manager ownership.
- The current debug-safe runtime mode remains blocking:
  DMA waits through `hw_dma_fence()`, Gemmini tasks use blocking fence unless the split helper already fenced, and ReRoCC release performs a same-cfg CSR readback.
- SPM page pool sizing now uses `prt_cfg_spm_manager_count()`, which prefers `num_gemmini_mgrs` and avoids the old 4-core/12-manager page-domain mismatch.
- Completion flags are no longer transient stack addresses: the runtime has a page-aligned, prefaulted, `mlock()`ed completion pool with per-slot VA->PA conversion.
- `GemminiCoupledDMA` still writes a completion flag TileLink Put after copy completion; `hw_dma_fence()` corresponds to manager-idle completion, not per-token completion.
- `stage_prepare_exec_views()` remains the important gap for slot-stable SPM binding: action-level allocation exists, but runtime stage execution can still prepare/bind/flush views dynamically.

## New document

- `pipeline-runtime/docs/testing/pipeline_runtime_questions_answers_20260505_draft.md`

## Verification

No new compile/test was run for this documentation checkpoint. It relies on the earlier 2026-05-05 host build, artifact audit and CPU dry-run checkpoint:

- `pipeline-runtime/debug_records/20260505T173746Z_contract_microtests_while_cfg32_builds.md`

## Next

- Continue monitoring both bitstream builds at the requested 1200s cadence.
- When noTrace `FireSim-generated.sv` appears, audit NIC/optimized-DMA/no-Trace markers before remote build upload.
- When either bitstream succeeds, update HWDB, run `infrasetup`, and validate remote gdbserver attach on the new AGFI.
