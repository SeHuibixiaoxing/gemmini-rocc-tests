# 20260505T184000Z - Host threaded fpga-backend stub run for ours2 batch1

## Goal

Exercise more of the pipeline-runtime scheduling path while the cfg32 NIC F2
bitstreams are still building, without consuming FPGA capacity or modifying
hardware. This run uses the host/x86 binary with `--backend fpga`, which takes
the threaded worker path but uses host stubs for non-RISC-V Gemmini/DMA logic.

This is intentionally different from `--hw-validate-only`: it reaches segment
setup, action allocation, topology build, stage task construction, worker
scheduling, Gemmini issue/fence accounting, and trace emission.

## Command

From `/home/ubuntu/chipyard`:

```sh
PRT=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime
ROOT=conference/HybridMapper/output/pipeline_runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
METHOD=ours2
LOG=/tmp/pipeline-runtime-host-fpga-threaded-ours2-batch1-20260505T1840Z.log
TRACE=/tmp/pipeline-runtime-host-fpga-threaded-ours2-batch1-20260505T1840Z.trace

nice -n 19 timeout 600 "$PRT/pipeline_runtime" \
  --backend fpga \
  --model-yaml "$ROOT/model.layers.yaml" \
  --layer-mapping-yaml "$ROOT/gemmini_layer_mapping.$TARGET.yaml" \
  --skip-model-bin-load \
  --pipeline-yaml "$ROOT/pipeline_mapping.$TARGET.$METHOD.yaml" \
  --skip-input-load \
  --skip-golden-check \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 12 \
  --pair-manager-mode 1 \
  --pages-per-acc 1024 \
  --spm-page-bytes 1024 \
  --spm-xlate-enable 1 \
  --watchdog-ms 300000 \
  --export-dma-timeout-ms 300000 \
  --batch 1 \
  --trace "$TRACE" >"$LOG" 2>&1
```

## Result

Pass:

```text
host_fpga_threaded_rc=0
log=/tmp/pipeline-runtime-host-fpga-threaded-ours2-batch1-20260505T1840Z.log
trace=/tmp/pipeline-runtime-host-fpga-threaded-ours2-batch1-20260505T1840Z.trace
```

The log file is empty because the current host build did not emit progress logs
for this path. The trace file was generated and reported:

```text
run_ns=4246800958
dma_submit_count=0
dma_complete_count=0
dma_inflight_peak=0
gemm_issue_count=40
gemm_fence_count=40
gemm_busy_ns=3946909857
prefetch_attempt_count=0
prefetch_success_count=0
export_submit_ahead_count=0
export_retire_count=0
trace_event_count=122
trace_event_drop_count=0
```

## Interpretation

This confirms the current software tree can run the threaded, fpga-backend host
stub path for the intended 4-core / 12-Gemmini / 12-DMA pair-manager target with
1024-byte SPM pages. It covers a deeper software path than
`--hw-validate-only`, including worker scheduling and task construction.

It still does not prove:

- RISC-V custom instructions;
- live CoupledDMA operation;
- SPM xlate hardware;
- Gemmini hardware drain/fence;
- NIC/gdbserver behavior;
- F2 timing/placement.

## Concurrent Hardware State

At this checkpoint:

- mainline `pairdummy-cfg32-nic-mainline-20260505T132956Z` had finished Vivado
  synthesis report generation and was preparing the synthesized netlist for
  logic optimization; no placement, route, AGFI, or AFI yet.
- no-TraceIO `pairdummy-cfg32-nic-notrace-20260505T171453Z` had reached
  `post-debug-synthesis.fir` but not final `FireSim-generated.sv`.

