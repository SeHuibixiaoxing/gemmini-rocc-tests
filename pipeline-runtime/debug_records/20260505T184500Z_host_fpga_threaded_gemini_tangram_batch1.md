# 20260505T184500Z - Host threaded fpga-backend stub run for gemini2/tangram2 batch1

## Goal

Extend the host/x86 `--backend fpga` threaded runtime check from `ours2` to the
other two current bertmini mapping methods while the cfg32 NIC bitstreams are
still building.

This remains a software-only host stub test. It does not execute RISC-V custom
instructions or live F2 hardware.

## Command

From `/home/ubuntu/chipyard`, run the same low-priority command for
`gemini2` and `tangram2`:

```sh
PRT=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime
ROOT=conference/HybridMapper/output/pipeline_runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128

for METHOD in gemini2 tangram2; do
  LOG=/tmp/pipeline-runtime-host-fpga-threaded-${METHOD}-batch1-20260505T1845Z.log
  TRACE=/tmp/pipeline-runtime-host-fpga-threaded-${METHOD}-batch1-20260505T1845Z.trace
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
done
```

## Result

Both methods passed:

```text
method=gemini2 rc=0
method=tangram2 rc=0
```

Trace summaries:

```text
gemini2: run_ns=3964484785
gemini2: gemm_issue_count=40
gemini2: gemm_fence_count=40
gemini2: dma_submit_count=0
gemini2: trace_event_count=122
gemini2: trace_event_drop_count=0

tangram2: run_ns=3925914250
tangram2: gemm_issue_count=40
tangram2: gemm_fence_count=40
tangram2: dma_submit_count=0
tangram2: trace_event_count=122
tangram2: trace_event_drop_count=0
```

## Interpretation

Together with the earlier `ours2` host threaded run, all three current bertmini
mapping methods now pass the host threaded fpga-backend stub path for:

- `num_cores=4`
- `num_gemmini_mgrs=12`
- `num_dma_mgrs=12`
- `pair_manager_mode=1`
- `pages_per_acc=1024`
- `spm_page_bytes=1024`
- `spm_xlate_enable=1`
- `batch=1`

This makes a purely host-side scheduling/task-construction bug less likely for
the first F2 run. The remaining unknowns are still the real RISC-V/F2 hardware
paths: DMA, SPM xlate, Gemmini drain/fence, NIC, and gdbserver.

