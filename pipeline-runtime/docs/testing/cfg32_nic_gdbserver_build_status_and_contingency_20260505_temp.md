# cfg32 NIC gdbserver build status and contingency - 2026-05-05

This temporary note tracks the active `12p4c128sbus32cfg + optimized DMA +
current NIC` bitstream path for pipeline-runtime remote gdbserver debugging.

## Current state

The common remote gdbserver software path is not the current blocker. The
single-core 1BP NIC smoke has already passed on the recovered current AGFI, and
the cfg32 workload image still passes `debug-preflight` and `local-freshness`.

The current blocker is obtaining a fresh cfg32 NIC AGFI that matches the current
hardware source closely enough to debug pipeline-runtime hangs.

## Historical 12p NIC AGFIs

There have been successful 12-pair NIC bitstream builds before this round:

- `agfi-0b9490626efd2b861` / `afi-06a9540775fb04a5e`, created
  `2026-04-21T22:35:28Z`, available `2026-04-22T00:13:15Z`.
- `agfi-02e18c6f7a7a95096` / `afi-03ae6bee93f249537`, created
  `2026-04-24T07:27:40Z`, available `2026-04-24T09:08:12Z`.

Both images are named
`firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic` in AWS and
use:

```text
WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128
```

The local `cfg32_nic` HWDB later aliased `agfi-02e18c6f7a7a95096`, but this is
not evidence that the current post-NIC-debug/noTrace source tree can still close
the exact same design. On `2026-04-27`, the `agfi-02e18c6f7a7a95096` local-GDB
attempt failed before Linux with the old SimpleNIC host-driver
`ERR MISMATCH! on writing tokens in...` path; that run was blocked by stale or
incompatible driver collateral, not by pipeline-runtime user code.

Therefore:

- old 12p+NIC F2 implementation closure is proven;
- current 12p+cfg32+NIC implementation closure is not proven;
- no historical 12p+NIC run has demonstrated the single-core old-AGFI remote
  gdbserver capability set.

## Builds

Mainline cfg32 NIC:

- Session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- Target: `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
- Platform: `FRFCFS16GBQuadRank_BaseF2Config`
- Strategy/frequency: `TIMING`, `20MHz`
- Result: failed, no AGFI/AFI.
- Failure: Vivado `Phase 3 Detail Placement`, `Place 30-487` / `Place 30-99`.
  The CL pblock had `26405` available CLBs, while unplaced instances required
  `29960` CLBs.
- Post-synth `cl_firesim` utilization: total LUT `96.96%`, logic LUT
  `80.32%`, LUTRAM `35.98%`.

noTrace cfg32 NIC fallback:

- Session: `pairdummy-cfg32-nic-notrace-20260505T171453Z`
- Target: `FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig`
- Platform: `FRFCFS16GBQuadRank_BaseF2Config`
- Strategy/frequency: `TIMING`, `20MHz`
- Current result: failed, no AGFI/AFI.
- Current observed phase: Vivado `place_design completed successfully` as of
  `2026-05-06 00:19 UTC`; pre-route `phys_opt_design -directive
  AggressiveExplore` reached post-phy_opt checkpoint/report writing as of
  `2026-05-06 00:49 UTC`; `route_design -tns_cleanup -directive Explore
  -timing_summary` started as of `2026-05-06 00:53 UTC`; by the
  `2026-05-06 01:29 UTC` poll it had completed router initialization, global
  routing, and the initial net routing pass. It later failed route verification
  at `2026-05-06 09:34 UTC`.
- Failure: `Route 35-2`, design not legally routed, `5975 node overlaps`, plus
  `Constraints 18-1000` partially-conflicted nets. No post-route DCP, bitstream,
  AFI, or AGFI was produced.
- Post-synth `cl_firesim` utilization: total LUT `89.67%`, logic LUT
  `78.72%`, LUTRAM `23.63%`.

The noTrace build leaves `DigitalTop` essentially unchanged and removes
TraceIO-related FireSim top-level pressure. That is why it is the current best
candidate: the user-space pipeline-runtime gdbserver workflow does not need
TraceIO. It has now crossed the mainline build's exact detail-placement failure
boundary and completed placement, but it did not route. The final blocker is
now routing congestion/legalization in the dense noTrace cfg32 NIC image, not
HWDB, rootfs, gdbserver, or workload setup.
The first post-place timing read shows the worst paths are shell/static to
`CL_DMA_PCIS_SLV` SLR2 PCIS paths dominated by routing, not deep Gemmini/DMA
logic. See:
[`cfg32_nic_notrace_post_place_timing_20260506.md`](cfg32_nic_notrace_post_place_timing_20260506.md).

## Scale decision

For the immediate objective, which is getting a usable remote-gdbserver target
for pipeline-runtime hang debugging, a smaller debug target is now the higher
confidence path. The latest mainline image exceeded detail-placement CLB
capacity, and the noTrace image still failed route legality with `5975` node
overlaps. That is a broad routability/resource problem, not a software or
gdbserver setup problem.

Keep exact `12p4c128sbus32cfg` as a final-validation target, but do not block
gdbserver bring-up on it. A practical next split is:

1. Build a resource-reduced cfg32 NIC debug image that preserves the same
   software-visible NIC, cfg32 register ABI, DMA path, and ReRoCC/pair-manager
   programming model, but reduces manager/pair count enough to close quickly.
2. In parallel, only if capacity permits, run one carefully chosen exact-12p
   implementation experiment after reviewing route/congestion reports. Do not
   spend multiple serial bitstream turns on strategy-only retries unless a
   report shows a narrow physical bottleneck.

## If noTrace succeeds in a future rebuild

1. Extract AGFI/AFI and result directory from FireSim build logs.
2. Update:

```text
sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml
```

Keep the existing `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic`
key, but set:

```text
agfi: <new noTrace AGFI>
driver_tar: /home/ubuntu/chipyard/sims/firesim/sim/output/f2/f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/driver-bundle.tar.gz
```

3. Commit the HWDB update as its own checkpoint with AGFI/AFI, config, recipe,
   result directory, driver bundle hash, and limitations.
4. Run:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh infrasetup
RUN_HOST_PRIVATE_IP="$(generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh current-private-ip)"
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh run "${RUN_HOST_PRIVATE_IP}"
```

5. Wait for `[gdbserver] phase=listening`.
6. Use a real GDB attach as the first TCP client:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh \
  "${RUN_HOST_PRIVATE_IP}" <guest-ip-or-endpoint>
```

Do not use `nc`, `telnet`, or port scanners against `gdbserver --once`.

## Current noTrace failure

The completed noTrace attempt failed in route. See:

```text
pipeline-runtime/debug_records/20260506T095500Z.md
```

The key failure is:

```text
ERROR: [Route 35-2] Design is not legally routed. There are 5975 node overlaps.
ERROR: [Constraints 18-1000] Routing results verification failed due to partially-conflicted nets
route_design failed
```

Do not update:

```text
sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml
```

for this failed build.

## If a future noTrace/resource-reduced build fails

Do not immediately launch another bitstream. First collect:

- copied result directory;
- Vivado log around the first `ERROR`;
- post-synth utilization;
- post-opt timing;
- placement failure detail, especially whether it repeats `Place 30-487`
  pblock CLB capacity or fails later in route/timing.

Likely next build choices depend on that failure mode:

- If noTrace repeats pblock CLB capacity, further resource reduction is more
  valuable than another strategy-only run. Candidate reductions include removing
  nonessential observability, reducing bridge overhead, or building a smaller
  debug target to get remote GDB working while keeping a separate 12-manager
  path for final validation.
- If noTrace passes placement but fails route/timing, keep the same RTL and try
  a placement/route strategy variant or timing-focused recipe. Record the
  strategy change separately.
- If noTrace produces an AGFI with timing violations, it may still be useful for
  gdbserver bring-up, but it must be documented as a debug AGFI and validated by
  live behavior, not assumed correct.

## Current local readiness

- `debug-preflight`: PASS.
- `local-freshness`: PASS.
- `network-audit`: PASS for both mainline WithNIC generated target and noTrace
  generated target.
- noTrace driver bundle has been prepared as generated output:

```text
/home/ubuntu/chipyard/sims/firesim/sim/output/f2/f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig-FRFCFS16GBQuadRank_BaseF2Config/driver-bundle.tar.gz
```

The tarball sha256 recorded in the debug log is:

```text
a6380ebc1b3050a78ec49a950214f318c1c73bde345c0da786f111433bd86249
```
