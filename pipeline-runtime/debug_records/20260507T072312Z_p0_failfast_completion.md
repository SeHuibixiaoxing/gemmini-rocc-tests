Debug category: pipeline-runtime / P0 / fail-fast / gdbserver SOP

# 20260507T072312Z - P0 fail-fast completion checks

## Goal

Close the P0 work items from
`docs/testing/pipeline_runtime_optimization_actions_20260505_draft.md` after the
cfg32 NIC no-TraceIO AGFI proved remote gdbserver attach works.

## Code Changes

- Extended `scripts/audit_pipeline_runtime_artifact.py` to fail fast on:
  - model layer `address` / `address2` + `tensorSize` ranges outside the
    top-level model address span;
  - stage local tensor bytes larger than the corresponding model tensor size;
  - missing buffer bindings for stage tensors;
  - buffer binding `pages_per_slot` too small for the stage tensor bytes;
  - overlapping stage SPM alias windows in a segment.
- Extended `runtime_prepare_stage_spm_windows()` to reject overlapping stage
  SPM alias windows at runtime before stage threads can execute.
- Updated the gdbserver SOP with the currently verified cfg32 NIC no-TraceIO
  route and the P0 breakpoint/classification workflow.
- Added `docs/testing/pipeline_runtime_p0_completion_20260507.md`.

## Verification

Build:

```sh
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all
```

Static positive checks:

```sh
for method in ours2 gemini2 tangram2; do
  python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
    --pipeline-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.${method}.yaml \
    --hardware-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
    --model-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
    --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 \
    --page-size-bytes 1024
done
```

Result:

```text
P0_POSITIVE_PASS method=ours2
P0_POSITIVE_PASS method=gemini2
P0_POSITIVE_PASS method=tangram2
```

Runtime dry-run positive checks:

```text
P0_DRYRUN_PASS method=ours2
P0_DRYRUN_PASS method=gemini2
P0_DRYRUN_PASS method=tangram2
```

Negative checks:

```text
P0_NEGATIVE_PASS page_size
P0_NEGATIVE_PASS model_range
P0_NEGATIVE_PASS stage_window_overlap
P0_NEGATIVE_PASS runtime_stage_window_overlap
```

Other checks:

```text
python3 -m py_compile .../audit_pipeline_runtime_artifact.py
bash -n .../run_pairdummy_cfg32_gdbserver_expect_triage.sh
```

## Interpretation

P0 is complete for the software-side contract and gdbserver debugging surface:
the runtime now has a known-good remote gdbserver path and the main artifact
shape mistakes are caught before a long F2 hang. This does not prove the
pipeline-runtime workload completes on FPGA; the next debugging step should use
gdbserver on the verified AGFI to capture the actual runtime hang stack and
classify it into pipe/ring, DMA, ReRoCC, Gemmini, or thread/fatal categories.
