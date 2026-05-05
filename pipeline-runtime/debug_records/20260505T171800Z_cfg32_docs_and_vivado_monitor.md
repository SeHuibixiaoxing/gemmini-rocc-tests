# 20260505T171800Z cfg32 NIC docs update and Vivado monitor

## Context

This checkpoint was taken while waiting for the `12p4c128sbus32cfg + optimized DMA + current NIC`
F2 build intended for pipeline-runtime remote `gdbserver` debugging.

Build session:

- tmux session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- pane log: `tmp/firesim-aws-f2/tmux/pairdummy-cfg32-nic-mainline-20260505T132956Z.pane.log`
- manager log: `sims/firesim/deploy/logs/2026-05-05--13-29-57-buildbitstream-C1F6YJIUOD2RJEE0.log`
- build host: `i-0479b74dd4de8e428`
- build host private IP: `192.168.0.60`
- remote Vivado log:
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_05-165008.vivado.log`

## Build status at this checkpoint

- no `exitcode` file yet
- buildbitstream tmux session is alive
- z1d build host is running
- GoldenGate and remote source copy are complete
- Vivado customer CL build is in RTL elaboration / synthesis
- pane log shows `Finished RTL Elaboration`
- remote Vivado log already contains multiple `synth_design completed successfully` entries
- no AGFI/AFI yet
- no post-synth utilization, placement, route, or timing report found yet

This means there is still no evidence to update HWDB or start `cfg32_nic` runfarm testing.
Continue 1200s polling until a success/failure result or at least post-synth utilization is available.

## Documentation changes made

Updated:

- `docs/testing/gdbserver_integration_sop.md`
- `docs/testing/pipeline_runtime_hybridmapper_requirements_draft.md`
- `docs/testing/pipeline_runtime_questions_answers_20260505_draft.md`
- `docs/testing/pipeline_runtime_optimization_actions_20260505_draft.md`

Main additions:

- SOP now records the current cfg32 NIC build status as remote Vivado synthesis, not local GoldenGate.
- SOP now requires a git checkpoint after AGFI/HWDB update and before runfarm launch.
- SOP now defines first-pass cfg32 NIC `gdbserver` attach criteria.
- Requirements draft now has a frozen baseline for action ownership, SPM stability, manager sharing, DMA completion, and artifact contracts.
- Questions draft now explains the SPM xlate reserved ReRoCC cfg slot and action-scoped manager programming.
- Optimization draft now breaks SPM stable-binding work into staged steps and clarifies the no-DMA compute split.

## Static conclusion

Current cfg32 workflow still forces a conservative debug path when `spm_xlate_enable=1`:

- `sync_mode = PRT_SYNC_MODE_BLOCKING_DEBUG`
- `dma_backend = PRT_DMA_BACKEND_BLOCKING_FENCE`
- `gemmini_mode = PRT_GEMMINI_MODE_BLOCKING_FENCE`

So the first FPGA `gdbserver` run should prioritize:

1. IceNIC / IPv4 / `gdbserver` attach.
2. Runtime init contract: `cores=4`, `gemmini=12`, `dma=12`, `spm_mgrs=12`, `pages_per_acc=1024`, `page_bytes=1024`.
3. Fixed-load DMA fence / shared ReRoCC fence.
4. SPM xlate reserved cfg acquire/fence/release.
5. Gemmini blocking fence.
6. Pipe/ring wait if all workers are blocked in buffer synchronization.

It should not start by assuming async DMA/Gemmini overlap is the current primary failure mode.

## Validation

Command:

```bash
git -C generators/gemmini/software/gemmini-rocc-tests diff --check -- \
  pipeline-runtime/docs/testing/gdbserver_integration_sop.md \
  pipeline-runtime/docs/testing/pipeline_runtime_hybridmapper_requirements_draft.md \
  pipeline-runtime/docs/testing/pipeline_runtime_questions_answers_20260505_draft.md \
  pipeline-runtime/docs/testing/pipeline_runtime_optimization_actions_20260505_draft.md
```

Result: pass.
