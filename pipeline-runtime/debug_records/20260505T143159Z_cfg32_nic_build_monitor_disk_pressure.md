# 20260505T143159Z - cfg32 NIC build monitor and disk-pressure intervention

## Context

Active build:

- tmux: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- command:
  `scripts/firesim-tmux-run.sh --session-name pairdummy-cfg32-nic-mainline-20260505T132956Z buildbitstream -b config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml -r config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`
- build recipe:
  `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic`
- target:
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
- platform:
  `FRFCFS16GBQuadRank_BaseF2Config`
- strategy:
  `build_strategy: TIMING`
- frequency:
  `20 MHz`

This is the timing-strategy build requested for the pipeline-runtime gdbserver
debug target. It is not the earlier non-TIMING experiment.

## Observation

At `2026-05-05T143159Z`:

```text
no-exitcode
tmux-alive
GoldenGateMain elapsed: about 59m37s
Java RSS: about 12.9 GiB
pane log mtime: 2026-05-05 13:32:23 UTC
manager log mtime: 2026-05-05 13:32:23 UTC
AWS z1d/f2 instances: none observed
```

Before cleanup, root disk had only about `2.3G` available:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/root       290G  288G  2.3G 100% /
```

Memory pressure was also visible:

```text
Mem: 15Gi total, about 14Gi used
Swap: 41Gi total, about 10Gi used
```

This means the current delay is local GoldenGate resource pressure, not AWS F2
capacity and not a Vivado/TIMING failure yet.

## Cleanup Performed

Deleted only generated FireSim collateral and old build-result directories. The
active WithNIC Sbus128 generated-src directory was not deleted, and the
user-provided known-good 1BP result directory
`2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz` was preserved.

Generated-src removed:

- `sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config`
- `sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2P2x1x2CoupledDMAPairManagerDefault4x4Sbus128-FRFCFS16GBQuadRank_BaseF2Config`

Old build results removed:

- `sims/firesim/deploy/results-build/2026-03-15--14-05-31-firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
- `sims/firesim/deploy/results-build/2026-04-02--15-16-47-firesim_gemmini_rerocc_globalnoc_coupleddma_4c4g4d_20mhz`
- `sims/firesim/deploy/results-build/2026-04-02--20-23-40-firesim_gemmini_rerocc_globalnoc_coupleddma_4c4g4d_20mhz`
- `sims/firesim/deploy/results-build/2026-04-20--04-47-09-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128`
- `sims/firesim/deploy/results-build/2026-04-20--06-40-09-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128`
- `sims/firesim/deploy/results-build/2026-04-21--09-12-59-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic`
- `sims/firesim/deploy/results-build/2026-04-23--15-08-12-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic`
- `sims/firesim/deploy/results-build/2026-04-27--06-30-20-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32`

After cleanup:

```text
Filesystem      Size  Used Avail Use% Mounted on
/dev/root       290G  250G   41G  87% /

41G  sims/firesim/deploy/results-build
13G  sims/firesim/sim/generated-src
4.6G sims/firesim-staging/generated-src
```

## Current Interpretation

- Continue the active build; do not restart solely because of the slow
  GoldenGate phase.
- Watch whether `GoldenGateMain` eventually refreshes files under the active
  WithNIC Sbus128 generated-src directory and whether the z1d build host appears.
- If this build fails later, separate local GoldenGate memory/disk pressure from
  remote Vivado/TIMING failures in the postmortem.

## Limitations

No AGFI/AFI exists yet for this build. No FPGA runworkload or gdbserver test has
been performed for the cfg32 NIC target at this checkpoint.
