# cfg32 NIC build static freshness checkpoint

Timestamp: `2026-05-05T16:31:39Z`

## Goal

Record a static checkpoint while the `12p4c128sbus32cfg + optimized DMA + current NIC`
F2 build is still running. This is not an AGFI validation result; it only confirms
that the currently generated target and GoldenGate inputs still expose the expected
NIC and optimized DMA markers.

## Active build

- tmux session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- command:
  `scripts/firesim-tmux-run.sh --session-name pairdummy-cfg32-nic-mainline-20260505T132956Z buildbitstream -b config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml -r config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`
- pane log:
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-cfg32-nic-mainline-20260505T132956Z.pane.log`
- manager log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-05--13-29-57-buildbitstream-C1F6YJIUOD2RJEE0.log`

At this checkpoint:

- no tmux exitcode file exists yet
- the build tmux session is alive
- local GoldenGate Java is still running
- no z1d/f2 build/runfarm instance has been launched yet

## Static checks

Network target audit:

```text
network_target_audit_target_dir=/home/ubuntu/chipyard/sims/firesim-staging/generated-src/firechip.chip.FireSim.WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128
network_target_audit_has_blkdev=yes
network_target_audit_has_nic=yes
network_target_audit_nic_hit=184:		L33: ice-nic@10016000 {
network_target_audit_nic_hit=185:			compatible = "ucb-bar,ice-nic";
network_target_audit_status=pass
network_target_audit_summary=generated-target-exposes-guest-nic
```

Generated RTL markers:

- `FireSim-generated.sv` contains `SimpleNICBridgeModule`
- `FireSim-generated.const.h` contains `ep_4:SimpleNICBridgeModule`
- `FireSim-generated.sv` contains optimized DMA marker `bytes_written_per_beat`
- source `generators/gemmini/src/main/scala/gemmini/DMA.scala` contains
  `bytes_written_per_beat` and `write_shift`

The cfg32 NIC wrapper still points at:

- runtime config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic.yaml`
- hwdb config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml`

## Interpretation

This rules out the old non-NIC AGFI mistake for the active generated target: the
currently generated target exposes `ice-nic@10016000`, and the GoldenGate output
contains the SimpleNIC bridge. It also shows that the optimized DMA source-level
marker reached generated RTL.

It does not prove the final F2 AGFI will pass placement, timing, boot, network,
or remote gdbserver attach. The next required milestone remains the actual
build result and HWDB update to the new AGFI/driver tar.
