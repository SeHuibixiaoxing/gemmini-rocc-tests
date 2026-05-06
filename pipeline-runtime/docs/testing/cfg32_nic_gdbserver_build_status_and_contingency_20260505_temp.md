# cfg32 NIC gdbserver build status and contingency - 2026-05-05

This temporary note tracks the active `12p4c128sbus32cfg + optimized DMA +
current NIC` bitstream path for pipeline-runtime remote gdbserver debugging.

## Current state

The common remote gdbserver software path is not the current blocker. The
single-core 1BP NIC smoke has already passed on the recovered current AGFI, and
the cfg32 workload image still passes `debug-preflight` and `local-freshness`.

The current blocker is obtaining a fresh cfg32 NIC AGFI that matches the current
hardware source closely enough to debug pipeline-runtime hangs.

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
- Current result: still running, no AGFI/AFI yet.
- Current observed phase: Vivado `place_design completed successfully` as of
  `2026-05-06 00:19 UTC`; post-place checkpoint/report writing was underway.
- Post-synth `cl_firesim` utilization: total LUT `89.67%`, logic LUT
  `78.72%`, LUTRAM `23.63%`.

The noTrace build leaves `DigitalTop` essentially unchanged and removes
TraceIO-related FireSim top-level pressure. That is why it is the current best
candidate: the user-space pipeline-runtime gdbserver workflow does not need
TraceIO. It has now crossed the mainline build's exact detail-placement failure
boundary and completed placement, but it is still not a usable AGFI until
route, bitstream generation, and AFI creation complete. The next risk is route
closure under the post-place congestion warning and `WNS=-3.250` timing summary.

## If noTrace succeeds

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

## If noTrace fails

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
