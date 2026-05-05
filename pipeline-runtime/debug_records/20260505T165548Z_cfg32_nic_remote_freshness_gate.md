# cfg32 NIC build remote freshness gate

Time: `2026-05-05 16:55 UTC`

## Build under test

- tmux session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- build config:
  `sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml`
- recipe:
  `sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`
- target config:
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  + `FRFCFS16GBQuadRank_BaseF2Config`
- strategy/frequency: `TIMING`, `20MHz`
- build host: `i-0479b74dd4de8e428`, `z1d.3xlarge`, private IP `192.168.0.60`
- remote Vivado log:
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_05-165008.vivado.log`

## Freshness checks

Local config-specific CL:

```text
sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128-FRFCFS16GBQuadRank_BaseF2Config
```

Local generated Verilog:

```text
FireSim-generated.sv mtime=2026-05-05 16:46:07.228185468 +0000 size=166512822
```

Remote generated Verilog:

```text
remote sv=2026-05-05 16:46:07.228185468 +0000 166512822
```

Required RTL markers:

- `SimpleNICBridgeModule`: present locally and remotely.
- optimized DMA marker `bytes_written_per_beat`: present locally and remotely.
- optimized DMA marker `write_shift`: present locally and remotely.

Network target audit:

```text
network_target_audit_status=pass
network_target_audit_nic_hit=184:        L33: ice-nic@10016000 {
network_target_audit_nic_hit=185:            compatible = "ucb-bar,ice-nic";
```

## Current build phase

The build has passed GoldenGate output generation and is running Vivado on the
remote `z1d.3xlarge` host. At this checkpoint there is no exitcode file and no
AGFI/AFI yet. The pane log shows Vivado synthesis activity and `Synthesis
finished with 0 errors, 0 critical warnings and 230 warnings`; placement has not
yet reached the historical overutilization failure point.

## Notes

The cfg32 property is not exposed as a simple `cfg32` string in
`FireSim-generated.sv`; for this build it is tracked by the build config/recipe
names and the selected target. Final validation must still confirm the HWDB is
updated to the produced AGFI and that the runtime logs use the intended cfg32
software path.
