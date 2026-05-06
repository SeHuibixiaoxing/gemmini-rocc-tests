# cfg32 NIC noTrace post-place timing note - 2026-05-06

This note records the first static timing read from the active
`12p4c128sbus32cfg + optimized DMA + current NIC` noTrace F2 build:

- session: `pairdummy-cfg32-nic-notrace-20260505T171453Z`
- target: `FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig`
- platform: `FRFCFS16GBQuadRank_BaseF2Config`
- strategy/frequency: `TIMING`, `20MHz`
- build host: `i-0ecf73dad2a8ae8d0` / `192.168.0.213`

## Current artifact state

At `2026-05-06T00:28:49Z`, the build had produced:

```text
post_place.dcp
post_place_timing.rpt
```

The Vivado log still showed an active `vivado` process. There was no AGFI/AFI
yet, and no cfg32 NIC HWDB update is valid at this stage.

## Worst post-place path

The worst reported path in:

```text
cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config.2026_05_05-191909.post_place_timing.rpt
```

is:

```text
Slack (VIOLATED) : -3.250ns
Destination: WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/m_payload_i_reg[80]/D
Path Group: WRAPPER/CL/clk_main_a0
Requirement: 4.000ns
Data Path Delay: 3.106ns (logic 0.113ns, route 2.993ns)
Logic Levels: 1 (LUT3=1)
```

Important properties:

- The source is hidden inside the static/shell side of the design.
- The destination is the CL DMA PCIS slave slice in `pblock_CL_SLR2`.
- The report shows an `SLR Crossing[1->2]`.
- About `96%` of the data delay is routing, not CL user logic.
- The path is not a deep Gemmini, ReRoCC, PairManager, or optimized DMA logic
  cone.

Several following worst paths are similar PCIS `ar`/`r` pipe payload or CE
paths, again route-dominated with one LUT level.

## Interpretation

This timing report should not be read as evidence that pipeline-runtime or the
optimized DMA datapath is functionally wrong. It is a shell/PCIS cross-SLR route
and skew problem in a very dense image.

This matches the user's earlier observation that AWS shell timing violations
have appeared even in older gdbserver-capable bitstreams. Therefore, if this
build eventually produces an AGFI despite timing violations, it is still worth a
live gdbserver validation run. The live run result, not this post-place timing
summary alone, determines whether the AGFI is useful for pipeline-runtime
debugging.

If the build fails before AGFI creation because of routing or timing closure,
the next decision should be:

- First collect the route/post-route failure context and exact failing path
  groups.
- If failures remain dominated by shell/PCIS route/skew, a strategy or physical
  implementation variant is more plausible than rewriting Gemmini/DMA logic.
- If failures move into CL user logic or capacity, reduce resources or
  observability instead of only changing strategy.

## Operational consequence

For the active build:

- Continue 1200s polling.
- Do not update HWDB before AGFI/AFI exists.
- Do not start runfarm yet.
- Keep the prepared noTrace driver bundle paired with the future noTrace AGFI
  if AFI creation succeeds.
