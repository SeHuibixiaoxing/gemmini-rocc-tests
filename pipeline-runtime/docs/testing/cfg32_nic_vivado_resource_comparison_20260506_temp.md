# 20260506 cfg32 NIC Vivado resource comparison

## Scope

This note compares the current failing 12p+NIC Vivado builds with the older
12p+NIC build that produced a usable AGFI.

The older reference is:

- Build log:
  `sims/firesim/deploy/logs/2026-04-23--15-08-12-buildbitstream-D77Q9RMTJJ0UPRX8.log`
- AFI / AGFI: `afi-03ae6bee93f249537` / `agfi-02e18c6f7a7a95096`
- Runtime HWDB alias later used it as
  `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic`
- Build target:
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
- Platform: `FRFCFS16GBQuadRank_BaseF2Config`
- Frequency / strategy: `20 MHz`, `TIMING`

Important limitation: the full local result directories for the historical
12p+NIC builds were deleted during the 2026-05-05 disk-pressure cleanup:

- `sims/firesim/deploy/results-build/2026-04-21--09-12-59-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic`
- `sims/firesim/deploy/results-build/2026-04-23--15-08-12-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic`

The 4/23 DCP tar still exists in S3:

```text
s3://firesim-930030326150-us-west-2/dcp/2026_04_23-161238.Developer_CL.tar-192.168.0.81-BBF9AQJXQX.tar
```

It was downloaded to `/tmp/firesim-old-dcp/2026_04_23-161238.Developer_CL.tar`
and opened with Vivado 2024.2, matching the manifest `tool_version=v2024.2`.
The regenerated reports are local scratch artifacts under
`/tmp/firesim-old-dcp/reports/`:

- `old_20260423_post_route_utilization_hier.rpt`
- `old_20260423_post_route_utilization_flat.rpt`
- `old_20260423_route_status.rpt`
- `old_20260423_timing_summary.rpt`
- `old_20260423_congestion.rpt`

These are not committed because they are generated scratch reports, but the
numbers below come from them rather than only from the manager log.

## Current build outcomes

| Case | Build result | Outcome |
|---|---|---|
| Historical 12p+NIC | `2026-04-23--15-08-12-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic` | `route_design completed successfully`; AFI creation allowed from a `post_route.VIOLATED.dcp` |
| Current mainline cfg32 NIC | `2026-05-05--13-29-57-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic` | placement failed with `Place 30-487` |
| Current cfg32 NIC noTrace | `2026-05-05--17-14-55-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace` | placement passed, route failed with `Route 35-2` / `5975 node overlaps` |

The mainline placement failure is explicit:

```text
There are a total of 142215 CLBs in the pblock, of which 26405 CLBs are
available, however, the unplaced instances require 29960 CLBs.
```

The noTrace route failure is explicit:

```text
5774 signals failed to route due to routing congestion.
ERROR: [Route 35-2] Design is not legally routed. There are 5975 node overlaps.
```

The first partially-conflicted nets named by Vivado are in target fabric, not in
TraceIO:

- `rerocc_manager/rr_req_q`
- `GemminiCoupledDMA` `buffer/nodeOut_a_q`
- `globalNoCDomain/noc/.../routers/input_buffer`
- Gemmini command / reservation station logic

## Top-level post-synth resources for current builds

| Case | Total LUTs | Logic LUTs | LUTRAMs | FFs | RAMB36 | RAMB18 | URAM | DSP |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2026-05-01 cfg32 NIC, placement fail | 1,279,748 | 1,060,890 | 218,132 | 658,503 | 44 | 68 | 0 | 2,079 |
| 2026-05-05 cfg32 NIC, placement fail | 1,264,037 | 1,047,093 | 216,218 | 624,396 | 44 | 68 | 0 | 2,079 |
| 2026-05-05 cfg32 NIC noTrace, route fail | 1,169,035 | 1,026,303 | 142,006 | 618,967 | 44 | 68 | 8 | 2,079 |
| 2026-04-30 single-core 1BP known good | 170,452 | 105,826 | 63,732 | 113,702 | 179 | 88 | 0 | 19 |
| 2026-05-05 single-core 1BP recovered good | 131,812 | 97,472 | 33,674 | 77,063 | 179 | 88 | 8 | 19 |

The single-core 1BP path is not the resource problem. The recovered current
single-core 1BP bitstream is smaller than the old known-good single-core 1BP
bitstream and passed the gdbserver smoke. The issue is specific to the large
12p+NIC target.

## Bridge/Trace pressure vs target-fabric pressure

noTrace removes a large amount of FireSim bridge queue pressure:

| Bucket | 5/5 cfg32 NIC | 5/5 noTrace | Delta |
|---|---:|---:|---:|
| Total LUTs | 1,264,037 | 1,169,035 | -95,002 |
| Logic LUTs | 1,047,093 | 1,026,303 | -20,790 |
| LUTRAMs | 216,218 | 142,006 | -74,212 |
| FFs | 624,396 | 618,967 | -5,429 |
| URAM | 0 | 8 | +8 |

The reduction is mainly stream queue related:

| Bucket | 5/5 cfg32 NIC total/LUTRAM | 5/5 noTrace total/LUTRAM | Notes |
|---|---:|---:|---|
| `CPUManagedStreamEngine` rows | 125,694 / 100,032 | 32,277 / 25,824 | noTrace moves pressure down and uses 8 URAM |
| SimpleNIC stream queues | 67,026 / 53,952 | 32,198 / 25,824 | one large stream direction remains |
| TracerV stream queues | 57,960 / 46,080 | 0 / 0 | removed by noTrace |

This explains why noTrace gets past placement. It does not fix routing, because
the dense target fabric is essentially unchanged:

| Target bucket | 5/5 cfg32 NIC | 5/5 noTrace | Delta |
|---|---:|---:|---:|
| 12 Gemmini wrapper rows | 442,061 LUT / 245,391 FF | 442,061 LUT / 245,391 FF | 0 |
| 12 Gemmini parent rows | 382,771 LUT / 221,127 FF | 382,771 LUT / 221,127 FF | 0 |
| 12 `GemminiCoupledDMA` parent rows | 45,346 LUT / 22,932 FF | 45,346 LUT / 22,932 FF | 0 |
| 12 `ReRoCCManager` parent rows | 7,212 LUT / 2,904 FF | 7,212 LUT / 2,904 FF | 0 |
| `globalNoCDomain` top row | 338,625 LUT / 185,441 FF | 338,625 LUT / 185,441 FF | 0 |
| 40 router parent rows | 329,463 LUT / 175,784 FF | 329,463 LUT / 175,784 FF | 0 |

So the current noTrace failure is not TraceIO residue. The router/DMA/Gemmini
fabric still has to route through the same congested regions.

## Historical build vs current build

The regenerated historical DCP report changes the earlier log-only read. The old
4/23 design was not the same size as the current 5/5 design. It was materially
smaller in the target fabric.

| Bucket | Historical 4/23 routed DCP | Current 5/5 cfg32 NIC post-synth | Current 5/5 noTrace post-synth |
|---|---:|---:|---:|
| Top / `cl_firesim` total LUT | 1,016,985 CL row / 1,019,917 top flat | 1,264,037 | 1,169,035 |
| Top / `cl_firesim` LUTRAM | 142,926 | 216,218 | 142,006 |
| Top / `cl_firesim` FF | 621,924 CL row / 632,205 top flat | 624,396 | 618,967 |
| Top / `cl_firesim` DSP | 2,031 | 2,079 | 2,079 |
| `FireSim_` total LUT | 933,474 | 1,073,557 | 1,073,483 |
| `globalNoCDomain` total LUT | 280,774 | 338,625 | 338,625 |

The current noTrace build has almost the same top LUTRAM count as the historical
routed DCP, but its total LUT count is still about `+152k` higher at the top
level and about `+140k` higher in `FireSim_`. The growth is not TraceIO residue.
It is in target fabric.

The clearest target growth buckets are:

| Target bucket | Historical 4/23 routed DCP | Current 5/5 noTrace post-synth | Delta |
|---|---:|---:|---:|
| `globalNoCDomain` | 280,774 LUT / 185,519 FF | 338,625 LUT / 185,441 FF | +57,851 LUT / -78 FF |
| one `GemminiCoupledDMAPairWrapper` row | about 32.6k LUT / 20.9k FF | about 36.8k LUT / 20.4k FF | about +4.2k LUT each |
| one `GemminiCoupledDMA` row | about 3.38k LUT / 1.91k FF | about 3.78k LUT / 1.91k FF | about +0.4k LUT each |
| one `ReRoCCManager` row | about 544 LUT / 242 FF | 601 LUT / 242 FF | about +57 LUT each |

Across 12 Gemmini/ReRoCC tiles, the wrapper/DMA/ReRoCC growth explains roughly
`+55k` LUT. `globalNoCDomain` explains another `+58k` LUT. Together these account
for most of the `FireSim_` growth over the historical routed DCP.

Therefore the current problem is not only routing-strategy drift. Strategy drift
is real, but the current target is also larger and denser than the historical
12p+NIC image that routed.

## Strategy drift is a concrete difference

Both the historical build and current builds were launched with `--strategy
TIMING`, but the actual Vivado directives are different.

Historical 4/23 build:

```text
place_design -directive SSI_SpreadLogic_high -no_bufg_opt
phys_opt_design -directive AggressiveExplore
route_design -directive AggressiveExplore -tns_cleanup -timing_summary
```

Current 5/5 builds:

```text
place_design -directive ExtraNetDelay_high -no_bufg_opt
phys_opt_design -directive AggressiveExplore
route_design -tns_cleanup -directive Explore -timing_summary
```

This is a major clue. The older passing/routed build spread logic more
aggressively and used `AggressiveExplore` for routing. The current `TIMING`
strategy no longer does that in the local F2 shell collateral.

One subtlety in the 5/5 logs matters: the current build prints the historical
defaults once near CL setup:

```text
place_direct     : SSI_SpreadLogic_high
phy_opt_direct   : AggressiveExplore
route_direct     : AggressiveExplore
```

However, after `strategy_TIMING.tcl` is sourced, the same log prints the actual
resolved project settings:

```text
build_strategy   : TIMING
strategy_file    : /home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/common/shell_stable/build/scripts/strategy_TIMING.tcl
place_direct     : ExtraNetDelay_high
phy_opt_direct   : AggressiveExplore
route_direct     : Explore
```

The executed Vivado commands then match the resolved values, not the earlier
default display. Therefore the meaningful comparison is old
`SSI_SpreadLogic_high/AggressiveExplore` versus current
`ExtraNetDelay_high/Explore`.

The current local strategy file is:

```text
sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/common/shell_stable/build/scripts/strategy_TIMING.tcl
```

It sets:

```tcl
set place_directive  "ExtraNetDelay_high"
set phys_directive   "AggressiveExplore"
set route_directive  "Explore"
```

This F2 shell directory is untracked from the FireSim git repository (`git -C
sims/firesim status --short -- platforms/f2/aws-fpga-firesim-f2` reports it as
`? platforms/f2/aws-fpga-firesim-f2`). Normal source diffs will not protect this
strategy from drifting unless the resolved directives are copied into tracked
project collateral or checked explicitly in build preflight logs.

The current noTrace route result is also visibly worse than the historical one:

| Metric | Historical 4/23 routed build | Current 5/5 noTrace route fail |
|---|---:|---:|
| North congested cluster level | 0 | 3 |
| South congested cluster level | 0 | 2 |
| East congested cluster level | 2 | 4 |
| West congested cluster level | 0 | 3 |
| Route status | 1,565,070 routable nets; 1,565,070 fully routed; 0 errors | `5774` signals failed, `5975` overlaps |

The regenerated old congestion report still shows real pressure. Its final
placer congestion table has two level-5 short windows, both around
`rerocc_prci_domain_4/.../gemmini/mod` and
`globalNoCDomain/noc/.../router_sink_domain_16/routers`. But the old route is
legal. The current noTrace build reaches route verification with thousands of
overlaps.

## Current diagnosis

There are three separate resource/congestion layers.

First, the mainline cfg32 NIC image is too close to the CL pblock packing limit.
The extra Trace/CPU stream queue LUTRAM makes detail placement fail with only
`26405` CLBs available for `29960` required unplaced CLBs. The detailed failure
also reports:

```text
Control sets: 15361
Luts: 1219387 (combined) 1258059 (total), available capacity: 1137720
```

Second, noTrace removes enough LUTRAM/queue pressure to pass placement, but it
does not change the dense target fabric. The route failure is in the 12
Gemmini+DMA+ReRoCC+GlobalNoC fabric. The listed overlaps and conflicted nets are
in those blocks, and the module-level utilization is unchanged between mainline
and noTrace for those blocks.

Third, the current target fabric is larger than the historical routed fabric.
The historical 4/23 build proves that the earlier 12p+NIC scale could route, but
the current noTrace image has roughly `+140k` more `FireSim_` LUTs, mainly from
`globalNoCDomain` and the Gemmini/DMA wrappers. The actual TIMING strategy also
regressed: old routed with `SSI_SpreadLogic_high` placement and
`AggressiveExplore` route; current uses `ExtraNetDelay_high` placement and
`Explore` route.

The current best diagnosis is a combination: target-fabric growth consumed much
of the placement/routing margin, and the changed TIMING directives then route a
denser design worse than the historical one.

## Recommended next steps

1. Create an explicit tracked F2 build strategy for this target, for example
   `TIMING_LEGACY_SPREAD_AGGR`, that reproduces the historical directives:
   `SSI_SpreadLogic_high` for place, `AggressiveExplore` for route, and the same
   pre-route phys-opt behavior.
2. Add a build preflight/checkpoint that prints the resolved place/phys/route
   directives before synthesis so a nominal `TIMING` build cannot silently drift.
3. Rebuild the noTrace 12p+NIC target first with the legacy aggressive strategy.
   It already passes placement, so this is the cheapest exact-scale experiment
   for testing how much of the failure is strategy-induced.
4. In parallel with that build, statically diff the source/config changes that
   account for `globalNoCDomain` growing by about `+58k` LUT and each Gemmini DMA
   wrapper growing by about `+4.2k` LUT relative to the 4/23 routed DCP. That
   growth is large enough to matter even if the legacy strategy improves route.
5. If the noTrace legacy-strategy build still fails route, reduce target-fabric
   congestion instead of only removing TraceIO: smaller P count, shallower router
   buffers, fewer GlobalNoC queues, or a debug-scale cfg32 NIC image such as
   C4P8/C4P6 while keeping the software-visible cfg32/NIC/DMA path.
6. Preserve the regenerated DCP-report method above as the comparison SOP. The
   original local build result directory is gone, but the S3 routed DCP is enough
   to regenerate utilization, route status, timing summary, and congestion
   reports when exact historical numbers are needed.
