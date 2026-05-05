# Static artifact audit enhancement checkpoint

Timestamp: `2026-05-05T15:09:23Z`

## Context

The `12p4c128sbus32cfg + optimized DMA + current NIC` F2 bitstream build is still
running in tmux session `pairdummy-cfg32-nic-mainline-20260505T132956Z`. While
waiting for GoldenGate/Vivado, I strengthened software-only artifact checks that
can catch mapper/runtime contract bugs before the new AGFI is available.

## Code changes

- `pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py`
  - Added `--page-size-bytes`.
  - Checks `pages_per_acc * page_size_bytes == shared_spad_local_size_bytes`.
  - Checks each stage `[execBaseVPage, execBaseVPage + localSpmPageSpan)` is
    inside `segmentSpmPageSpan`.
  - Checks explicit `pAccIdxList` assignments do not reuse the same physical
    manager across stages in one segment.
- `pipeline-runtime/scripts/run_bertmini_host_closure.sh`
  - Passes `--model-yaml model.layers.yaml` to the artifact auditor, matching
    the auditor's existing op/tensor cross-check contract.

## Verification

Positive audit:

```bash
/home/ubuntu/chipyard/.conda-env/bin/python \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
  --pipeline-yaml /home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.<method>.yaml \
  --hardware-yaml /home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
  --model-yaml /home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
  --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 \
  --page-size-bytes 1024
```

Methods tested: `ours2`, `gemini2`, `tangram2`.

Result: all three passed.

Negative audit:

```bash
/home/ubuntu/chipyard/.conda-env/bin/python \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
  --pipeline-yaml /home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml \
  --hardware-yaml /home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
  --model-yaml /home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
  --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 \
  --page-size-bytes 4096
```

Result: expected failure:

```text
[artifact-audit] FAIL target pages_per_acc=1024 * page_size=4096 != shared_spad_local_size_bytes=1048576
rc=1
```

Syntax checks:

```bash
/home/ubuntu/chipyard/.conda-env/bin/python -m py_compile \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py

bash -n \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh

bash -n \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh
```

Result: all passed.

## Limitations

This checkpoint is a static/software-only milestone. It does not validate the
active cfg32 NIC bitstream, remote gdbserver on the new AGFI, DMA hardware, or
pipeline-runtime execution on FPGA. Those remain blocked on bitstream completion.
