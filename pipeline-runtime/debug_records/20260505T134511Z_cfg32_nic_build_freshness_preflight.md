# 20260505T134511Z cfg32_nic build freshness/preflight checkpoint

## Context

This checkpoint records the static/preflight state of the requested
`12p4c128sbus32cfg + optimized DMA + current NIC` FireSim F2 build while the
bitstream build is still running.

Build session:

- tmux session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- pane log:
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-cfg32-nic-mainline-20260505T132956Z.pane.log`
- manager log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-05--13-29-57-buildbitstream-C1F6YJIUOD2RJEE0.log`
- build config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml`
- build recipe:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`

Command launched from `sims/firesim` manager environment via
`scripts/firesim-tmux-run.sh`:

```bash
./scripts/firesim-tmux-run.sh \
  --session-name pairdummy-cfg32-nic-mainline-20260505T132956Z \
  buildbitstream \
  -b config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml \
  -r config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml
```

## Live status

At this checkpoint:

- no tmux exitcode file yet
- tmux session still alive
- the local `GoldenGateMain` process is still running
- no F2/z1d AWS instance is active yet, so this has not reached the Vivado
  build-host phase

The current log tail is still in local generation/GoldenGate output and shows
only the usual unsupported annotation warnings.

## Freshness evidence

Generated source directory:

`/home/ubuntu/chipyard/sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config/`

Staging directory:

`/home/ubuntu/chipyard/sims/firesim-staging/generated-src/firechip.chip.FireSim.WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128/`

Observed evidence:

- `FireSim-generated.const.h` contains `SimpleNICBridgeModule`
- `FireSim-generated.sv` contains `module SimpleNICBridgeModule`
- staging DTS contains:
  - `ice-nic@10016000`
  - `compatible = "ucb-bar,ice-nic"`
  - `reg = <0x0 0x10016000 0x0 0x1000>`
- staging JSON/memmap contains `ice-nic@10016000`
- staging JSON reports `hardware-exec-breakpoint-count: 1` for each of the
  four Rocket CPUs, so this is still the 1BP route, not an 8BP route
- staging DTS/JSON/memmap contains 12 `rerocc-mgr@...` nodes from
  `0x20000` through `0x2b000`
- generated source/config name is the requested
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
- source-side optimized DMA markers are present in
  `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/DMA.scala`:
  `bytes_written_per_beat` and `write_shift`
- source-side Linux ReRoCC userspace header still has `RR_MAX_CFGS 32` in
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_control.h`

## Workflow preflight

The cfg32 NIC gdbserver workflow reports:

- workflow tag: `pairdummy-sbus128-gdbserver-cfg32-nic`
- runtime config:
  `config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic.yaml`
- HWDB:
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml`
- topology: `example_1config`
- `num_cores=4`
- `num_gemmini=12`
- `num_dma=12`
- `pair_manager_mode=1`
- `dma_force_direct_enable=1`
- `gdbserver_enable=1`
- `gdbserver_port=2345`
- network target audit enforced

The wrapper-scoped network audit command:

```bash
bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh network-audit
```

passed with the narrowed target glob:

`*WithNIC*GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`

and found the WithNIC DTS `ice-nic@10016000` node.

## Lightweight software checks

Passed:

```bash
python3 -m py_compile \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/decode_prt_breadcrumb.py \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/triage_prt_capture.py \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/generate_record_category_index.py
```

Passed YAML parse checks for:

- `config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic.yaml`
- `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml`
- `config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml`
- `config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`

## Interpretation

This checkpoint does not prove that the bitstream will place or that the
eventual AGFI will pass remote gdbserver. It does prove that the build currently
in flight is not accidentally using the old non-NIC pairdummy target and is not
an 8BP target. It also confirms that the gdbserver workflow will enforce the
WithNIC generated target before trying to launch/run the FPGA workload.

Next required checkpoint is either:

- build failure: record Vivado/manager failure evidence and commit it; or
- build success: record AGFI/AFI/build result, update/confirm HWDB, then run the
  managed gdbserver workflow and commit the test result.
