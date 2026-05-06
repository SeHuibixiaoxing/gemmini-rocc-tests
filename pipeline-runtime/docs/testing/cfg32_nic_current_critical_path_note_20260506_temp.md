# CFG32 NIC Current Critical Path Note - 2026-05-06

This temporary note records the static timing-path evidence collected while the
two active TIMING builds are still running.

## Active Builds

| Build | Config | Host | State at last check |
|---|---|---|---|
| 12p dummy8x8/sbus64 | `FireSimGemminiReRoCCPairDummy8x8C4P12Sbus64NICNoTraceConfig` + `BaseF2Config` | `192.168.1.77` | `route_design` completed, post-route event/DFX DRC active, no `to_aws`/AGFI yet |
| 8p dummy16x16/sbus128 | `FireSimGemminiReRoCCPairDummy16x16C4P8Sbus128NICNoTraceConfig` + `BaseF2Config` | `192.168.1.129` | route started, early router initialization active, no `to_aws`/AGFI yet |

Both use the FireSim/AWS TIMING strategy arguments visible in the Vivado
process:

```text
SSI_SpreadLogic_high AggressiveExplore AggressiveExplore A1 B0 C0 H2
```

## Top Path Pattern

The first post-phys-opt timing paths for both current candidates are still
dominated by the F2 shell/PCIS SLR2 register slice, not by Gemmini, the target
NoC, or the NIC datapath.

12p dummy8x8/sbus64 post-phys-opt report:

```text
Slack (VIOLATED) :        -3.242ns
Destination:            WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/skid_buffer_reg[22]/D
Path Group:             WRAPPER/CL/clk_main_a0
Data Path Delay:        3.062ns  (logic 0.077ns (2.515%)  route 2.985ns (97.485%))
Logic Levels:           0
SLR Crossing[1->2]
```

The next visible worst paths are the same shape:

- `AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/skid_buffer_reg[80]/D`
- `AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/m_payload_i_reg[22]/D`
- `AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/skid_buffer_reg[81]/D`
- `AXI4_REG_SLC_PCIS_SLR2/inst/r.r_pipe/m_payload_i_reg[382]/CE`

Their route-delay share is about `95%` to `97%`, with `0` or `1` logic level.

8p dummy16x16/sbus128 post-phys-opt report:

```text
Slack (VIOLATED) :        -3.242ns
Destination:            WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/skid_buffer_reg[22]/D
Path Group:             WRAPPER/CL/clk_main_a0
Data Path Delay:        3.062ns  (logic 0.077ns (2.515%)  route 2.985ns (97.485%))
Logic Levels:           0
SLR Crossing[1->2]
```

The next visible worst paths again stay in `AXI4_REG_SLC_PCIS_SLR2` `ar_pipe`
payload/skid-buffer registers, with route-delay share about `93%` to `97%`.

## Interpretation

- Shrinking the Gemmini dummy arrays and/or the number of pairs materially
  reduces LUT/FF pressure, but it does not move the top setup critical path
  away from the AWS/F2 PCIS SLR boundary.
- This explains why the 8p build is more likely to route than the original
  12p dummy16x16/sbus128 build, while still showing roughly the same first
  timing-path family.
- Current evidence does not point at the optimized DMA datapath, Gemmini dummy
  array shape, SimpleNIC datapath, or target-side NoC as the first timing
  limiter.
- If either build produces an AGFI, the relevant question is functional
  validation under gdbserver/pipeline-runtime, not whether the top setup report
  looks clean. The known-good gdbserver bitstream also had timing issues.
- If future timing cleanup is needed, the local target should be PCIS/Shell
  boundary placement/constraints or PCIS handoff width/fanout, not another blind
  reduction in Gemmini array dimensions.

## Consequence for Current Builds

- Keep the 12p dummy8x8/sbus64 build alive until the AWS flow either emits
  `to_aws`/AGFI collateral or exits after post-route checks. It has already
  proved that the reduced 12p design can legally route.
- Keep the 8p dummy16x16/sbus128 build alive. It has lower resource pressure
  and no placement-capacity failure so far, but it has not yet reached global
  route iterations.
- Do not make another RTL/config change before these two route/AFI outcomes are
  known. The current critical-path evidence is structural and should not be
  overfit before live bitstream evidence exists.

## 22:55 UTC Update

The two active builds now share the same main caveat: the router has emitted
`Route 35-514` and disabled hold fixing.

12p dummy8x8/sbus64:

- route completed successfully after `Route 35-514`
- route timing remained violated (`WNS` around `-3.339ns`, `WHS` around
  `-3.672ns` before post-route phys-opt)
- post-route phys-opt is still running
- no post-route phys-opt checkpoint/report, `Developer_CL.tar`, `to_aws`, AGFI,
  or AFI exists yet

8p dummy16x16/sbus128:

- route is still in `Phase 5.1 Global Iteration 0`
- `Route 35-514` has appeared
- overlap count is converging strongly:
  `403829 -> 30645 -> 3297 -> 595 -> 208 -> 95 -> 50 -> 25 -> 14`
- no route-finalize verdict, `to_aws`, AGFI, or AFI exists yet

Implication:

- Reducing pair count and/or sbus width helped routability/resource pressure,
  but it did not remove the shell/PCIS hold-pressure class.
- A generated AFI from either build should be considered usable only as a live
  validation candidate, not as a timing-clean result.
- The next decision point remains empirical: whether either flow reaches AWS
  packaging and then passes gdbserver/pipeline-runtime tests.

## 23:17 UTC Update

The 8p dummy16x16/sbus128 candidate has made material route progress:

- overlap converged to zero after `Route 35-514`
- route reached `Phase 7 Route finalize`
- routed-net verification completed successfully
- the current visible stage is `Phase 13 Post Router Timing / Phase 13.1 Update
  Timing`
- no route-completed line, DCP/tarball, `to_aws`, AFI, or AGFI exists yet

The 12p dummy8x8/sbus64 candidate remains in post-route phys-opt `Phase 2
Critical Path Optimization` with no post-route phys-opt checkpoint/report or AWS
packaging collateral yet.

Implication:

- The 8p build is now the more promising cfg32 candidate for reaching a routed
  DCP, despite still being low-trust due to `Route 35-514`.
- The 12p build should still be kept alive, but it is spending substantial time
  in post-route phys-opt without reaching packaging.
