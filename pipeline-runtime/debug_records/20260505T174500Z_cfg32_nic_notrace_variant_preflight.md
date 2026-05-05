# 20260505T174500Z cfg32 NIC no-TraceIO variant preflight

## Goal

Prepare a parallel resource-reduced F2 build variant for `12p4c128sbus32cfg + optimized DMA + current NIC`
while the mainline cfg32 NIC build is still in Vivado synthesis.

The motivation is the 2026-05-01 cfg32 NIC placement failure, where post-synth utilization and pblock
errors showed the design was near or above the F2 resource boundary. Remote `gdbserver` does not require
TracerV, so disabling target TraceIO is a low-risk way to remove TracerV bridge pressure while preserving
NIC, block device, FASED, and the default FireSim bridge environment.

## Mainline build still running

- session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- status at this preflight: no exitcode; tmux alive; remote Vivado synthesis still running
- no AGFI/AFI yet

## New variant

New TargetConfig:

```text
FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig
```

Composition:

```text
WithNIC
chipyard.config.WithNoTraceIO
WithDefaultFireSimBridges
WithFireSimConfigTweaks
chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128
```

This mirrors the proven single-core `FireSimRocketNICNoTraceConfig` pattern: `WithNoTraceIO` overrides the
TraceIO enabled by `WithFireSimConfigTweaks`, so `WithTracerVBridge` has no target TracePort to bind.

New build config:

```text
sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace.yaml
```

New recipe:

```text
sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic_notrace.yaml
```

Build key:

```text
firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace
```

Strategy/frequency:

- `TIMING`
- `20MHz`

## Static checks

Commands:

```bash
git diff --check -- \
  generators/firechip/chip/src/main/scala/TargetConfigs.scala \
  sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace.yaml \
  sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic_notrace.yaml
```

Result: pass.

```bash
python3 - <<'PY'
import yaml
for path in [
 'sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic_notrace.yaml',
 'sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic_notrace.yaml',
]:
    with open(path) as f:
        yaml.safe_load(f)
    print(path, 'ok')
PY
```

Result: both YAML files parsed.

## Limitation

This preflight only creates and statically checks the variant. It does not prove that the new TargetConfig
elaborates, reaches Vivado, fits, or runs `gdbserver`. Those claims require the actual build and subsequent
freshness / runfarm validation.
