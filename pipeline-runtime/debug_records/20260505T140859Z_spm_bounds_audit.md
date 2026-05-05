# 20260505T140859Z - SPM tensor bounds audit checkpoint

## Goal

While the `12p4c128sbus32cfg + optimized DMA + current NIC` F2 bitstream is still
in `GoldenGateMain`, add and verify a low-cost software/static guard for one of
the open `问题.md` risks:

- before compute/DMA, prove each SPM tensor byte range fits inside the SPM pages
  assigned to that tensor and inside the stage/action alias window.

This checkpoint does not change the active bitstream build inputs. It only
changes runtime-side fail-fast validation and the local artifact audit script.

## Code changes

- `pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py`
  - validates `localSpmTensorAddr + localSpmTensorBytes` against:
    - `localSpmPageSpan * 1024`;
    - `[localSpmFirstVPage, localSpmFirstVPage + localSpmPageCount) * 1024`.
- `pipeline-runtime/src/prt_runtime.c`
  - validates the same stage SPM bounds in `runtime_prepare_stage_spm_windows`;
  - validates `stage->exec_base_vpage + local_spm_page_span <= action->alias_page_count`;
  - returns `PRT_ERR_PARSE` with stage/slot/tensor/range details before issuing
    SPM xlate, DMA, or Gemmini work.

The check is intentionally fail-fast only. It does not alter manager assignment,
DMA/Gemmini issue order, fences, or SPM page allocation.

## Verification

Command:

```bash
python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
  --pipeline-yaml generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml \
  --hardware-yaml generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
  --model-yaml generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml \
  --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
```

Result:

```text
[artifact-audit] PASS target=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 segments=13
[artifact-audit] tensor_type_coverage={'ALL_RINGBUFFER': 14, 'DRAM': 41, 'ISOLATE_SPM': 8, 'SHARED_SPM': 33}
[artifact-audit] buffer_binding_coverage={'PIPE': 96, 'RING': 7, 'WEIGHT': 56}
[artifact-audit] split_kind_coverage={'oc': 32, 'resadd_spatial': 6, 'single': 2}
[artifact-audit] op_type_coverage={'conv': 32, 'resadd': 8}
[artifact-audit] rr_stage_scope_budget=15 reserved_cfg=31
```

Additional checks:

```bash
python3 -m py_compile \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py

cd generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime
cc -D_GNU_SOURCE -Iinclude -I.. \
  -DPRT_ENABLE_PROGRESS_LOG=0 \
  -DPRT_ENABLE_PROGRESS_RAW_LOG=0 \
  -DPRT_ENABLE_PROGRESS_HOT_LOG=0 \
  -DPIPELINE_RUNTIME_GEMMINI_PHASE_LOG=0 \
  -DPRT_ENABLE_ONLY_MARKER=0 \
  -DPRT_ENABLE_CRITICAL_UART_PROBE=1 \
  -DPRT_ENABLE_CRITICAL_UART_PAD_BURST=0 \
  -DPRT_ENABLE_PROGRESS_PAD_BURST=0 \
  -DPRT_MLOCKALL_MODE=0 \
  -O0 -g -Wall -Wextra -Werror \
  -Wno-error=unused-function \
  -Wno-error=unused-variable \
  -Wno-error=unused-but-set-variable \
  -std=gnu11 -fsyntax-only src/prt_runtime.c
```

Result:

- `py_compile`: pass.
- `prt_runtime.c` syntax-only: pass.
- Existing warnings remain in unrelated code paths in `prt_runtime.c`; they were
  not introduced by this checkpoint.

## Known limitation

Full host `make` still fails before link in `src/prt_dma.c` because existing
debug/trigger helper functions are compiled under the current host macro set
but only used in RISC-V or probe-enabled branches. The first blocker seen in
this checkpoint was:

```text
src/prt_dma.c:1847:7: error: variable 'hw_done' set but not used [-Werror=unused-but-set-variable]
```

followed by multiple existing `-Werror=unused-function` diagnostics in
`prt_dma.c`. This is a host-build hygiene blocker for local tests, not evidence
of a new FPGA/runtime semantic failure.

## Build status at this checkpoint

F2 build session:

- tmux: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- pane log:
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-cfg32-nic-mainline-20260505T132956Z.pane.log`
- manager log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-05--13-29-57-buildbitstream-C1F6YJIUOD2RJEE0.log`

Current state:

- no exitcode file yet;
- tmux still alive;
- still in local `GoldenGateMain`;
- no z1d/f2 EC2 build/run instance observed yet.
