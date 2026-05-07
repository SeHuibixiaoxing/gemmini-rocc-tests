Debug category: pipeline-runtime / FireMarshal / dummy8x8 sbus64 / image closure

# 20260507T080300Z - dummy8x8 sbus64 image closure

## Goal

Ensure the FireMarshal image used by the current gdbserver F2 run contains the
dummy8x8/sbus64 HybridMapper artifacts rather than the previous
dummy16x16/sbus128 artifacts.

## Problem Found

The first `image-closure` run still rendered `/firemarshal.env` with:

```text
PIPELINE_RUNTIME_PROFILE_ID=pairdummy-sbus128-fixed-v25
TARGET_KEY=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
```

Root cause:

- `host-init-fileonly-sync-pairdummy.sh` always sourced
  `pairdummy_sbus128_fixed_env.sh`, overriding the `TARGET_KEY` exported by the
  wrapper workflow.
- `render_pairdummy_guest_env.sh` started from the checked-in baseline and only
  overlaid debug/gdb variables, not hardware/profile variables.

## Fix

- `host-init-fileonly-sync-pairdummy.sh` now sources the default sbus128 fixed
  env only when no `TARGET_KEY` is already set, unless
  `PAIRDUMMY_FIXED_ENV_SCRIPT` is explicitly provided.
- `render_pairdummy_guest_env.sh` now overlays profile/hardware/runtime core
  variables such as `PIPELINE_RUNTIME_PROFILE_ID`, `TARGET_KEY`, `METHODS`,
  `NUM_GEMMINI`, `NUM_DMA`, `PAIR_MANAGER_MODE`, page settings, skip flags, and
  capture/log controls.

## Verification

Re-ran:

```sh
pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh marshal-build
pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh marshal-install
render_pairdummy_guest_env.sh <effective-env>
apply_guest_env_to_image.sh <image> <effective-env>
verify_pairdummy_firemarshal_image_freshness.sh <workload-json>
```

The build log shows the correct mapping cache:

```text
gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml.cache.bin
```

Image `/firemarshal.env` now contains:

```text
PIPELINE_RUNTIME_PROFILE_ID=pairdummy-sbus64-dummy8x8-fixed-v1
TARGET_KEY=rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64
METHODS=ours2
NUM_GEMMINI=12
NUM_DMA=12
```

Image artifact directory now contains:

```text
gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml
gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml.cache.bin
hardware_target.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml
pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.ours2.yaml
pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.gemini2.yaml
pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.tangram2.yaml
```

This closes the configuration mismatch before F2 testing.
