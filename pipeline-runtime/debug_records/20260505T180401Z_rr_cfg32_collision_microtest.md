# 20260505T180401Z ReRoCC cfg32 collision microtest

## Context

While the cfg32 NIC bitstreams are still building, I checked whether the current `bertmini` c4_g12 pipeline artifacts can collide with the runtime's ReRoCC cfg assignment scheme.

Relevant runtime logic:

- `RR_MAX_CFGS = 32`
- `rr_cfg_id_for_stage(stage_id, opcode_id)` maps DMA opcode 2 to lane 0 and other opcodes, including Gemmini opcode 3, to lane 1:
  `cfg = stage_id * 2 + lane`
- `PRT_RR_SPM_XLATE_CFG_ID = RR_MAX_CFGS - 1 = 31`

If a segment had stage id 15 using Gemmini opcode 3, it would use cfg 31 and collide with the reserved SPM xlate cfg. If a segment had 16 or more stages, ordinary stage cfg ids could also wrap modulo 32.

## Command

An inline Python/YAML check scanned:

- `conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.gemini2.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.tangram2.yaml`

It computed cfg ids for DMA and Gemmini opcode lanes in every segment and failed on duplicate cfgs or cfg 31 usage.

## Result

Passed:

```text
rr cfg32 stage/opcode collision check passed
```

Observed maximum segment stage counts:

- `ours2`: max 6 stages per segment
- `gemini2`: max 5 stages per segment
- `tangram2`: max 6 stages per segment

No segment used stage id 15, and no ordinary DMA/Gemmini cfg id collided with cfg 31.

## Interpretation

For the current c4_g12 `bertmini` artifacts, cfg32 is sufficient for ordinary stage DMA/Gemmini cfg lanes plus the reserved SPM xlate cfg. This removes one plausible software-side cause for cfg-slot collisions in the pending gdbserver run.

This does not prove future artifacts are safe. A future mapper output with 16 or more stages in one segment, or stage 15 using Gemmini opcode 3, would need a different cfg allocation policy or a reserved-cfg avoidance rule.

## Active build status

- Mainline cfg32 NIC build still alive, no AGFI/AFI.
- noTrace cfg32 NIC build still alive in GoldenGate; `post-split-expressions.fir/json` exist, but no final `FireSim-generated.sv` yet.
