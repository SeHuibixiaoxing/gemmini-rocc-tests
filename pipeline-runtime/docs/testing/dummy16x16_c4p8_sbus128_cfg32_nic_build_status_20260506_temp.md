# Dummy16x16 C4P8 Sbus128 Cfg32 NIC Build Status (2026-05-06)

## Purpose

This is the concurrent lower-pair-count build requested for pipeline-runtime
debugging:

- 4 Rocket cores
- 8 ReRoCC Gemmini/CoupledDMA pair managers
- dummy Gemmini mesh 16x16
- 128-bit sbus/NoC
- cfg32 FireSim F2 build recipe
- NIC enabled
- TraceIO disabled
- ordinary `TIMING` build strategy

The main comparison target is the earlier 12-pair cfg32 NIC noTrace build:

- `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace`
- known failing result directory:
  `sims/firesim/deploy/results-build/2026-05-05--17-14-55-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace/`

This 8-pair build is intended to measure how much resource pressure is removed
by dropping from 12 pair managers to 8 while keeping dummy 16x16 Gemmini meshes
and the 128-bit sbus/NoC.

## Build Inputs

Top-level checkpoint:

- commit: `417f4ee0 Add 8p dummy16x16 sbus128 NIC target`

FireSim submodule checkpoint:

- commit: `f8d612111 Add 8p dummy16x16 sbus128 cfg32 NIC build inputs`

New Scala target path:

- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`
- `generators/firechip/chip/src/main/scala/TargetConfigs.scala`

New FireSim YAMLs:

- `sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus128_cfg32_nic_notrace.yaml`
- `sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus128_cfg32_nic_notrace.yaml`

Target config:

- `FireSimGemminiReRoCCPairDummy16x16C4P8Sbus128NICNoTraceConfig`

Underlying chipyard config:

- `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P8x4x2CoupledDMAPairManagerDummy16x16Sbus128`

DMA-related pair-manager settings intentionally match the current optimized
path:

- `filterDmaVisibleManagers = true`
- `connectSbusSlaveToStl = true`
- shared scratchpad page-table translation shared with CoupledDMA
- `pairTlMaxInFlight = Some(64)`
- `pairAtlMaxInFlight = Some(64)`

## Launch

Launched at `2026-05-06T16:16:12Z`.

Command:

```sh
scripts/firesim-tmux-run.sh \
  --session-name pairdummy16x16-c4p8-sbus128-cfg32-nic-notrace-20260506T161612 \
  buildbitstream \
  -b config_build_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus128_cfg32_nic_notrace.yaml \
  -r config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus128_cfg32_nic_notrace.yaml
```

tmux metadata:

- session: `pairdummy16x16-c4p8-sbus128-cfg32-nic-notrace-20260506T161612`
- pane log:
  `tmp/firesim-aws-f2/tmux/pairdummy16x16-c4p8-sbus128-cfg32-nic-notrace-20260506T161612.pane.log`
- exit code file:
  `tmp/firesim-aws-f2/tmux/pairdummy16x16-c4p8-sbus128-cfg32-nic-notrace-20260506T161612.exitcode`

Build farm tag:

- `pairdummy16x16c4p8sbus128cfg32nicntbf`

## Current State

At launch checkpoint, the job reached local FireSim RTL generation:

- `make ... replace-rtl`
- SBT assembly/loading started
- no AWS build host had been allocated yet for this 8-pair job
- no AGFI/AFI exists yet
- no synthesis utilization report exists yet

The existing 12-pair dummy8x8/sbus64 build remains active in a separate tmux
session:

- `pairdummy8x8-sbus64-cfg32-nic-notrace-20260506T123753`

Its synthesis phase has completed and post-synthesis checkpoint/report commands
have run on remote host `192.168.1.77`, but final build outcome is not yet
proven.

## Resource Comparison Plan

When post-synthesis utilization for this 8-pair build is available, compare it
against:

1. The failed 12-pair dummy16x16/sbus128/cfg32/NIC/noTrace baseline:
   `2026-05-05--17-14-55-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace`.
2. The active 12-pair dummy8x8/sbus64/cfg32/NIC/noTrace experiment, once its
   final reports are copied back.

Primary resource metrics:

- total LUT
- logic LUT
- LUTRAM
- FF
- RAMB36
- RAMB18
- URAM
- DSP

Also check whether the 8-pair build still triggers the same style of downstream
placement/routing resource failure seen in the 12-pair baseline.
