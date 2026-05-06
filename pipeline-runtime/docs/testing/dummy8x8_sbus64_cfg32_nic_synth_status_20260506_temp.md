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
