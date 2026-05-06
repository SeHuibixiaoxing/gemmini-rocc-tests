# Dummy8x8 Sbus64 Cfg32 NIC Synthesis Status - 2026-05-06

This is a temporary checkpoint for the 12-pair cfg32 NIC/noTrace FireSim F2 build requested as a lower-resource retry of the failing dummy16x16/sbus128 cfg32 NIC/noTrace build.

## Build Under Test

- Build config: `config_build_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace.yaml`
- Build recipe: `config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace.yaml`
- Target config: `FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig`
- Platform config: `FRFCFS16GBQuadRank_BaseF2Config`
- Frequency: `20`
- Strategy: `TIMING`
- Build tmux session: `pairdummy8x8-sbus64-cfg32-nic-notrace-20260506T123753`
- FireSim manager log: `sims/firesim/deploy/logs/2026-05-06--12-37-54-buildbitstream-KHTQ2UK6ZDIS2B99.log`
- Build host: `i-03ad1ba9110058890`, `z1d.3xlarge`, private IP `192.168.1.77`

## Current Milestone

As of 2026-05-06 16:01 UTC, Vivado synthesis has completed on the remote build host:

- `Synthesis finished with 0 errors, 0 critical warnings and 6328 warnings.`
- `Finished Writing Synthesis Report`
- Peak Vivado memory in the synthesis log: `71659.109 MB`
- At the report-writing line, Vivado reported only `711 MB` free physical memory, but the process did not OOM.
- The Vivado process was still running after synthesis, so implementation/packaging was not finished at this checkpoint.
- The formal `build/reports/*.post_synth_utilization.rpt` file had not appeared yet either locally or on the remote build host.

Because the formal post-synthesis utilization report was not available yet, the comparison below uses the synthesis log's `Report Cell Usage` table for the new build and the formal `post_synth_utilization.rpt` top row for the baseline. Treat it as a provisional same-stage signal, not as the final utilization report.

## Baseline

Baseline failing build:

- Result dir: `sims/firesim/deploy/results-build/2026-05-05--17-14-55-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace/`
- Report: `cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/reports/26_05_05-210145.post_synth_utilization.rpt`
- Previous implementation failure: route failure with `5774 signals failed to route` and `5975 node overlaps`.

Top `cl_firesim` baseline row:

| Metric | Baseline |
|---|---:|
| Total LUT | 1,169,035 |
| Logic LUT | 1,026,303 |
| LUTRAM | 142,006 |
| SRL | 726 |
| FF | 618,967 |
| RAMB36 | 44 |
| RAMB18 | 68 |
| URAM | 8 |
| DSP | 2,079 |

## Provisional New Synthesis Data

From remote Vivado log:

`/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_06-143630.vivado.log`

Important cell usage rows:

| Cell group | Count |
|---|---:|
| LUT1+LUT2+LUT3+LUT4+LUT5+LUT6 | 972,399 |
| FDCE+FDPE+FDR+FDRE+FDS+FDSE | 543,801 |
| RAMB36E2 | 44 |
| RAMB18E2 | 164 |
| URAM288 | 8 |
| DSP_ALU | 1,992 |
| DSP48E1 | 3 |

For DSP, the log exposes 1,992 `DSP_ALU` cells plus 3 `DSP48E1` cells. This is best treated as an approximate DSP-site count of 1,995 until the formal utilization report is available.

## Provisional Resource Delta

| Metric | Baseline | New provisional | Delta | Delta % | Notes |
|---|---:|---:|---:|---:|---|
| Logic LUT-like count | 1,026,303 | 972,399 | -53,904 | -5.3% | Baseline is formal logic LUT; new is log LUT primitive sum. |
| FF | 618,967 | 543,801 | -75,166 | -12.1% | Same practical primitive class. |
| DSP | 2,079 | about 1,995 | about -84 | about -4.0% | Formal new DSP count pending. |
| RAMB36 | 44 | 44 | 0 | 0.0% | Same. |
| RAMB18 | 68 | 164 | +96 | +141.2% | BRAM18 usage increased materially. |
| BRAM36-equivalent | 78 | 126 | +48 | +61.5% | RAMB36 + RAMB18/2. |
| URAM | 8 | 8 | 0 | 0.0% | Same. |

Interpretation:

- Changing each Gemmini dummy core to 8x8 and reducing system bus width to 64 bits did reduce LUT-like count, FFs, and DSPs.
- The reduction is not dramatic at the top level because the design still includes 12 pair managers, NIC, FireSim bridges, memory models, and shell logic.
- BRAM18 usage increased substantially, so this retry is not a uniform resource reduction.
- Total LUT and LUTRAM cannot be compared exactly until the formal new `post_synth_utilization.rpt` appears.

## Similar-Issue Assessment

At this checkpoint, the previous implementation failure has not reappeared:

- No `Place 30-487` failure has appeared.
- No `Route 35-2` or failed-routing signal count has appeared.
- No placement/route step has completed yet, so routability is still unproven.
- The only confirmed pressure point is high synthesis memory on the remote build host. It completed synthesis without OOM, but peak memory was high enough that `z1d.3xlarge` had very little free physical memory at the report-writing point.

The build is therefore past synthesis, but not yet past the previous failing class of issue. The resource trend is partially improved, while BRAM18 pressure got worse and route risk remains open.

## Cleanup Note

No directory cleanup was performed. Earlier read-only checks identified old generated-src directories that could release about 21 GB, but cleanup was not run because the user required explicit confirmation before any cleanup action.

## Formal Post-Synthesis Update - 2026-05-06 16:25 UTC

The formal post-synthesis utilization report is now available on the remote
build host:

```text
/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/reports/26_05_06-161045.post_synth_utilization.rpt
```

Top `cl_firesim` row from that report:

| Metric | Formal post-synth value |
|---|---:|
| Total LUT | 1,044,525 |
| Logic LUT | 925,213 |
| LUTRAM | 118,586 |
| SRL | 726 |
| FF | 551,796 |
| RAMB36 | 44 |
| RAMB18 | 164 |
| URAM | 8 |
| DSP | 1,995 |

Formal delta versus the failed 12-pair dummy16x16/sbus128/cfg32/NIC/noTrace
baseline:

| Metric | Baseline | 12p dummy8x8/sbus64 | Delta | Delta % |
|---|---:|---:|---:|---:|
| Total LUT | 1,169,035 | 1,044,525 | -124,510 | -10.65% |
| Logic LUT | 1,026,303 | 925,213 | -101,090 | -9.85% |
| LUTRAM | 142,006 | 118,586 | -23,420 | -16.49% |
| FF | 618,967 | 551,796 | -67,171 | -10.85% |
| RAMB36 | 44 | 44 | 0 | 0.00% |
| RAMB18 | 68 | 164 | +96 | +141.18% |
| URAM | 8 | 8 | 0 | 0.00% |
| DSP | 2,079 | 1,995 | -84 | -4.04% |

Interpretation update:

- The formal report confirms the earlier provisional conclusion: reducing the
  dummy mesh to 8x8 and cutting sbus/NoC width to 64 bits helps, but only by
  about 10-11% for top-level LUT/FF.
- The small top-level reduction is consistent with large fixed costs outside
  the Gemmini mesh datapath: 12 pair managers, ReRoCC/NoC, FireSim bridges, NIC,
  memory/shell integration, and target/platform glue.
- RAMB18 usage increased substantially, so this is not a monotonic resource
  win.
- The build has progressed past synthesis and into shell `link_design`, but
  placement/routing success is still unproven at this update.

## Placement Progress Update - 2026-05-06 17:31 UTC

The remote Vivado process is still alive on build host `192.168.1.77`.

Observed process:

```text
vivado -mode batch -source build_all.tcl -log 2026_05_06-143630.vivado.log
  -tclargs SSI_SpreadLogic_high AggressiveExplore AggressiveExplore A1 B0 C0 H2
```

Current implementation stage in the Vivado log:

```text
Phase 2 Global Placement
Phase 2.5 Global Place Phase1
```

Recent successful sub-stages:

- `Phase 1 Placer Initialization`
- `Phase 2.1 Floorplanning`
- `Phase 2.2 Physical Synthesis After Floorplan`
- `Phase 2.3 Update Timing before SLR Path Opt`
- `Phase 2.4 Post-Processing in Floorplanning`

Memory at this point:

- Vivado peak memory: about `71.7 GB`
- root filesystem on build host: `193G`, `63G` used, `130G` free

The run has therefore advanced beyond synthesis and into placement. The previous
12-pair dummy16x16/sbus128 failure mode was a later implementation/routing
failure, so this checkpoint still does not prove that the reduced-resource
configuration can complete, but no equivalent placement or routing error has
appeared yet.

## Key Path Audit - 2026-05-06 17:35 UTC

The current post-opt timing report still points at the same shell-side DDR
clock/reset crossings that showed up in the failing 12-pair baseline.

Representative worst path:

- source: `WRAPPER/CL/SH_DDR/SYNC_RST/pipe_reg[3][0]/C`
- path group: `**async_default**`
- path type: recovery
- slack: `-1.284ns`
- data path delay: `0.585ns`
- logic levels: `1` (`LUT2=1`)

Another repeated offender is the DDR UI clock reset path:

- source: `WRAPPER/CL/SH_DDR/genblk1.IS_DDR_PRESENT.DDR4_0/inst/div_clk_rst_r1_reg/C`
- path group: `WRAPPER/CL/clk_main_a0`
- slack: `-0.978ns`

This matters because it means the current timing pain is not obviously coming
from the Gemmini pair-manager reductions. The target changes are still useful
for resource pressure, but the worst slack remains concentrated in shell DDR
reset infrastructure and not in the ReRoCC datapath.

## Resource Hotspot Comparison - 2026-05-06 17:37 UTC

The hierarchical utilization table explains why the LUT/FF drop is modest.
Several fixed-cost blocks barely move, while only the top-level shell/target
container shows a noticeable reduction.

| Block | 12p dummy16x16/sbus128 | 12p dummy8x8/sbus64 | Delta |
|---|---:|---:|---:|
| `firesim_top` total LUT | 1,132,791 | 1,008,281 | -11.0% |
| `ChipTop` total LUT | 1,071,502 | 946,952 | -11.6% |
| `DigitalTop` total LUT | 1,071,502 | 946,952 | -11.6% |
| `ReRoCCManagerTile` total LUT | 40,386 | 38,443 | -4.8% |
| `GemminiCoupledDMAPairWrapper` total LUT | 36,851 | 35,178 | -4.5% |
| `CPUManagedStreamEngine_0` total LUT | 32,252 | 32,252 | ~0% |
| `SimpleNICBridgeModule_0` total LUT | 1,565 | 1,565 | ~0% |
| `IceNIC` total LUT | 4,711 | 4,712 | ~0% |

Interpretation:

- The sbus/mesh reduction helps the shell container, but not enough to remove
  the large fixed costs from NIC, FireSim bridge logic, DDR infrastructure, and
  the 12 repeated ReRoCC manager tiles.
- Most of the remaining pressure is therefore in shared infrastructure and
  replication overhead rather than in the Gemmini array itself.
- That makes the current retry worth continuing, but it also means another
  large LUT/FF reduction should not be expected without changing the bridge or
  repetition structure itself.

## Placement/Physopt Monitor - 2026-05-06 17:57 UTC

Remote host `192.168.1.77` is still running the TIMING Vivado job. The active
Vivado process has accumulated more than 5 hours of CPU time and remains alive.

Latest log/report state:

- Vivado log:
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_06-143630.vivado.log`
- log modified at: `2026-05-06 17:53:35 UTC`
- log size: about `1.75 MB`
- line count: `12319`
- newest report remains the post-opt timing report from `16:42 UTC`
- no route report or final DCP/AGFI completion evidence exists yet

The current tail of the Vivado log ends at:

```text
Phase 2.6.2 Physical Synthesis In Placer | Checksum: 25cf3a1b3
Time (s): cpu = 02:17:41 ; elapsed = 01:10:13 . Memory (MB): peak = 71894.383 ; gain = 207.180 ; free physical = 22519 ; free virtual = 33658
```

Additional grep markers in the same log show later placer sub-steps such as
`Phase 3 Retarget`, `Phase 4 Constant propagation`, and `PBP: Compute
Congestion`, but the final emitted tail still returns to the physical-synthesis
summary above. Treat this as an active placer/physopt checkpoint, not as route
entry.

Important non-fatal messages seen during this stage:

- many `Physopt 32-1132` very-high-fanout messages
- repeated names under
  `CPUManagedStreamEngine_0/SIMPLENICBRIDGEMODULE_0_from_cpu_stream_incomingQueueIO_q/enq_ptr_value_reg[...]`
- Vivado says timing constraints prevent optimizing all loads on these nets
- a small set of shell/PCIS/DDR ready/control nets was replicated by physopt

Interpretation:

- The previous failed build's route-stage signatures have still not reappeared:
  no `Route 35-162`, no `Route 35-2`, no failed-routing count, and no node
  overlap count.
- The NIC bridge queue pointer fanout is a real placement/timing pressure point,
  but at this checkpoint it is still an optimization warning, not a routing
  failure.
- The build remains worth monitoring into actual route before drawing the final
  pass/fail conclusion.

## Detail Placement Monitor - 2026-05-06 18:19 UTC

Remote host `192.168.1.77` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `3h42m`
- parent Vivado CPU: about `164%`
- parent Vivado memory: about `73.2%` of the `z1d.3xlarge`
- Vivado log modified at `2026-05-06 18:18:11 UTC`
- Vivado log size: about `1.75 MB`

The run has now advanced beyond global placement and into detail placement:

```text
Phase 2.6 Global Place Phase2
Phase 2 Global Placement
Phase 3 Detail Placement
Phase 3.1 Commit Multi Column Macros
Phase 3.2 Commit Most Macros & LUTRAMs
Phase 3.3 Small Shape DP
Phase 3.3.1 Small Shape Clustering
Phase 3.3.2 Slice Area Swap
Phase 3.3.2.1 Slice Area Swap Initial
```

The latest completed sub-stage in the log is:

```text
Phase 3.3.2 Slice Area Swap | Checksum: 1dae053dd
Time (s): cpu = 03:19:55 ; elapsed = 01:34:49 . Memory (MB): peak = 71894.383 ; gain = 207.180 ; free physical = 22455 ; free virtual = 33598
```

At this checkpoint, the previous noTrace route-failure markers have still not
reappeared:

- no `Place 30-487`
- no `Place 46-14`
- no `Route 35-445`
- no `Route 35-162`
- no `Route 35-2`
- no failed-routing signal count
- no node-overlap count

Interpretation:

- This is a real progress marker relative to the earlier 17:57 snapshot:
  placement is no longer only in global placement / physical synthesis.
- The build still has not proven routability; the old failure class occurred
  later, during route.
- The current resource reduction remains promising enough to continue, but not
  sufficient evidence for pass probability until route begins and either clears
  or emits congestion diagnostics.

## Post-Placement Optimization Monitor - 2026-05-06 18:41 UTC

Remote host `192.168.1.77` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `4h05m`
- parent Vivado CPU: about `171%`
- parent Vivado memory: about `73.2%`

The run has advanced further than the 18:19 checkpoint:

```text
Phase 3.3 Small Shape DP
Phase 3.4 Re-assign LUT pins
Phase 3.5 Pipeline Register Optimization
Phase 3.6 Fast Optimization
Phase 3 Detail Placement
Phase 4 Post Placement Optimization and Clean-Up
Phase 4.1 Post Commit Optimization
Phase 4.1.1 Post Placement Optimization
Phase 4.1.1.1 BUFG Replication
Phase 4.1.1.2 Post Placement Timing Optimization
```

Latest completed timing line:

```text
Phase 4.1.1.1 BUFG Replication | Checksum: 23a7d6ad4
Time (s): cpu = 04:04:29 ; elapsed = 01:47:17 . Memory (MB): peak = 71894.383 ; gain = 207.180 ; free physical = 22435 ; free virtual = 33579
```

At this checkpoint, the previous failure signatures still have not appeared:

- no `Place 30-487`
- no `Place 46-14`
- no `Route 35-445`
- no `Route 35-162`
- no `Route 35-2`
- no failed-routing signal count
- no node-overlap count

Interpretation:

- This build has now cleared detail placement and reached post-placement timing
  optimization, which is a stronger progress signal than the earlier global
  placement checkpoint.
- It still has not entered the decisive route stage, so it is not yet a
  successful bitstream candidate.
- If route later fails, compare the new failure names against the old noTrace
  failure list before changing RTL; a different failure surface would imply a
  different next experiment.

## Post-Place Monitor - 2026-05-06 19:03 UTC

Remote host `192.168.1.77` remains active.

New reports:

```text
/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/reports/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config.2026_05_06-143630.post_place_timing.rpt
```

Placement completed:

```text
place_design completed successfully
256 Infos, 4 Warnings, 0 Critical Warnings and 0 Errors encountered.
```

However, the placer emitted the same warning class that appeared in the failed
12-pair dummy16x16/sbus128 noTrace route attempt:

```text
WARNING: [Place 46-14] The placer has determined that this design is highly congested and may have difficulty routing.
```

The post-placement estimated congestion table is milder than a route failure,
but still shows broad regions:

| Scope | Notable estimated congestion |
|---|---|
| Overall | west global `16x16`; east/west short `64x64` |
| SLR0 | west short `16x16` |
| SLR1 | west long `8x8`; west short `32x32` |
| SLR2 | west global `16x16`; east/west short `64x64` |

The post-place timing report's worst visible path is again shell-boundary /
PCIS-SLR related:

| Field | Value |
|---|---|
| Slack | `-3.248ns` |
| Path group | `WRAPPER/CL/clk_main_a0` |
| Path type | setup |
| Destination | `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/skid_buffer_reg[22]/D` |
| Data path delay | `3.077ns`, `97.498%` route |
| Crossing | `SLR Crossing[1->2]` |

Current implementation stage:

```text
AWS FPGA: (18:53:02): Start physical-optimizing customer design ...
AWS FPGA: phys_opt command: phys_opt_design -directive AggressiveExplore
Starting Physical Synthesis Task
```

At this checkpoint:

- `Place 46-14` has appeared, so a similar congestion warning is present.
- `Place 30-487` has not appeared.
- `Route 35-445`, `Route 35-162`, `Route 35-2`, failed-routing counts, and
  node-overlap counts have not appeared.
- The build has not yet entered the decisive route stage.

Interpretation:

- The 12p dummy8x8/sbus64 resource reduction was enough to pass placement and
  produce a post-place checkpoint, but not enough to remove Vivado's high
  congestion warning.
- Because the old failure occurred during route after a similar congestion
  warning, this build remains at material route risk.
- The current 8p dummy16x16/sbus128 post-synth result is a stronger resource
  reduction than this 12p dummy8x8/sbus64 experiment and should be monitored as
  the more promising routability candidate.

## Route Entry Monitor - 2026-05-06 19:28 UTC

Remote host `192.168.1.77` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `4h51m`
- parent Vivado CPU: about `186%`
- parent Vivado memory: about `73.2%`
- newest reports remain:
  - post-place timing report from `18:53 UTC`
  - post-phys-opt timing report from `19:20 UTC`

The build completed physical optimization and entered route:

```text
phys_opt_design completed successfully
AWS FPGA: (19:20:50): Start routing customer design ...
AWS FPGA: route command: route_design -tns_cleanup -directive Explore -timing_summary
Command: route_design -tns_cleanup -directive Explore -timing_summary
INFO: [Route 35-270] Using Router directive 'Explore'.
INFO: [Route 35-375] Restored and blocked 360 bleed over nodes.
Phase 2.3 Global Clock Net Routing
Number of Nodes with overlaps = 0
Phase 2.4 Update Timing
```

At this checkpoint, route is still in an early phase and has not failed:

- no `Route 35-445`
- no `Route 35-162`
- no `Route 35-2`
- no failed-routing signal count
- no node-overlap failure
- no implementation `ERROR`

The important negative evidence is the line `Number of Nodes with overlaps = 0`
at global clock net routing. That does not prove final route success, but it
means the previous failed build's large node-overlap count has not appeared in
the early route phases.

Interpretation:

- The 12p dummy8x8/sbus64 experiment is now in the decisive stage. It has
  passed synthesis, placement, and physical optimization under the TIMING
  strategy.
- The earlier `Place 46-14` warning still makes the build risky. The correct
  next action is to keep monitoring route rather than changing RTL before
  `route_design` either completes or emits concrete failed-net diagnostics.
- If it fails, the first artifact to extract is the exact route failure class
  and conflicted-net list, because the resource reductions changed the design
  enough that failure names may differ from the 12p dummy16x16/sbus128 baseline.

## Route Progress Monitor - 2026-05-06 19:49 UTC

Remote host `192.168.1.77` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `5h12m`
- parent Vivado CPU: about `207%`
- parent Vivado memory: about `73.3%`
- newest formal report remains the `19:20 UTC` post-phys-opt timing report
- route has not completed and no post-route report exists yet

The route stage progressed beyond initialization and initial routing:

```text
Phase 2.4 Update Timing
INFO: [Route 35-416] Intermediate Timing Summary | WNS=-3.124 | TNS=-4725.087| WHS=-2.493 | THS=-4396.102|
WARNING: [Route 35-41] Unusually high hold violations were detected on a large number of pins. This may result in high router runtime.
Phase 2.5.1 Update Timing
INFO: [Route 35-416] Intermediate Timing Summary | WNS=-3.124 | TNS=-5118.811| WHS=-3.642 | THS=-6584.831|
Router Utilization Summary
  Number of Failed Nets               = 1398331
  Number of Unrouted Nets             = 946558
  Number of Partially Routed Nets     = 451773
  Number of Node Overlaps             = 0
Phase 3 Global Routing
Phase 4 Initial Routing
INFO: [Route 35-449] Initial Estimated Congestion
INFO: [Route 35-580] Design has 1949 pins with tight setup and hold constraints.
Phase 5 Rip-up And Reroute
Phase 5.1 Global Iteration 0
```

The `Failed Nets` count above is from the early router utilization summary
before rip-up/reroute; it is not the final `Route 35-2` failed-routing result.
At this checkpoint, the previous hard-failure signatures still have not
appeared:

- no `Route 35-445`
- no `Route 35-162`
- no final `Route 35-2`
- no final failed-routing signal count
- no node-overlap failure
- no implementation `ERROR`

Initial estimated congestion is materially milder than the earlier failed
12p dummy16x16/sbus128 route result:

| Direction | Global | Long | Short |
|---|---:|---:|---:|
| NORTH | `4x4`, `0.43%` | `4x4`, `0.71%` | `8x8`, `1.95%` |
| SOUTH | `4x4`, `0.32%` | `4x4`, `0.71%` | `8x8`, `1.07%` |
| EAST | `8x8`, `0.73%` | `8x8`, `0.75%` | `16x16`, `2.90%` |
| WEST | `8x8`, `1.11%` | `16x16`, `1.67%` | `8x8`, `3.32%` |

The tight setup/hold list points at shell/DMA PCIS register-slice pins rather
than target-side Gemmini logic:

```text
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/r.r_pipe/m_payload_i_reg[253]/D
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/r.r_pipe/m_payload_i_reg[188]/D
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/r.r_pipe/m_payload_i_reg[89]/D
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/r.r_pipe/m_payload_i_reg[189]/D
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/r.r_pipe/m_payload_i_reg[439]/D
```

Interpretation:

- The 12p dummy8x8/sbus64 build is still alive in the decisive route stage.
- The main new risk signal is high hold-pressure / runtime risk, not a
  confirmed routing failure.
- Current route evidence is more shell/PCIS dominated than SimpleNIC-queue
  dominated. That weakens the case for changing SimpleNIC queue depth before
  this route either finishes or emits a concrete final failure marker.

## Route Progress Monitor - 2026-05-06 20:11 UTC

Remote host `192.168.1.77` remains active.

Process/resource snapshot:

- parent Vivado elapsed time: about `5h34m`
- parent Vivado CPU: about `232%`
- parent Vivado memory: about `73.3%`
- newest formal report remains the `19:20 UTC` post-phys-opt timing report
- no post-route report exists yet

The route is still in `Phase 5.1 Global Iteration 0`. The new route signal is:

```text
Phase 5 Rip-up And Reroute
Phase 5.1 Global Iteration 0
 Number of Nodes with overlaps = 572508
```

At this checkpoint, this is still an in-progress rip-up/reroute overlap count,
not the final route-status verdict. The hard failure signatures have still not
appeared:

- no `Route 35-162`
- no final `Route 35-2`
- no final failed-routing signal count
- no post-route timing or route-status report
- no implementation `ERROR`

Comparison with the earlier failed 12p dummy16x16/sbus128 noTrace build:

```text
2026-05-06 01:48:08  Number of Nodes with overlaps = 1814681
2026-05-06 02:15:12  Number of Nodes with overlaps = 576771
...
2026-05-06 08:55:57  Number of Nodes with overlaps = 6503
2026-05-06 09:25:23  CRITICAL WARNING: [Route 35-162] 5774 signals failed to route due to routing congestion.
2026-05-06 09:25:29  ERROR: [Route 35-2] Design is not legally routed. There are 5975 node overlaps.
```

Interpretation:

- `572508` overlaps is a serious congestion/routing-risk signal.
- It is lower than the failed 12p dummy16x16/sbus128 build's first visible
  Phase 5.1 overlap count, and roughly comparable to that failed build's second
  overlap checkpoint.
- Therefore it should not be treated as a terminal failure yet. The only
  defensible next action is to keep monitoring until route either converges,
  reports `Route 35-162` / `Route 35-2`, or produces post-route artifacts.
- If the overlap sequence plateaus above zero or ends with `Route 35-2`, collect
  `report_route_status`, failed-signal names, and any post-route DCP/report
  files before changing RTL.
