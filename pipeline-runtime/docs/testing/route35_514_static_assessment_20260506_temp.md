# Route 35-514 Static Assessment - 2026-05-06

This temporary note records the current interpretation of `Route 35-514` while
the cfg32 NIC pipeline-runtime bitstream builds are still running.

## Question

The active cfg32 NIC builds now show:

```text
WARNING: [Route 35-514] Design has a large number of hold violators. This is likely a design or constraint issue. Router is turning off hold fixing.
```

The immediate question is whether this warning means the bitstream should be
rejected before live testing.

## Old Passing AGFI Evidence

The old known-good single-core 1BP NIC bitstream was:

- AGFI: `agfi-0079cbbca617eca4e`
- AFI: `afi-0ee7774f829acd4de`
- Build result:
  `sims/firesim/deploy/results-build/2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz/`
- Config: `FireSimRocketNICNoTraceConfig + BaseF2Config`

Its Vivado log also contained `Route 35-514`:

```text
WARNING: [Route 35-514] Design has a large number of hold violators. This is likely a design or constraint issue. Router is turning off hold fixing.
```

The same log then continued to route completion:

```text
INFO: [Route 35-16] Router Completed Successfully
route_design completed successfully
```

Its post-route/post-route-physopt timing remained violated:

```text
INFO: [Physopt 32-669] Post Physical Optimization Timing Summary | WNS=-3.199 | TNS=-5148.379 | WHS=-3.942 | THS=-4282.343 |
CRITICAL WARNING: [Route 35-39] The design did not meet timing requirements.
```

The top post-route timing report was dominated by async DDR-ready/reset style
paths rather than gdbserver software behavior:

```text
Slack (VIOLATED) : -3.199ns
Destination: WRAPPER/CL/ddr_ready_pre_sync_meta_reg/CLR
Path Group: **async_default**
Path Type: Recovery
```

This old bitstream still passed the remote gdbserver smoke matrix, including
target remote, multiple software breakpoints, continue, next, info threads,
thread apply all bt, register read, disassembly, variable and memory read/write,
thread switching, Ctrl-C regain, and detach.

## Current Active Builds

12p dummy8x8/sbus64/cfg32/NIC/noTrace:

- Config:
  `FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig + BaseF2Config`
- Strategy: ordinary `TIMING`
- Route has completed after `Route 35-514`.
- Post-route phys-opt is still running.
- No `Developer_CL.tar`, `to_aws`, AFI, or AGFI exists yet.

8p dummy16x16/sbus128/cfg32/NIC/noTrace:

- Config:
  `FireSimGemminiReRoCCPairDummy16x16C4P8Sbus128NICNoTraceConfig + BaseF2Config`
- Strategy: ordinary `TIMING`
- `Route 35-514` has appeared.
- Route is still active in `Phase 5.1 Global Iteration 0`.
- Overlap count is converging:
  `403829 -> 30645 -> 3297 -> 595 -> 208 -> 95 -> 50 -> 25 -> 14`.
- No route-finalize verdict, AFI, or AGFI exists yet.

## Strategy Difference

The F2 shell build scripts contain two relevant strategies:

- `strategy_TIMING.tcl`
  - includes `set_param route.enableGlobalHoldIter 1`
  - does not include `set_param route.enableHoldExpnBailout 0`
- `strategy_TIMING_HOLDFIX.tcl`
  - includes both `set_param route.enableGlobalHoldIter 1`
  - and `set_param route.enableHoldExpnBailout 0`

So ordinary `TIMING` allows Vivado to bail out of hold expansion when it decides
there are too many hold violators. `TIMING_HOLDFIX` keeps the router trying
hold fixing longer. The tradeoff is runtime and Vivado crash risk; previous
records show `TIMING_HOLDFIX` can avoid early `Route 35-514`, but it may run
much longer and has also hit post-route tool instability in some experiments.

## PCIS/Floorplan Inheritance Check

The active cfg32 builds do include the current PCIS2SLR staging/floorplan
collateral. The remote `small_shell_cl_pnr_user.xdc` files for both active
builds map:

```text
WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2 -> pblock_CL_SLR2
WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR1 -> pblock_CL_SLR1
```

The local F2 RTL also contains both shell-boundary register-slice instances:

```text
AXI4_REG_SLC_PCIS_SLR2
AXI4_REG_SLC_PCIS_SLR1
```

Therefore the current `Route 35-514` is not explained by accidentally omitting
the PCIS2SLR slice experiment. The more accurate interpretation is that PCIS2SLR
staging helps setup/routing pressure but does not eliminate the broader
PCIS/RL_SHIM/Shell hold-pressure class under ordinary `TIMING`.

The generated XDC still contains an inherited reference to
`WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_CROSSBAR`, while the FireSim F2 design comments
state that this module does not exist in FireSim's DMA architecture. The
grandchild `pblock_CL_SLR1_XBAR` block is commented out, which avoids one empty
pblock mapping, but the SLR1 module-mapping list still mentions the nonexistent
crossbar. This produces warnings in related logs, but it is not new and was
present in earlier timing experiments. It should be cleaned eventually for
signal hygiene, but the current top timing and tight-pin evidence points at the
real PCIS/RL_SHIM register-slice paths, not this stale XDC name by itself.

## Tight-Pin Comparison

The active builds' `tight_setup_hold_pins.txt` files are close to the old
passing AGFI in both size and dominant prefix.

| Build | `tight_setup_hold_pins.txt` lines | `RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE` count |
|---|---:|---:|
| Old passing single-core 1BP NIC AGFI | 2002 | 1801 |
| 12p dummy8x8/sbus64/cfg32/NIC | 1953 | 1837 |
| 8p dummy16x16/sbus128/cfg32/NIC | 1955 | 1837 |

The top pins in both active builds are still `r_pipe/m_payload_i_reg[...]`
endpoints under:

```text
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE
```

This reinforces the earlier conclusion from `20260505T085605Z`: the
PCIS/RL_SHIM tight-pin class is a real timing-pressure indicator, but not a
standalone explanation for functional gdbserver failure. A passing AGFI already
had the same class and nearly the same count.

## Interpretation

`Route 35-514` is a real risk marker, not a harmless warning. It means the route
is not a timing-clean implementation and any resulting AFI should be considered
low-trust.

It is not, by itself, a sufficient reason to discard a bitstream before live
testing. The old passing AGFI had the same class of evidence and still passed
the gdbserver matrix.

The decision rule for the current builds is:

1. Keep ordinary `TIMING` builds alive if they continue toward route/package
   completion.
2. If an AFI/AGFI is produced, run the gdbserver/pipeline-runtime validation
   rather than rejecting solely on `Route 35-514`.
3. If a build fails before packaging, use its route and timing artifacts to
   decide whether the next build should switch to `TIMING_HOLDFIX`, reduce
   PCIS/RL_SHIM hold pressure, or reduce target resource pressure.
4. Do not assume `TIMING_HOLDFIX` is automatically better for the current cfg32
   target. It is a plausible fallback, but the old known-good AGFI proves that
   ordinary `TIMING` plus violated route can still be functionally useful.

## Current Blocker

The blocker is not static interpretation of `Route 35-514`. The blocker is
whether either active cfg32 NIC build reaches AWS packaging and produces an
AGFI that can be validated under gdbserver.

## 8p C4P8 Failure Update

The 8p dummy16x16/sbus128/cfg32/NIC build later failed after route overlap
converged and after routed-net verification. The final blocker was not
`Route 35-514` by itself. It was DFX boundary legality:

```text
ERROR: [Constraints 18-4430] ... boundary net WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/... does not contain PartPin LOC.
INFO: [Route 35-17] Router encountered errors.
route_design failed
```

This refines the assessment:

- `Route 35-514` remains a low-trust warning, but the 8p build's hard failure
  was `Constraints 18-4430`.
- The net families named by the DFX error match the same PCIS/RL_SHIM boundary
  region highlighted by tight-pin and critical-path evidence.
- A future retry should focus on DFX-legal placement/routing for
  `RL_SHIM/DMA_PCIS_AXI_REG_SLC` and `RL_SHIM/DDR_STAT_PIPE_DATA` boundary nets.
