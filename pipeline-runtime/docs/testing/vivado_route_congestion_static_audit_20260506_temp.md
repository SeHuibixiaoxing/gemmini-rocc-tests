# Vivado Route Congestion Static Audit - 2026-05-06

This note records a static audit of the failed 12-pair dummy16x16/sbus128
cfg32/NIC/noTrace TIMING build and explains how it should guide the active
12-pair dummy8x8/sbus64 and 8-pair dummy16x16/sbus128 builds.

## Failed Baseline

Failed build:

```text
sims/firesim/deploy/results-build/2026-05-05--17-14-55-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace/
```

Vivado log:

```text
cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_05-191909.vivado.log
```

Route command:

```text
route_design -tns_cleanup -directive Explore -timing_summary
```

## Failure Signature

Placement already warned that the design was hard to route:

```text
WARNING: [Place 46-14] The placer has determined that this design is highly congested and may have difficulty routing.
INFO: [Place 30-746] Post Placement Timing Summary WNS=-3.250.
```

Routing then reported local routing congestion and high pin utilization:

```text
INFO: [Route 35-445] Local routing congestion detected. At least 1308 CLBs have high pin utilization.
INFO: [Route 35-448] Estimated Global/Short routing congestion is level 5 (32x32).
INFO: [Route 35-581] Estimated Timing congestion is level 5 (32x32).
INFO: [Route 35-580] Design has 1948 pins with tight setup and hold constraints.
```

The final failure was legal routability, not just timing:

```text
CRITICAL WARNING: [Route 35-162] 5774 signals failed to route due to routing congestion.
ERROR: [Route 35-2] Design is not legally routed. There are 5975 node overlaps.
ERROR: [Constraints 18-1000] Routing results verification failed due to partially-conflicted nets.
```

The route phase consumed about 8.7 elapsed hours and peaked at about `71.96 GB`
RSS before failing.

## Congestion Area

The reported congestion region was concentrated around these INT tile ranges:

```text
North: INT_X104Y440 -> INT_X111Y447, INT_X96Y400 -> INT_X103Y407
South: INT_X104Y400 -> INT_X111Y407, INT_X104Y360 -> INT_X111Y367
East:  INT_X96Y416  -> INT_X111Y431, INT_X96Y400  -> INT_X111Y415
West:  INT_X88Y528  -> INT_X95Y535,  INT_X88Y520  -> INT_X95Y527
```

The reported max directional congestion was high:

- North 8x8 max congestion: `90.1762%`
- South 8x8 max congestion: `85.9408%`
- East 16x16 max congestion: `90.5697%`
- West 8x8 max congestion: `85.9525%`

## Net Names Involved

The first overlapping nets and verification failures are mostly in target
logic, not in the DDR reset timing paths that dominate the post-opt timing
report.

Representative target-side names:

- `chiptop0/system/rerocc_prci_domain_10/rerocc_tile/applyOrElse/gemmini/unrolled_cmd_q/...`
- `chiptop0/system/rerocc_prci_domain_9/rerocc_tile/applyOrElse/gemmini/mod/ld_input_/...`
- `chiptop0/system/rerocc_prci_domain_8/rerocc_tile/applyOrElse/gemmini/ex_controller/...`
- `chiptop0/system/rerocc_prci_domain_6/rerocc_tile/applyOrElse/dma/buffer/nodeOut_a_q/...`
- `chiptop0/system/rerocc_prci_domain_3/rerocc_tile/rerocc_manager/rr_req_q/...`
- `chiptop0/system/sbus/globalNoCDomain/noc/noc/router_sink_domain_30/routers/input_unit_3_from_31/input_buffer/qs_3/Q[145]`

This does not prove every overlap is caused by Gemmini or ReRoCC, but it does
show that the final route failure is in the target/NoC/Gemmini/DMA placement
region rather than being explained by the shell DDR reset timing paths alone.

## Implications For Active Builds

1. The shell-side post-opt timing violations are real, but they are not the
   same signal as the route failure. The old passing gdbserver AGFI also had
   poor timing, so timing alone should not be used as the pass/fail predictor.
2. The 12-pair dummy8x8/sbus64 build should reduce some datapath pressure, but
   it still keeps 12 repeated `rerocc_prci_domain_*` pair-manager regions and
   the NoC routing structure. It is a useful experiment, but it can still fail
   with the same class of route congestion.
3. The 8-pair dummy16x16/sbus128 build removes four pair-manager regions. If
   the route failure is mainly replication/congestion in target-side ReRoCC,
   Gemmini, DMA, and NoC structures, the 8-pair build is the more promising
   test for whether reducing pair count is enough.
4. If 12-pair dummy8x8/sbus64 fails with similar `rerocc_prci_domain_*` and NoC
   overlap names, the next hardware reduction should focus on pair count,
   queue/NoC pressure, or bridge structure. Further sbus width changes alone
   are unlikely to be decisive.

## Current Monitoring Hook

When the active 12-pair dummy8x8/sbus64 build reaches route, compare the new
Vivado log against these baseline markers:

- `Place 46-14` highly congested warning
- `Route 35-445` local routing congestion with high CLB pin utilization
- `Route 35-448` and `Route 35-581` congestion levels
- `Route 35-162` failed-to-route signals
- `Route 35-2` node overlaps
- `Constraints 18-1000` partially-conflicted nets and whether names still point
  at `rerocc_prci_domain_*`, `applyOrElse/gemmini`, `applyOrElse/dma`,
  `rerocc_manager/rr_req_q`, and NoC router buffers
