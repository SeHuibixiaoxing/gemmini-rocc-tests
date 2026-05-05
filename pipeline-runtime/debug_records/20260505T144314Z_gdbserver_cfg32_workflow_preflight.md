# 20260505T144314Z - cfg32 NIC gdbserver workflow preflight

## Goal

Before the new cfg32 NIC AGFI exists, verify that the workflow selected for
pipeline-runtime gdbserver debugging will point to the intended runtime/HWDB
files and will enforce the guest NIC audit before consuming F2 time.

## Commands

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh debug-preflight
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh network-audit
```

## Key Results

Workflow selection:

```text
workflow_name=pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh
workflow_tag=pairdummy-sbus128-gdbserver-cfg32-nic
profile_id=pairdummy-sbus128-fixed-v25
runtime_cfg=/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic.yaml
hwdb_cfg=/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml
runtime_topology=example_1config
network_prepare_mode=auto
network_target_audit_enforced=yes
gdbserver_enable=1
gdbserver_bind_addr=0.0.0.0
gdbserver_port=2345
methods=ours2
num_cores=4
num_gemmini=12
num_dma=12
pair_manager_mode=1
dma_force_direct_enable=1
```

Debug preflight:

```text
debug_preflight_probe_tier=2
debug_preflight_trigger_enable=0
debug_preflight_breadcrumb_enable=1
debug_preflight_guest_deep_log_enable=0
debug_preflight_dma_export_probe_enable=0
debug_preflight_dma_fixed_load_probe_enable=0
debug_preflight_status=pass
```

Network target audit:

```text
network_target_audit_status=pass
network_target_audit_target_glob=*WithNIC*GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128
network_target_audit_has_nic=yes
network_target_audit_nic_hit=184: L33: ice-nic@10016000 {
network_target_audit_summary=generated-target-exposes-guest-nic
```

## HWDB Note

The cfg32 NIC HWDB currently still points to the old cfg32 NIC AGFI:

```text
firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic:
    agfi: agfi-02e18c6f7a7a95096
```

This is expected before the active build completes. After the active build
produces a new AGFI/AFI, update
`sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml`
to the new AGFI and matching `driver_tar`, then commit that tested state before
`launchrunfarm/infrasetup/runworkload`.

## Active Build State

At this checkpoint:

- tmux: `pairdummy-cfg32-nic-mainline-20260505T132956Z`;
- no exitcode yet;
- `GoldenGateMain` is alive;
- pane log has progressed to `Starting MidasTransforms`;
- no z1d/f2 AWS instance observed yet.

## Interpretation

The workflow side is ready for the new bitstream, but the HWDB must not be used
as-is for final validation because it still selects the old `agfi-02e18...`
image. The next hard gate after build success is HWDB freshness plus
`infrasetup`.
