# pipeline-runtime

Inter-layer pipeline runtime skeleton for Chipyard Gemmini (ReRoCC/CoupledDMA-aware).

## What is implemented now

- pthread-based runtime/control/stage threading model.
- DMA backend abstraction:
  - `blocking_fence`
  - `poll_progress_thread` with progress-thread completion queue.
- Gemmini backend abstraction:
  - `blocking_fence`
  - `async_experimental`.
- `ScheduleAction` resource lifecycle in runtime segment loop.
- ReRoCC manager scope helpers for Gemmini/DMA command routing.
- Core runtime APIs and CLI entrypoint.
- Page allocator with configurable page units (default 1KB), all-bank interleave, 2x2 Hilbert order (`0,2,3,1`).
- C1-C8 scheduler function entrypoints and blocking wait predicates.
- Batch-complete termination from sink subbatch progress (`--batch` aware), with watchdog as deadlock guard.
- Multi-segment execution (segment-by-segment swizzle run).
- Preferred-bank page allocation + shared-tensor placement preference (`shared_tensor_is_read_first`).
- Shared C4 tensor canonical page-layout reuse across producer/consumer pipebufs.
- Runtime SPM page-table API (`map/unmap/translate`) with PTBR/PTE/fault observability.
- Paged shared-spad DMA copy helpers (`spm<->spm`, `dram<->spm`, `spm-va`).

## Build

```bash
cd pipeline-runtime
make
```

## Run (example)

```bash
./pipeline_runtime \
  --model-yaml /root/pipeline-data/model.yaml \
  --model-bin /root/pipeline-data/model.bin \
  --model-offset 0 \
  --pipeline-yaml /root/pipeline-data/pipeline.yaml \
  --input /root/pipeline-data/input.bin \
  --golden /root/pipeline-data/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --num-gemmini-mgrs 8 \
  --num-dma-mgrs 8 \
  --gemmini-base-id 0 \
  --dma-base-id 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000
```

## Notes

- YAML loader now extracts segment/stage/ring and entry-export tensor metadata used by runtime build.
- YAML loader also extracts `tensor_spm_util_in_stage/shared/in_ringbuffer` and runtime uses them for page sizing.
- Model YAML loader parses layer metadata (`index/type/param/tensorIds/address/address2`) and resolves tensor addresses as model-relative offsets via `--model-bin`; use `--model-offset` when one bin contains multiple models (with `addr_base` compatibility fallback).
- Runtime topology is now built from YAML (no `mock topology` seeding).
- Shared-spad page addresses are emitted in DMA as `0x40000000 + ppn * 4096`.
- Shared-spad page addresses are emitted in DMA as `0x40000000 + ppn * page_size`.
- Runtime core count can be overridden by `--num-cores` (default 4).
- Manager topology can be configured by:
  - `--num-gemmini-mgrs`
  - `--num-dma-mgrs`
  - `--gemmini-base-id`
  - `--dma-base-id`
- Sync policy can be selected with `--sync-mode async|blocking_debug`.
- With `--spm-xlate-enable 1`, runtime currently forces blocking-debug until async per-page retire support lands.
- Page budget per accelerator can be overridden by `--pages-per-acc` (default 256).
- SPM translation knobs:
  - `--spm-page-bytes`
  - `--spm-xlate-enable`
  - `--spm-xlate-range-base`
  - `--spm-xlate-range-size`
  - `--hw-validate-only`
  - `--hw-validate-only` runs init/preflight only and returns `HW_VALIDATE_ONLY_PASS` on success.
- Gemmini backend accepts:
  - `prt_gemmini_conv_desc_t` and calls `tiled_conv_auto` on target builds.
  - `prt_gemmini_resadd_desc_t` and calls `tiled_resadd_stride_auto` on target builds.
- Runtime supports strict e2e compare:
  - load `--input` and map model input tensors before run
  - compare model output tensors with `--golden` after run
  - print mismatch summary and return non-zero on mismatch
- Runtime executes all pipeline segments by running each segment as a temporary one-segment topology.
- For completion detection in the current segment:
  - on the last segment: strictly use model-output sink tensor IDs
  - on non-last segments: use terminal export tensors in current topology
  - watchdog timeout returns non-zero (`timeout`) instead of success
- Topology page allocations are released on segment/topology teardown; runtime reports allocator leak if release invariants fail.
- Remaining gap: full tensor shape/stride driven byte-accurate mapping is not finished yet.
- `--trace <path>` now emits runtime overlap metrics as `key=value` text, including:
  - `run_ns`
  - DMA: `dma_submit_count`, `dma_complete_count`, `dma_inflight_peak`, `dma_busy_ns`
  - Gemmini: `gemm_issue_count`, `gemm_fence_count`, `gemm_busy_ns`
  - overlap hooks: `prefetch_attempt_count`, `prefetch_success_count`, `export_submit_ahead_count`, `export_retire_count`
  - derived estimates: `overlap_est_ns`, `dma_util_pct`, `gemm_util_pct`, `overlap_est_pct`
  - cycle calibration fields: `trace_cycle_overhead`, `trace_cycle_ref`, `trace_ns_ref`
  - event log fields: `trace_event_count`, `trace_event_drop_count`, `event_format`, `event_<idx>=...`
- Hardware validation gate runner (firesim deploy):
  - `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
  - default policy is `5` consecutive workload passes per gate (`GATE_RUNS` to override).

## Dummy Data Workflow (HybridMapper)

Generate dummy model/input/golden for one model:

```bash
python3 /home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py \
  --model bertmini \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml
```

Artifacts:

- `/home/wzy/proj/HybridMapper/output/pipeline/<model>/dummy_weight/model.bin`
- `/home/wzy/proj/HybridMapper/output/pipeline/<model>/dummy_input/input.bin`
- `/home/wzy/proj/HybridMapper/output/pipeline/<model>/dummy_input/golden/golden.bin`

## Model Input/Output Tensor IDs

Extract model input/output tensor IDs from `layers.yaml` with semantics aligned to
HybridMapper `Model.get_model_in_out_ids()`:

```bash
python3 scripts/extract_model_io_ids.py \
  --layers-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --pretty
```

Optionally save JSON:

```bash
python3 scripts/extract_model_io_ids.py \
  --layers-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --output /tmp/model_io_ids.json \
  --pretty
```

## Session Handoff

- Detailed handoff: `HANDOFF.md`
- New-session prompt template: `NEXT_SESSION_PROMPT.md`
