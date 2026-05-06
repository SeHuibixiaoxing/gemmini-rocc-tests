# 20260506 cfg32 NIC resource root cause note

## Purpose

This is a focused follow-up to
`cfg32_nic_vivado_resource_comparison_20260506_temp.md`.

Question being answered: the old 12-pair NIC bitstream routed and produced an
AGFI, but the current 12-pair `cfg32` NIC builds either fail placement or fail
route. Is the current blocker mostly TraceIO/NIC/gdbserver software, mostly
Vivado strategy drift, or mostly target-fabric growth?

Current answer: the single-core 1BP gdbserver path is recovered, so the blocker
for the requested large pipeline-runtime debug bitstream is not the common
software/NIC/gdbserver stack. The immediate blocker is implementation closure of
the large 12-pair target. The best static evidence points to target-fabric
growth, especially ReRoCC cfg widening through the GlobalNoC and the optimized
CoupledDMA direct-copy path, with Vivado TIMING strategy drift making an already
denser design route worse.

## Known baselines

Single-core 1BP gdbserver known-good historical baseline:

- AGFI: `agfi-0079cbbca617eca4e`
- AFI: `afi-0ee7774f829acd4de`
- Build result:
  `sims/firesim/deploy/results-build/2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz/`
- Config: `FireSimRocketNICNoTraceConfig + BaseF2Config`
- Type: single-core Rocket + NIC + no TraceIO + 30MHz, not the 8BP variant
- Capability covered: `target remote`, multiple software breakpoints,
  `continue`, `next`, `info threads`, `thread apply all bt`, register reads,
  disassembly, variable/memory read-write, thread switching, Ctrl-C control
  recovery, and `detach`
- Relevant records:
  `debug_records/20260501T045108Z.md` and
  `debug_records/20260502T123027Z.md`

Current recovered single-core 1BP baseline:

- AGFI: `agfi-03d9518415ec82449`
- AFI: `afi-0bf1f9a2bdacaab09`
- Result: gdbserver smoke passed on the current software/hardware stack
- Meaning: the common remote-gdbserver software path, NIC driver path, and
  one-breakpoint hardware path are healthy enough for the old smoke coverage.

Historical 12-pair NIC baseline:

- AGFI: `agfi-02e18c6f7a7a95096`
- AFI: `afi-03ae6bee93f249537`
- Build log:
  `sims/firesim/deploy/logs/2026-04-23--15-08-12-buildbitstream-D77Q9RMTJJ0UPRX8.log`
- DCP tar:
  `s3://firesim-930030326150-us-west-2/dcp/2026_04_23-161238.Developer_CL.tar-192.168.0.81-BBF9AQJXQX.tar`
- Regenerated local reports:
  `/tmp/firesim-old-dcp/reports/old_20260423_post_route_utilization_hier.rpt`,
  `/tmp/firesim-old-dcp/reports/old_20260423_post_route_utilization_flat.rpt`,
  `/tmp/firesim-old-dcp/reports/old_20260423_route_status.rpt`,
  `/tmp/firesim-old-dcp/reports/old_20260423_timing_summary.rpt`, and
  `/tmp/firesim-old-dcp/reports/old_20260423_congestion.rpt`
- Important limitation: the old local result directory was deleted, so the DCP
  reports and manager log are now the authoritative local evidence.

Current failed large builds:

- Mainline cfg32 NIC:
  `sims/firesim/deploy/results-build/2026-05-05--13-29-57-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic/`
  failed placement with `Place 30-487`.
- cfg32 NIC noTrace:
  `sims/firesim/deploy/results-build/2026-05-05--17-14-55-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace/`
  passed placement but failed route with `5774` unrouted signals and `5975`
  node overlaps.

## Resource delta that matters

Top target/resource buckets from the old routed DCP versus current noTrace:

| Bucket | Old 2026-04-23 routed DCP | Current 2026-05-05 noTrace post-synth | Delta |
|---|---:|---:|---:|
| Top flat LUT | 1,019,917 | 1,169,035 | about +149k |
| `FireSim_` LUT | 933,474 | 1,073,483 | about +140k |
| `globalNoCDomain` LUT | 280,774 | 338,625 | +57,851 |
| One `GemminiCoupledDMAPairWrapper` LUT | about 32.6k | about 36.8k | about +4.2k |
| One `GemminiCoupledDMA` LUT | about 3.38k | about 3.78k | about +0.4k |
| One `ReRoCCManager` LUT | about 544 | 601 | about +57 |

Main interpretation:

- Removing TraceIO/noTrace is necessary because the mainline cfg32 NIC image
  over-packs the shell pblock during placement.
- Removing TraceIO is not sufficient because the target fabric is unchanged:
  the 12 Gemmini wrappers, CoupledDMA instances, ReRoCC managers, and
  `globalNoCDomain` rows are effectively identical between current mainline NIC
  and current noTrace NIC.
- The current noTrace design has top-level LUTRAM similar to the old routed DCP,
  but still has roughly `+140k` target/FireSim LUTs. That is why placement can
  pass and route can still fail.

## Code path: cfg32 widens the GlobalNoC payload

The biggest suspicious structural change is that cfg32 is currently a default
ReRoCC client size, not just a local software CSR extension.

Relevant code:

- `generators/rerocc/src/main/scala/client/CSRs.scala`
  - `ReRoCCCSRs.MAX_CFGS = 32`
  - cfg CSR banks now cover `0x810..0x81f` and `0x820..0x82f`.
- `generators/rerocc/src/main/scala/client/Client.scala`
  - `ReRoCCClientParams(nCfgs: Int = ReRoCCCSRs.MAX_CFGS)`
  - each client now defaults to 32 cfgs.
- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`
  - the 12-pair config calls `new rerocc.WithReRoCC(...)` without an explicit
    `clientParams`, so it inherits the 32-cfg default.
- `generators/rerocc/src/main/scala/bus/Parameters.scala`
  - `clientIdBits = log2Ceil(cParams.clients.map(_.nCfgs).sum)`.
- `generators/rerocc/src/main/scala/bus/Protocol.scala`
  - `ReRoCCMsgBundle` carries `client_id`.
- `generators/rerocc/src/main/scala/bus/NoC.scala`
  - `minPayloadWidth = new ReRoCCMsgBundle(wideBundle).getWidth`.
  - that payload is placed in Constellation flits and replicated through router
    input buffers, switches, muxes, and route logic.

For 4 CPU clients:

- Old 16 cfg/client: `4 * 16 = 64` global client IDs, requiring 6 bits.
- Current 32 cfg/client: `4 * 32 = 128` global client IDs, requiring 7 bits.

That looks like "only one more bit", but this bit is in a globally-routed
message payload. It fans through the GlobalNoC fabric and every relevant router
queue/mux path. This matches the report shape:

- `globalNoCDomain` grows by about `+57.9k` LUT.
- Individual `rerocc_client`/CSR logic is not the dominant growth bucket.
- Router rows grow broadly rather than one isolated CSR block exploding.

Static conclusion: cfg32's current implementation is physically expensive
because the software-visible cfg count also increases the global transaction
namespace width. If the final requirement truly needs 32 software cfg slots, the
better long-term fix is likely a compressed/active transaction namespace rather
than directly widening every NoC payload path.

## Code path: optimized CoupledDMA direct copy increases local fabric

The other suspicious structural change is the optimized misaligned direct-copy
implementation in nested gemmini commit `085864a Optimize CoupledDMA misaligned
direct copies`.

Old shape:

- A simpler direct-copy state machine with byte-wide fallback for misaligned
  cases.
- Fewer read-window/cached-read registers and less partial-mask logic.

Current shape in
`generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`:

- `sIssueGet0`, `sWaitGet0`, `sIssueGet1`, `sWaitGet1`, then put/flag states.
- `curReadDataLo`, high-side read data, cached read address/data, read-window
  construction, source/destination shift logic, and partial put masks.

This is functionally useful for misaligned direct-copy performance, but the
large 12-pair noTrace build has almost no routing margin. The report-level
shape is consistent with the DMA/wrapper path contributing meaningful growth:

- each `GemminiCoupledDMA` parent is roughly `+0.4k` LUT versus old routed DCP;
- each pair wrapper is roughly `+4.2k` LUT versus old routed DCP;
- multiplied by 12 pairs, this is large enough to materially affect placement
  and route pressure.

Static conclusion: the optimized DMA is not the only issue, but it is a good
candidate to disable for the first route-recovery build because it restores
margin without changing the gdbserver/NIC control path.

## Vivado strategy drift is real, but not the whole problem

Historical 4/23 build actually used:

```text
place_design -directive SSI_SpreadLogic_high -no_bufg_opt
phys_opt_design -directive AggressiveExplore
route_design -directive AggressiveExplore -tns_cleanup -timing_summary
```

Current 5/5 noTrace build command line initially shows the old TCL args:

```text
vivado ... -tclargs SSI_SpreadLogic_high AggressiveExplore AggressiveExplore A1 B0 C0 H2
```

But the sourced `strategy_TIMING.tcl` overrides them. The actual resolved and
executed commands are:

```text
place_design -directive ExtraNetDelay_high -no_bufg_opt
phys_opt_design -directive AggressiveExplore
route_design -tns_cleanup -directive Explore -timing_summary
```

Current route also reports:

```text
Design has 1948 pins with tight setup and hold constraints.
Design has a large number of hold violators. Router is turning off hold fixing.
5774 signals failed to route due to routing congestion.
Design is not legally routed. There are 5975 node overlaps.
```

The old design also violated timing, so negative timing slack alone is not the
stop condition. The important difference is that the old design became legally
routed and then AGFI creation could proceed. The current design reaches route
verification with thousands of overlaps.

Static conclusion: restoring the historical directives is required for a fair
experiment and may be enough for a marginal build. It should not be treated as a
guaranteed fix, because the current target is substantially larger than the old
routed target.

## Current blocker

Current blocker is not "the 1BP gdbserver software path is broken".

Current blocker is:

1. The mainline 12p cfg32 NIC build over-packs placement due to combined
   target-fabric and stream/Trace queue pressure.
2. The noTrace 12p cfg32 NIC build removes enough queue pressure to place but
   still fails route in the dense target fabric.
3. The target-fabric growth relative to the routed 4/23 12p NIC DCP is large
   enough that simply changing shell route strategy may fail; the likely
   structural causes are cfg32 NoC payload widening and optimized DMA fabric
   growth.

## Next build experiments

The goal is to minimize bitstream-build count while still separating structural
resource growth from strategy drift.

Recommended first exact-scale candidate:

- 12p + 4c + sbus128 + cfg32 + NIC + noTrace.
- Restore historical TIMING directives:
  `SSI_SpreadLogic_high` place, `AggressiveExplore` phys-opt, and
  `AggressiveExplore` route.
- Disable or bypass optimized CoupledDMA misaligned direct-copy hardware for
  this large bitstream candidate, ideally by a parameter/config switch rather
  than a broad revert.
- Keep NIC and the software-visible cfg32 path enabled.

Reason: this preserves the user's desired large debug target while removing two
known implementation-risk deltas: strategy drift and DMA local fabric growth.

Recommended parallel diagnostics if AWS capacity allows:

- Strategy-only diagnostic: cfg32 + NIC + noTrace + optimized DMA + restored
  legacy directives. This isolates Vivado strategy drift.
- cfg16 diagnostic: cfg16 + NIC + noTrace + optimized DMA + restored legacy
  directives. This isolates the GlobalNoC cost of cfg32/client-id widening.

Interpretation plan:

- If exact-scale cfg32 noTrace with old DMA and legacy directives routes, move
  to AGFI/gdbserver/pipeline-runtime bring-up.
- If strategy-only routes but old-DMA candidate is unnecessary, keep optimized
  DMA and document strategy as the main fix.
- If cfg16 routes but cfg32 does not, prioritize ReRoCC cfg namespace encoding.
- If all exact-scale cfg32 builds fail route, reduce the debug target scale
  temporarily, for example C4P8 or C4P6, while designing a lower-cost cfg32
  encoding for the final large target.

## Required preflight for the next build

Before launching another expensive build:

- Confirm no stale F2 run-farm instance is running.
- Confirm the build recipe name, target config, platform config, and frequency.
- Confirm the actual Vivado strategy file that will be sourced.
- Print or capture resolved `place_direct`, `phy_opt_direct`, and
  `route_direct` in the build log before implementation.
- Record whether optimized CoupledDMA direct-copy logic is enabled.
- Commit this static-analysis milestone before changing build inputs.

## Status at this checkpoint

This document is static analysis only. It does not introduce a new AGFI/AFI and
does not claim that the next exact-scale bitstream will route. It narrows the
current blocker to implementation closure of the 12p cfg32 NIC target and gives
a concrete, low-rebuild-count experiment sequence.
