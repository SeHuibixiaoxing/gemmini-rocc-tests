# 20260505T143739Z - batch8 CPU dry-run and mapper audit

## Goal

Extend the previous host-side checks to the actual FireMarshal profile shape:

- `TARGET_BATCH=8`;
- `NUM_CORES=4`;
- `NUM_GEMMINI=12`;
- `NUM_DMA=12`;
- `PAIR_MANAGER_MODE=1`;
- `PAGES_PER_ACC=1024`;
- default runtime `PRT_PAGE_SIZE_BYTES=1024`.

This is still a CPU-backend software check. It does not replace FPGA validation,
but it narrows the risk that the runtime, YAML artifacts, or workflow arguments
are obviously inconsistent before the cfg32 NIC AGFI is available.

## CPU backend dry-run

Command:

```bash
cd generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime
timeout 240 ./pipeline_runtime \
  --backend cpu \
  --model-yaml ../rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml \
  --layer-mapping-yaml ../rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
  --pipeline-yaml ../rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml \
  --batch 8 \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 12 \
  --pair-manager-mode 1 \
  --gemmini-base-id 0 \
  --dma-base-id 0 \
  --spm-page-bytes 1024 \
  --pages-per-acc 1024 \
  --skip-model-bin-load \
  --skip-input-load \
  --skip-golden-check
```

Result:

- exit code 0;
- no stdout/stderr diagnostics;
- runtime was slower than batch1 but completed within the 240s timeout.

## Mapper/static artifact audit

Command:

```bash
for method in ours2 gemini2 tangram2; do
  python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
    --pipeline-yaml generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.${method}.yaml \
    --hardware-yaml generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
    --model-yaml generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml \
    --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
done
```

Results:

```text
METHOD=ours2
[artifact-audit] PASS target=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 segments=13
[artifact-audit] tensor_type_coverage={'ALL_RINGBUFFER': 14, 'DRAM': 41, 'ISOLATE_SPM': 8, 'SHARED_SPM': 33}
[artifact-audit] buffer_binding_coverage={'PIPE': 96, 'RING': 7, 'WEIGHT': 56}
[artifact-audit] split_kind_coverage={'oc': 32, 'resadd_spatial': 6, 'single': 2}
[artifact-audit] op_type_coverage={'conv': 32, 'resadd': 8}
[artifact-audit] rr_stage_scope_budget=15 reserved_cfg=31

METHOD=gemini2
[artifact-audit] PASS target=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 segments=15
[artifact-audit] tensor_type_coverage={'DRAM': 44, 'ISOLATE_SPM': 52}
[artifact-audit] buffer_binding_coverage={'PIPE': 96, 'RING': 2, 'WEIGHT': 56}
[artifact-audit] split_kind_coverage={'oc': 32, 'resadd_spatial': 4, 'single': 4}
[artifact-audit] op_type_coverage={'conv': 32, 'resadd': 8}
[artifact-audit] rr_stage_scope_budget=15 reserved_cfg=31

METHOD=tangram2
[artifact-audit] PASS target=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 segments=12
[artifact-audit] tensor_type_coverage={'DRAM': 37, 'ISOLATE_SPM': 59}
[artifact-audit] buffer_binding_coverage={'PIPE': 96, 'RING': 5, 'WEIGHT': 56}
[artifact-audit] split_kind_coverage={'oc': 32, 'resadd_spatial': 4, 'single': 4}
[artifact-audit] op_type_coverage={'conv': 32, 'resadd': 8}
[artifact-audit] rr_stage_scope_budget=15 reserved_cfg=31
```

## Active build state

The active bitstream build is still running:

- tmux: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- no exitcode file yet;
- local `GoldenGateMain` still alive;
- pane/manager logs updated again at about `2026-05-05 14:35:33 UTC`,
  confirming this was slow local GoldenGate work rather than a dead process;
- no z1d/f2 AWS instance observed at this checkpoint.

## Interpretation

- Runtime/workflow SPM granularity now matches the fixed FireMarshal profile:
  1024-byte page size and 1024 pages per accelerator.
- `RR_MAX_CFGS=32` and `reserved_cfg=31` remain compatible with the cfg32 target.
- These checks increase confidence in the software/artifact side, but the final
  question still depends on the cfg32 NIC FPGA run and gdbserver attach.
