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
- no AGFI/AFI exists yet
- no synthesis utilization report exists yet

## Remote Build Update - 2026-05-06 17:31 UTC

The local FireSim generation/driver phase completed far enough to allocate the
remote F2 build host.

Remote build host:

- instance: `i-0b9776ce1493c06c9`
- instance type: `z1d.3xlarge`
- private IP: `192.168.1.129`
- build tag: `pairdummy16x16c4p8sbus128cfg32nicntbf`
- launched: `2026-05-06T17:30:34Z`

At `2026-05-06T17:31:55Z`, SSH inspection showed the instance booted and idle
from the remote side:

- root filesystem: `193G`, `61G` used, `133G` free
- no `vivado`, `build-bitstream`, or `aws_build` process was visible yet
- `/home/ubuntu/firesim-build/.../developer_designs` did not exist yet

Interpretation: the manager-side job had launched the remote host and was still
preparing/synchronizing the AWS F2 collateral. The actual Vivado synthesis stage
had not started on this host at that checkpoint.

## Remote Synthesis Update - 2026-05-06 17:35 UTC

By 17:35 UTC the remote host had entered Vivado synthesis.

Observed process tree on `192.168.1.129`:

- `build-bitstream.sh --strategy TIMING`
- `aws_build_dcp_from_cl.py --mode small_shell`
- `vivado -mode batch -source build_all.tcl -log 2026_05_06-173316.vivado.log`
- `parallel_synth_helper`
- `synth_design -top clk_wiz_0_firesim -part xcvu47p-fsvh2892-2-e -mode out_of_context`

This confirms the 8-pair build is now beyond host boot and collateral sync and
has started actual Vivado work on the remote F2 instance.

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

## Remote Synthesis Monitor - 2026-05-06 17:57 UTC

Remote host `192.168.1.129` is still actively running the TIMING Vivado job.

Observed active process tree:

- `build-bitstream.sh --strategy TIMING`
- `aws_build_dcp_from_cl.py --mode small_shell`
- `vivado -mode batch -source build_all.tcl -log 2026_05_06-173316.vivado.log`
- one parent Vivado plus multiple `parallel_synth_helper` worker processes

The log has not yet reached the main top-level synthesis completion point.
Several small/OOC synthesis subtasks have completed successfully, but the
latest tail is still in the main `synth_design` stream around FireSim bridge,
NIC, and generated target RTL modules.

Latest known remote log:

```text
/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P8Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_06-173316.vivado.log
```

State at this checkpoint:

- Vivado log size: about `795 KB`
- log modified at: `2026-05-06 17:56:58 UTC`
- reports directory exists but remains empty
- no post-synthesis utilization report exists yet
- no `Place 46-14`, `Route 35-445`, `Route 35-162`, `Route 35-2`, or failed
  routing marker can apply yet because implementation has not reached placement
  or route

Interpretation: this 8-pair experiment is still too early for resource
comparison. The next useful milestone is formal `*.post_synth_utilization.rpt`
creation.

## Remote Synthesis Monitor - 2026-05-06 18:19 UTC

Remote host `192.168.1.129` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `46 min`
- parent Vivado CPU: about `102%`
- parent Vivado memory: about `73.7%` of the `z1d.3xlarge`
- seven `parallel_synth_helper` worker Vivado tasks are still running
- Vivado log modified at `2026-05-06 18:12:58 UTC`
- Vivado log size: about `869 KB`

The reports directory is still empty, so no formal post-synthesis utilization
can be parsed yet.

Latest visible synthesis stage:

```text
Start Part Resource Summary
Finished Part Resource Summary
Start Cross Boundary and Area Optimization
```

The tail includes trimming messages for generated Gemmini/DMA command fields,
for example `command_p/stages_*_dram_addr_reg`,
`command_p/stages_*_spad_addr_reg`, and related fields. These are synthesis
trims of unconnected or width-reduced internal registers, not implementation
failure markers.

Interpretation:

- The 8-pair build has progressed deeper into top-level synthesis, but has not
  reached the formal resource-reporting milestone.
- No placement or route conclusion can be drawn yet.
- The next required action remains waiting for
  `*.post_synth_utilization.rpt`, then comparing total LUT, logic LUT, LUTRAM,
  FF, RAMB36/18, URAM, and DSP against the failed 12-pair dummy16x16/sbus128
  baseline and the active 12-pair dummy8x8/sbus64 build.
