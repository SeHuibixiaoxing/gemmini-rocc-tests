# 20260505T184353Z cfg32 NIC post-synth resource checkpoint

## Context

The active `12p4c128sbus32cfg + optimized DMA + current NIC` mainline F2 build
has reached the first useful Vivado resource checkpoint while the noTrace fallback
is still in GoldenGate.

## Build

- Session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- Build host: `i-0479b74dd4de8e428`, private IP `192.168.0.60`
- Target config:
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  + `FRFCFS16GBQuadRank_BaseF2Config`
- Strategy/frequency: `TIMING`, `20MHz`
- Vivado log:
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_05-165008.vivado.log`
- Post-synth utilization report:
  `build/reports/26_05_05-183555.post_synth_utilization.rpt`

## Evidence

Vivado completed synthesis and entered `link_design`:

- `synth_design completed successfully`
- post-synth DCP generated
- `AWS FPGA: (18:40:01): Start linking customer design ...`
- no place/route/AGFI/AFI result yet

Post-synth top-level utilization:

- `cl_firesim`: `1264037` total LUTs, `96.96%`
- `cl_firesim`: `1047093` logic LUTs, `80.32%`
- `firesim_top`: `1227790` total LUTs, `94.18%`
- `sim`: `1077313` total LUTs, `82.64%`
- DSP: `2079`, `23.04%`

Visible bridge queue resource pressure:

- SimpleNIC from-host queue: `32061` LUTs, `25824` LUTRAMs
- SimpleNIC to-host queue: `34965` LUTs, `28128` LUTRAMs
- Each TracerV to-host queue: roughly `14.5k` LUTs and `11.5k` LUTRAMs
- There are four TracerV to-host queues in the mainline build

## Interpretation

The mainline cfg32 NIC build is very close to F2 LUT capacity before placement.
If it fails in placement, that should be treated as resource/congestion pressure
first, not as evidence against the recovered 1BP gdbserver software path.

The parallel noTrace fallback remains important: it preserves NIC and
pipeline-runtime remote gdbserver usefulness while removing a visible resource
source that is not required for the first user-space GDB attach target.

## Next action

Keep monitoring both builds at the requested `1200s` interval. Do not update the
cfg32 NIC HWDB until a build produces a concrete AGFI/AFI. If mainline fails and
noTrace succeeds, use the noTrace AGFI for the first cfg32 NIC gdbserver attach
test.

