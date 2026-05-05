# cfg32 NIC post-synth resource checkpoint - 2026-05-05

This note records the first useful resource checkpoint from the active
`12p4c128sbus32cfg + optimized DMA + current NIC` F2 build.

## Build under observation

- Session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- Target config:
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  + `FRFCFS16GBQuadRank_BaseF2Config`
- Build config:
  `sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml`
- Recipe:
  `sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`
- Strategy/frequency: `TIMING`, `20MHz`
- Build host: `i-0479b74dd4de8e428`, private IP `192.168.0.60`
- Remote CL:
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config`
- Vivado log:
  `build/scripts/2026_05_05-165008.vivado.log`

## Observed status at 2026-05-05 18:43 UTC

The build has completed customer CL synthesis and generated post-synth reports.
Vivado then closed the synthesis project and entered `link_design`:

- `synth_design completed successfully`
- post-synth checkpoint generated:
  `cl_f2-firesim-...Sbus128-FRFCFS16GBQuadRank_BaseF2Config.2026_05_05-165008.post_synth.dcp`
- `link_design -mode default -reconfig_partitions WRAPPER/CL -top top` started at
  `18:40:01 UTC`
- No place/route/AGFI/AFI result yet.

## Post-synth utilization

Report:

`build/reports/26_05_05-183555.post_synth_utilization.rpt`

Top-level summary:

| Scope | Total LUTs | Logic LUTs | LUTRAMs | FFs | BRAM36 | BRAM18 | DSP |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `cl_firesim` | `1264037 (96.96%)` | `1047093 (80.32%)` | `216218 (35.98%)` | `624396 (23.95%)` | `44 (2.18%)` | `68 (1.69%)` | `2079 (23.04%)` |
| `firesim_top` | `1227790 (94.18%)` | `1014384 (77.81%)` | `213346 (35.50%)` | `581071 (22.29%)` | `18 (0.89%)` | `67 (1.66%)` | `2076 (23.01%)` |
| `sim` | `1077313 (82.64%)` | `967115 (74.18%)` | `110138 (18.33%)` | `559357 (21.45%)` | `16 (0.79%)` | `67 (1.66%)` | `2076 (23.01%)` |

Visible high-cost bridge queues:

- `SIMPLENICBRIDGEMODULE_0_from_cpu_stream_incomingQueueIO_q`:
  `32061` total LUTs, including `25824` LUTRAMs.
- `SIMPLENICBRIDGEMODULE_0_to_cpu_stream_outgoingQueueIO_q`:
  `34965` total LUTs, including `28128` LUTRAMs.
- Four `TRACERVBRIDGEMODULE_*_to_cpu_stream_outgoingQueueIO_q` queues:
  roughly `14.5k` total LUTs and `11.5k` LUTRAMs each.

Representative target compute cost:

- Each `GemminiCoupledDMAPairWrapper_*` instance is roughly `36.8k` LUTs and
  `168` DSPs.
- Each internal `GemminiCoupledDMA_*` is roughly `3.8k` LUTs.

## Interpretation

The mainline build is resource-tight before placement. Total LUT utilization is
already about `97%`, so the 2026-05-01 placement failure remains a plausible
risk even at `20MHz`.

This does not prove the mainline build will fail, but it changes the expected
failure mode: if placement fails, the first explanation should be F2 resource
pressure/congestion, not gdbserver software or 8BP residue.

The active no-TraceIO fallback build is justified by this report. Removing the
four TracerV bridge output queues should save on the order of tens of thousands
of LUT/LUTRAM resources while preserving the NIC, block device, FASED memory and
pipeline-runtime debugging goal. Since the current debugging target is
user-space `pipeline-runtime` via remote gdbserver, TraceIO is not required for
the first usable cfg32 NIC AGFI.

## Next checks

- Continue the mainline build until placement/route gives a clear verdict.
- Continue the no-TraceIO build and audit its generated RTL once
  `FireSim-generated.sv` exists.
- If mainline fails at placement but noTrace succeeds, update the cfg32 NIC HWDB
  to the noTrace AGFI and run the gdbserver attach test there first.
- If both fail due to placement/resource pressure, choose the next hardware
  change from the utilization report rather than from 8BP or generic GDB logic.

