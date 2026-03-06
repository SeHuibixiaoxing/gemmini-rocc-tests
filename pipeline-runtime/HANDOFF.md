# Pipeline Runtime Handoff (2026-03-01)

## Update (2026-03-05): ReRoCC + CoupledDMA Migration

- Added hardware gate automation script:
  - `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
  - strict order:
    1. baremetal metasim
    2. baremetal FPGA
    3. Linux FPGA
  - each gate checks `uartlog` for `ALL_TESTS_PASS`.
  - each gate now runs workload `5` times by default (`GATE_RUNS` override).
- Added SPM paging runtime interfaces and metadata:
  - `prt_spm_map_tensor/unmap/translate_range`
  - `spm_ptbr_pa`, `spm_pte_count`, `spm_fault_*` exported into action/trace.
- Added paged DMA transfer APIs and scheduler integration:
  - C1/C2/C3/C5/C6 now use page-granular shared-spad copy paths.
- Added CLI switches:
  - `--spm-page-bytes`
  - `--spm-xlate-enable`
  - `--spm-xlate-range-base`
  - `--spm-xlate-range-size`
  - `--hw-validate-only`
- `--hw-validate-only` is now executable as init-only preflight and prints `HW_VALIDATE_ONLY_PASS` on success.
- Added Gemmini translation control header/interface stubs:
  - `include/rerocc_gemmini_spm_xlate.h`
  - `prt_gemmini_spm_xlate_*` in `prt_rerocc` (real instruction path gated by `PRT_ENABLE_GEMMINI_SPM_XLATE_INSN`).
- Current runtime guardrail:
  - enabling `spm_xlate` currently forces blocking-debug mode for correctness until async per-page token retire is implemented.
- Linux coupled-DMA microbench updates:
  - `rerocc-linux-tests-coupleddma/rerocc_dma_matrix_linux_coupleddma.c` now supports multi-page payloads via software page-walk + per-page DMA submit.
  - `rerocc-linux-tests/rerocc_gemmini_conv_matrix.c` now validates `conv + resadd` (not conv-only).
  - quick marshal profiles use `--bytes 65536` for cross-page coverage.

- Added `ScheduleAction` runtime resource lifecycle:
  - `include/prt_schedule_action.h`
  - `src/prt_schedule_action.c`
- `ScheduleAction` now records SPM page views after topology bind:
  - in-stage page view (`stage/tensor/slot`)
  - ring page view (`tensor/slot`)
- Translation strategy freeze (new):
  - DRAM paging (OS/MMU, 4KB) and shared-spad paging (runtime managed, default 1KB) are two independent systems.
  - dedicated spad-move DMA does software-side translation and per-page submission.
  - Gemmini compute load/store DMA front-end executes dual-path translation (DRAM-TLB + shared-spad pager).
- Added ReRoCC manager scope wrappers:
  - `include/prt_rerocc.h`
  - `src/prt_rerocc.c`
- Segment execution now runs per-action:
  - generate action
  - allocate managers
  - allocate/bind SPM ownership
  - execute segment
  - release action resources
- Runtime now records stage manager metadata:
  - primary gemmini manager
  - primary dma manager
  - stage tile count from `accUtil`
  - stage manager list
- Strict parser enforcement:
  - each stage must provide `accUtil`
- New runtime CLI:
  - `--num-gemmini-mgrs`
  - `--num-dma-mgrs`
  - `--gemmini-base-id`
  - `--dma-base-id`
  - `--sync-mode async|blocking_debug`
- Default mode changed to async.
- Gemmini issue/fence policy updated:
  - issue path does not fence immediately
  - dependency boundary fences managers used by the stage (`rr_fence` per manager)
  - blocking debug mode = issue + immediate dependency-boundary fence.
- Split policy updated for multi-core execution:
  - `conv`: OC split first, generic 2D spatial rectangle split fallback
  - `resadd`: generic 2D rectangle split
  - unsplittable requests now hard-fail.
- ReRoCC/CoupledDMA plan is tracked in:
  - `pipeline-runtime/PLAN_REROCC_COUPLEDDMA.md`

This handoff is for continuing work across Codex sessions on:

- Chipyard runtime repo:
  - `/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime`
- HybridMapper data repo:
  - `/home/wzy/proj/HybridMapper`

## 1. Current State

### 1.1 Runtime core capabilities implemented

- Blocking pthread runtime skeleton (`control + stage workers`).
- C1-C8 pipeline-buffer taxonomy and scheduler entrypoints.
- YAML-driven topology build (removed mock topology).
- Model address resolution:
  - primary: `model_blob + model_offset + layer_address`
  - fallback compatibility: `model_blob + model_offset + (layer_address - addr_base)`
- Shared-spad DMA global address mapping:
  - `0x40000000 + ppn * 4096`
- Optional real Gemmini conv dispatch via `tiled_conv_auto` when `opaque_task` is built.
- Optional real Gemmini operator dispatch now includes:
  - `conv` via `tiled_conv_auto`
  - `resadd` via `tiled_resadd_stride_auto`
- Model I/O tensor ID extractor script exists in runtime repo:
  - `scripts/extract_model_io_ids.py`
  - semantics aligned with HybridMapper `Model.get_model_in_out_ids()`
- Runtime normal completion now follows batch progress:
  - sink subbatch progress reaches target (`--batch`)
  - watchdog timeout is deadlock-only guard and returns non-zero.
- Sink policy:
  - last segment uses strict model-output sinks
  - intermediate segments use terminal-export sinks in current topology.
- Runtime executes multi-segment pipelines via segment-by-segment topology swizzle.
- Shared C4 tensors reuse canonical per-slot physical page layouts across producer/consumer buffers.
- Page allocation honors preferred accelerator sets with all-bank fallback.
- Topology-lifetime page allocations are explicitly released and leak-checked on topology teardown.
- `poll_progress_thread` DMA backend is functional (progress thread + pending-token queue + timeout-aware wait).
- `gemmini_mode=async_experimental` now issues Gemmini work without immediate fence.
- Stage worker performs C1/C5 double-buffer prefetch on `no_use_idx` during async compute, then fences before export publication.
- C2/C6 export path now supports submit-ahead overlap:
  - per-slot in-flight DMA token tracking in pipebuf.
  - ordered delayed completion retirement (`subbatch_offset` advances on completion only).
  - stage dependency gate polls and retires export completions before next compute barrier.
- Runtime now emits overlap metrics with `--trace <path>`:
  - DMA/Gemmini active totals, inflight peak, submit-ahead/retire counters, and overlap estimates.
- Runtime trace now includes cycle calibration/event timeline fields for FPGA correlation:
  - `trace_cycle_overhead`, `trace_cycle_ref`, `trace_ns_ref`
  - `trace_event_count`, `trace_event_drop_count`
  - `event_format=idx,stage,kind,cycle,ns,aux0,aux1`
  - `event_<idx>=...`
- Stage worker now binds thread affinity (Linux) using `stage_acc_id`-derived CPU selection from current allowed cpuset to keep tile-local RoCC ownership stable.
- Runtime data path scope is locked to Gemmini-internal shared scratchpad windows only; SBUS scratchpad is not used.

### 1.2 Runtime configurability changes landed

- `--num-cores <n>` added.
- `--pages-per-acc <n>` added.
- `PRT_MAX_CORES` increased to `32`.
- `--model-offset <bytes>` supported and validated.

### 1.3 Real pipeline YAML parser compatibility fix landed

HybridMapper `entire_model` YAML has stage blocks where `accUtil` appears before `globalStageId`:

- Before fix: parser created stage only at `globalStageId`, causing dropped stage fields and parse failures.
- After fix: parser creates stage on list item `- ... accUtil: ...`, then patches stage id on `globalStageId`.

### 1.4 Model parser and strict e2e compare status

- Model parser now handles `layers.yaml` list-item ordering where `address/address2` appear before `index`.
- Runtime now supports strict compare path:
  - load `--input` and map model input tensors before run
  - load `--golden` and compare model output tensors after run
  - mismatch summary is printed; return code is non-zero (`PRT_ERR_MISMATCH`).

## 2. Cross-Repo Dummy Data + Golden Generation

Script added in HybridMapper:

- `/home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py`

It reads `layers.yaml` and emits:

- `output/pipeline/<model>/dummy_weight/model.bin`
- `output/pipeline/<model>/dummy_input/input.bin`
- `output/pipeline/<model>/dummy_input/golden/golden.bin`
- `output/pipeline/<model>/dummy_weight/manifest.json`

Notes:

- `model_offset` is fixed to `0` in generated manifest.
- Golden is a simple CPU-side pseudo implementation for flow validation:
  - supports `conv` and `resadd` semantics in a lightweight way
  - **not** Gemmini bit-exact reference math.

## 3. Validated Commands (BertMini)

### 3.1 Generate dummy artifacts

```bash
python3 /home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py \
  --model bertmini \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml
```

### 3.2 Build runtime

```bash
make -C /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j4
```

### 3.3 Run runtime with 8 cores

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 500
```

Observed result: `RC:0`.

### 3.4 Negative compare check (tampered golden)

```bash
cp /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin /tmp/golden_bad.bin
orig=$(od -An -tu1 -N1 /tmp/golden_bad.bin | tr -d ' ')
new=$(( (orig + 1) % 256 ))
printf "\\$(printf '%03o' "$new")" | dd of=/tmp/golden_bad.bin bs=1 seek=0 count=1 conv=notrunc status=none

/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /tmp/golden_bad.bin \
  --batch 1 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 500
```

Observed result: non-zero return code with mismatch summary.

### 3.5 Deadlock-guard timeout check

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --batch 4294967295 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 1
```

Observed result: non-zero return code with `timeout`.

### 3.6 100-batch stress check

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --batch 100 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000
```

Observed result: `RC:0`.

### 3.7 Async overlap smoke check (`poll_progress_thread + async_experimental`)

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 10 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000 \
  --dma-backend poll_progress_thread \
  --gemmini-mode async_experimental
```

Observed result: `RC:0`.

### 3.8 Trace metrics smoke check

Blocking baseline trace:

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 500 \
  --trace /tmp/prt_trace_blocking.txt
```

Async overlap trace:

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 10 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000 \
  --dma-backend poll_progress_thread \
  --gemmini-mode async_experimental \
  --trace /tmp/prt_trace_async.txt
```

Observed result: both runs `RC:0`; trace files contain `key=value` metrics.
Observed additional result: async trace contains non-zero `export_submit_ahead_count/export_retire_count` with zero event drops in host smoke.

## 4. Known Gaps / Risks

- Golden path is flow-oriented, not mathematically aligned with real Gemmini kernels.
- Lightweight YAML parser still has schema fragility compared with `libyaml`.
- `pages-per-acc` currently requires manual tuning (e.g. `4096` for bertmini 8-core case).
- Full shape/stride exact byte accounting is incomplete.
- `resadd` dispatch is wired, but target-side parameter-coverage validation (RISC-V/FPGA) is still pending.
- FPGA-side overlap validation and metric calibration are still pending.

## 5. Recommended Next Steps (Priority Order)

1. Validate overlap correctness/perf on target RISC-V/FPGA (`GemminiLearningConfigSpadNoC`) with trace evidence.
2. Expand `resadd` validation and parameter-coverage tests on target RISC-V/FPGA runs.
3. Replace pseudo golden with deterministic CPU reference kernels matching layer params.
4. Move lightweight parser to `libyaml` for robust full-schema parsing.

## 6. Key Files to Inspect First in Next Session

- Runtime:
  - `src/prt_runtime.c`
  - `src/prt_yaml_loader.c`
  - `src/main.c`
  - `src/prt_page_table.c`
- HybridMapper helper:
  - `/home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py`
