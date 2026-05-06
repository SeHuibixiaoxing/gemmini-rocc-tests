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

## Remote Synthesis Monitor - 2026-05-06 18:41 UTC

Remote host `192.168.1.129` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `1h08m`
- parent Vivado CPU: about `102%`
- parent Vivado memory: about `73.7%`
- multiple synthesis worker Vivado processes remain active
- no file exists yet in the build `reports/` directory

Latest visible synthesis stage has moved into retiming and memory/timing
advisories:

```text
INFO: [Synth 8-5816] Retiming module ...
INFO: [Common 17-14] Message 'Synth 8-5816' appears 100 times ...
INFO: [Synth 8-7052] The timing for the instance
FASEDMemoryTimingModel_*/readEgress/multiQueue/ram_data_reg
(implemented as a Block RAM) might be sub-optimal ...
```

Interpretation:

- The 8-pair build is still in top-level synthesis.
- It has not reached the post-synthesis utilization report milestone.
- The BRAM optional-output-register advisories are timing-quality warnings, not
  route or resource failure markers.

## Formal Post-Synthesis Update - 2026-05-06 19:03 UTC

The formal post-synthesis utilization report is now available on remote host
`192.168.1.129`:

```text
/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P8Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/reports/26_05_06-185956.post_synth_utilization.rpt
```

Top `cl_firesim` row:

| Metric | 8p dummy16x16/sbus128 |
|---|---:|
| Total LUT | 923,410 |
| Logic LUT | 801,010 |
| LUTRAM | 121,694 |
| SRL | 706 |
| FF | 482,665 |
| RAMB36 | 44 |
| RAMB18 | 68 |
| URAM | 8 |
| DSP | 1,407 |

Formal delta versus the failed 12-pair dummy16x16/sbus128/cfg32/NIC/noTrace
baseline:

| Metric | Failed 12p dummy16x16/sbus128 | 8p dummy16x16/sbus128 | Delta | Delta % |
|---|---:|---:|---:|---:|
| Total LUT | 1,169,035 | 923,410 | -245,625 | -21.01% |
| Logic LUT | 1,026,303 | 801,010 | -225,293 | -21.95% |
| LUTRAM | 142,006 | 121,694 | -20,312 | -14.30% |
| FF | 618,967 | 482,665 | -136,302 | -22.02% |
| RAMB36 | 44 | 44 | 0 | 0.00% |
| RAMB18 | 68 | 68 | 0 | 0.00% |
| URAM | 8 | 8 | 0 | 0.00% |
| DSP | 2,079 | 1,407 | -672 | -32.32% |

Formal delta versus the active 12-pair dummy8x8/sbus64/cfg32/NIC/noTrace
experiment:

| Metric | 12p dummy8x8/sbus64 | 8p dummy16x16/sbus128 | Delta | Delta % |
|---|---:|---:|---:|---:|
| Total LUT | 1,044,525 | 923,410 | -121,115 | -11.60% |
| Logic LUT | 925,213 | 801,010 | -124,203 | -13.42% |
| LUTRAM | 118,586 | 121,694 | +3,108 | +2.62% |
| FF | 551,796 | 482,665 | -69,131 | -12.53% |
| RAMB36 | 44 | 44 | 0 | 0.00% |
| RAMB18 | 164 | 68 | -96 | -58.54% |
| URAM | 8 | 8 | 0 | 0.00% |
| DSP | 1,995 | 1,407 | -588 | -29.47% |

Selected hierarchical rows:

| Block | Total LUT | FF | Notes |
|---|---:|---:|---|
| `firesim_top` | 887,166 | 439,340 | Shell plus target container. |
| `ChipTop` | 825,899 | 417,095 | Target-side chip. |
| `DigitalTop` | 825,899 | 417,095 | Same target-side hierarchy. |
| `CPUManagedStreamEngine_0` | 32,252 | 641 | Still fixed-cost NIC stream engine. |
| `SimpleNICBridgeModule_0` | 1,565 | 1,892 | Still essentially unchanged. |
| `IceNIC` | 4,710 | 2,618 | Still essentially unchanged. |
| One `ReRoCCManagerTile` | about 40,300 | about 23,300 | Eight copies remain. |
| One `GemminiCoupledDMAPairWrapper` | about 36,770 | about 20,448 | Per-pair cost is close to the failed 12p design. |

Interpretation:

- Dropping from 12 pairs to 8 removes far more pressure than only reducing
  Gemmini dummy mesh size and sbus width. The 8p build is about `21%` lower in
  top-level LUT and `22%` lower in FF than the failed 12p dummy16x16/sbus128
  baseline.
- The fixed NIC/stream-engine costs do not shrink. This matches the earlier
  fanout analysis: `CPUManagedStreamEngine_0`, `SimpleNICBridgeModule_0`, and
  `IceNIC` are mostly independent of pair count.
- The per-pair wrapper cost remains close to the failed 12p design. The win is
  primarily fewer replicated pair managers and less surrounding NoC/target
  pressure, not a cheaper individual pair.
- The 8p build is not yet proven routable. It has just crossed the formal
  synthesis resource milestone and is continuing into implementation.

## Post-Opt / Placement Entry Update - 2026-05-06 19:28 UTC

Remote host `192.168.1.129` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `1h55m`
- parent Vivado CPU: about `125%`
- parent Vivado memory: about `73.7%`
- newest report:
  `cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P8Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config.2026_05_06-173316.post_opt_timing.rpt`

The build has now completed `opt_design` and entered placement:

```text
opt_design completed successfully
AWS FPGA: (19:26:40): Start placing customer design ...
AWS FPGA: place command: place_design -directive ExtraNetDelay_high -no_bufg_opt
Command: place_design -directive ExtraNetDelay_high -no_bufg_opt
INFO: [Place 30-611] Multithreading enabled for place_design using a maximum of 8 CPUs
```

No placement congestion conclusion is available yet at this checkpoint:

- no `Place 46-14` warning has appeared so far
- no `Place 30-487` error has appeared
- no route stage has started yet
- no `Route 35-445`, `Route 35-162`, `Route 35-2`, failed-routing signal
  count, or node-overlap failure has appeared

The post-opt timing report still points at the same shell-side DDR
reset/clock-domain paths seen in the other cfg32 NIC builds:

| Field | Value |
|---|---|
| Worst visible slack | `-1.284ns` |
| Source | `WRAPPER/CL/SH_DDR/SYNC_RST/pipe_reg[3][0]/C` |
| Path group | `**async_default**` |
| Path type | recovery |
| Data path delay | `0.585ns`, `80.342%` route |
| Repeated DDR reset path | `WRAPPER/CL/SH_DDR/genblk1.IS_DDR_PRESENT.DDR4_0/inst/div_clk_rst_r1_reg/C` |

Interpretation:

- The 8p build has crossed the second useful implementation milestone:
  `post_synth` and `post_opt` both completed.
- It is still too early to decide routability because placement has only just
  started.
- The timing pain remains dominated by AWS shell DDR reset/clock paths, not by
  an obvious Gemmini or pair-manager datapath path. This matches the earlier
  12p observations and means the immediate pass/fail signal remains routing
  congestion, not the post-opt DDR reset slack itself.

## Placement Progress Monitor - 2026-05-06 19:49 UTC

Remote host `192.168.1.129` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `2h15m`
- parent Vivado CPU: about `135%`
- parent Vivado memory: about `73.8%`
- newest formal report remains the `19:26 UTC` post-opt timing report
- no post-place report exists yet

The build is still in placement and has progressed through placement
initialization into global placement / floorplanning:

```text
Phase 1 Placer Initialization
Phase 1.2 IO Placement/ Clock Placement/ Build Placer Device
Phase 1.3 Build Placer Netlist Model
Phase 1.4 Constrain Clocks/Macros
Phase 2 Global Placement
Phase 2.1 Floorplanning
Phase 2.1.1 Partition Driven Placement
Phase 2.1.1.1 PBP: Partition Driven Placement
Phase 2.1.1.2 PBP: Clock Region Placement
```

New non-fatal placement/floorplan message:

```text
WARNING: [Constraints 18-5648] For reconfigurable module WRAPPER/CL, the top/bottom edges of its PBLOCK pblock_CL are aligned with clock regions. With this type of floorplan, it has limited routability at PBLOCK top/bottom edges due to routing containment requirement.
```

This is a shell/pblock floorplanning constraint message. Vivado then inferred
PROHIBIT on the listed edge sites. It is not equivalent to the `Place 46-14`
high-congestion warning and is not a route failure.

At this checkpoint:

- no `Place 46-14` warning has appeared
- no `Place 30-487` error has appeared
- no post-place checkpoint/report exists yet
- no route stage has started yet
- no route failure marker can be drawn yet

Interpretation:

- The 8p dummy16x16/sbus128 build is still the stronger resource-reduction
  candidate, but it has not reached the decisive placement/route evidence yet.
- The absence of `Place 46-14` at this point is mildly positive, but placement
  is still too early to compare directly against the 12p dummy8x8/sbus64 run.
- Continue monitoring without changing hardware sources or build strategy.

## Placement Progress Monitor - 2026-05-06 20:11 UTC

Remote host `192.168.1.129` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `2h37m`
- parent Vivado CPU: about `147%`
- parent Vivado memory: about `73.9%`
- newest formal report remains the `19:26 UTC` post-opt timing report
- no post-place report exists yet

Placement has progressed farther, but has not reached the decisive checkpoint:

```text
Phase 2.1.1.4 PBP: Compute Congestion
Phase 2.1.1.5 PBP: Macro Placement
Phase 2.1.1.6 PBP: UpdateTiming
Phase 2.1.1.7 PBP: Add part constraints
Phase 2.2 Physical Synthesis After Floorplan
Phase 2.3 Update Timing before SLR Path Opt
Phase 2.4 Post-Processing in Floorplanning
Phase 2.5 Global Place Phase1
```

The post-floorplan physical synthesis pass replicated a small set of target and
bridge nets, including reset-chain nets, `globalNoCDomain` router nets, mbus
AXI queue nets, and ReadyValid bridge nets. This is normal placement/physopt
activity and is not itself a failure marker.

At this checkpoint:

- no `Place 46-14` warning has appeared
- no `Place 30-487` error has appeared
- no post-place checkpoint/report exists
- route has not started
- no route failure marker can be drawn yet

Interpretation:

- The 8p build remains the more promising candidate because its post-synth
  resource count is materially below both 12p runs.
- It is still too early to claim that 8p has solved routability. The next
  useful evidence is whether placement completes without `Place 46-14`, then
  the post-place timing/congestion report.
- Keep monitoring under the current `TIMING` strategy; do not change RTL before
  this build either reaches post-place or fails placement.

## Placement Progress Monitor - 2026-05-06 20:33 UTC

Remote host `192.168.1.129` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `3h00m`
- parent Vivado CPU: about `155%`
- parent Vivado memory: about `73.9%`
- newest formal report remains the `19:26 UTC` post-opt timing report
- no post-place report exists yet

Placement has moved through global place phase 1 and into physical synthesis in
the placer:

```text
Phase 2.5 Global Place Phase1
Phase 2.5 Global Place Phase1 | Checksum: 140fd93af
Phase 2.6 Global Place Phase2
Phase 2.6.1 UpdateTiming Before Physical Synthesis
Phase 2.6.2 Physical Synthesis In Placer
```

The placer physical-synthesis pass performed large LUT combining and some
high-fanout replication:

```text
LUT Combining: optimized 61132 nets or LUTs, removed 61058 cells
Very High Fanout: optimized 5 nets, created 38 new cells
```

The visible very-high-fanout messages include both shell and NIC/stream-side
nets:

- DDR ECC / calibration nets in `SH_DDR`
- `CPUManagedStreamEngine_0/SIMPLENICBRIDGEMODULE_0_from_cpu_stream_incomingQueueIO_q/enq_ptr_value_reg[...]`
- `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/.../S_READY`
- `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR1/.../S_READY`
- `dwidth_adapt_64bits_512bits_0/.../s_ready_i_reg_0`

At this checkpoint:

- no `Place 46-14` warning has appeared
- no `Place 30-487` error has appeared
- no post-place checkpoint/report exists
- route has not started
- no route failure marker can be drawn yet

Interpretation:

- The absence of `Place 46-14` is still encouraging, but placement is not done.
- The high-fanout messages confirm that SimpleNIC/CPU-managed stream queue
  pointers are a real physical-implementation pressure source even in the 8p
  build. They are still not a proven route blocker by themselves.
- Because the 8p build has substantially lower top-level LUT/FF/DSP than the
  12p builds, continue monitoring to post-place before considering any queue
  depth or RTL change.
