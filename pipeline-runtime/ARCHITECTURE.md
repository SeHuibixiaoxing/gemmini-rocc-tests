# Chipyard Pipeline Runtime: Final Execution Plan and Architecture

Date: 2026-03-05  
Target platform: `GemminiLearningConfigSpadReRoCCNoCCoupledDMAParametric` (Linux user-space, C + pthread)

## Update (2026-03-05): ReRoCC + CoupledDMA Track

- New target track is now active: `GemminiLearningConfigSpadReRoCCNoCCoupledDMAParametric`.
- Runtime now includes Mudnac-style `ScheduleAction` lifecycle for per-segment resource ownership:
  - generate, allocate ACC, allocate SPM, bind topology, release.
- `ScheduleAction` SPM source now exports observable page views:
  - flattened `in_stage_pages[stage][tensor][slot]`
  - flattened `ring_pages[tensor][slot]`
- Manager routing is no longer tile-local CPU ownership:
  - stage resources bind to ReRoCC manager IDs (`gemmini_base_id`, `dma_base_id`).
- New strict parser policy:
  - `stages[].accUtil` is mandatory and must be in `{1,2,4,8,16,32}`.
- New runtime config knobs:
  - `num_gemmini_mgrs`, `num_dma_mgrs`, `gemmini_mgr_base_id`, `dma_mgr_base_id`, `sync_mode`.
- Default policy is async-capable, but current SPM page-granular transfer path forces blocking-debug when `spm_xlate_enable=1` for correctness.
- Gemmini async strategy is now explicit:
  - issue path is non-blocking at submit point (no immediate `rr_fence`)
  - fence happens only at dependency boundaries (`rr_fence` per manager used by current stage task)
- Multi-core split policy is now output-centric:
  - `conv`: OC split first, 2D spatial rectangle split fallback with halo repack
  - `resadd`: 2D rectangle split on `(I, J)` output domain
  - cannot-split cases are explicit runtime errors.
- Translation strategy is now frozen as dual paging system:
  - DRAM paging (OS/MMU, 4KB) and shared-spad paging (runtime managed, default 1KB) are independent.
  - dedicated spad-move DMA does software-side translation and per-page submission.
  - Gemmini load/store DMA front-end performs dual-path translation (DRAM-TLB path + shared-spad pager path).
- Detailed migration plan is tracked in:
  - `pipeline-runtime/PLAN_REROCC_COUPLEDDMA.md`
- Hardware gate order is now locked and automated:
  - metasim baremetal -> FPGA baremetal -> FPGA Linux
  - runner: `sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
  - default policy: 5 consecutive workload passes per gate (`GATE_RUNS` override).
- CoupledDMA hardware microbench hardening:
  - Linux DMA matrix now supports multi-page payloads with software page-walk and per-page submit.
  - Linux Gemmini matrix now validates `conv + resadd`.
  - quick marshal profiles use `--bytes 65536` for cross-page DMA coverage.
- Runtime preflight mode:
  - `pipeline_runtime --hw-validate-only` now executes init-only validation and returns `HW_VALIDATE_ONLY_PASS` on success.

## 1. Requirement Baseline (Locked)

This plan is based on confirmed requirements:

1. Layer types in `layers.yaml`: only `conv` and `resadd`.
2. Completion target: runtime correctness first, then performance.
3. Model I/O identity must follow HybridMapper `Model.get_model_in_out_ids()`.
4. DMA default mode: blocking fence.
5. Gemmini compute and DMA overlap is mandatory (can be staged after correctness baseline).
6. Memory/page strategy must match MudnacSim effective behavior.
7. FPGA SoC final deployment and validation are required.
8. Pipeline/runtime porting must align with MudnacSim code behavior (not stale comments), with mandatory references:
   - `tmp/MudnacSim/src/runtime/pipeline.cpp`
   - `tmp/MudnacSim/src/runtime/pipeline_stage.cpp`
   - `tmp/MudnacSim/src/runtime/pipeline_buffer.cpp`
   - `tmp/MudnacSim/src/runtime/spm_manager.cpp`
   - `tmp/MudnacSim/src/runtime/runtime.cpp` (`AllocSpmAllBankPageInter` and `PipelineRuntime` methods)
9. `GemminiLearningConfigSpadNoC` routing constraint is mandatory:
   - a CPU/hart can only control the Gemmini and DirectDMA attached to its own tile (no cross-tile command targeting).
   - cross-tile data exchange must use DMA source/destination global addresses (including remote shared-spad windows), not remote RoCC command dispatch.
   - runtime stage threads must be pinned to designated CPUs/harts to keep ownership stable.
10. Scratchpad scope constraint is mandatory:
   - pipeline runtime data path must use only per-tile Gemmini-internal shared scratchpad windows.
   - SBUS-side scratchpad (if present in SoC config) is out of runtime scope and must not be used.
11. Paging and residency constraints are mandatory:
   - DRAM paging and shared-spad paging are separate systems with different page sizes.
   - during compute, one tensor must be fully in shared-spad or fully in DRAM (no mixed placement).

## 2. Current Implementation Snapshot (Already Done)

Implemented in `pipeline-runtime`:

- C1-C8 buffer taxonomy and blocking stage-thread scheduler.
- YAML-driven topology build (no mock topology).
- Ring/shared/isolate pair construction.
- Model-address resolution from `model.bin` + `model_offset` (with `addr_base` fallback).
- Runtime allocator default page size is now 1KB (configurable by `--spm-page-bytes`).
- Shared scratchpad DMA now uses software-side page-granular translation helpers in runtime (`prt_dma_copy_*_spm_pages`).
- This part is scheduled to migrate to dual paging:
  - shared-spad runtime page size 1KB and software-side per-page DMA translate/submit;
  - Gemmini load/store DMA front-end dual-path translation for compute accesses.
- Conv execution path wired to `tiled_conv_auto` when stage has conv descriptor.
- Runtime CLI supports `--num-cores`, `--pages-per-acc`, `--model-offset`, `--input`, `--golden`.
- Strict e2e path implemented: input mapping before run + golden compare after run.
- Model YAML parser fixed for layer entries where `address/address2` appear before `index`.
- Batch-complete stop implemented from sink subbatch progress (`--batch` aware); watchdog is deadlock guard.
- Multi-segment execution is enabled via segment-by-segment run (runtime swizzles each segment into a single-segment topology per pass).
- Page allocation now supports preferred-bank placement; shared tensor placement preference is driven by `shared_tensor_is_read_first`.
- C4 shared tensors now reuse canonical per-slot physical page layouts across producer/consumer pipebufs within a segment.
- Topology-lifetime page allocations are reclaimed on topology release, so segment-by-segment execution does not accumulate page-table occupancy.
- `gemmini_mode=async_experimental` now issues Gemmini work without immediate fence.
- Stage worker now performs C1/C5 double-buffer prefetch on `no_use_idx` during the async compute window, then fences at the compute dependency boundary before export publication.
- C2/C6 export path now supports submit-ahead DMA with delayed completion:
  - `poll_progress_thread` mode submits DMA without immediate wait.
  - completion is retired at dependency boundaries via nonblocking progress polling.
  - `subbatch_offset` advances only on completion (not on submit) to preserve correctness.
- Stage worker now pins itself to a deterministic CPU (Linux, selected from current allowed cpuset) based on `stage_acc_id`, to preserve tile-local RoCC control ownership.

Validated baseline:

- `bertmini` flow runs with `RC=0` using existing dummy artifacts.

## 3. MudnacSim Strategy Alignment (Key Decision for Q6)

After checking MudnacSim code (`tmp/MudnacSim/src/runtime/pipeline.cpp`, `tmp/MudnacSim/src/runtime/runtime.cpp`):

- Physical SPM pages are allocated once per pipeline action/segment (`AllocSpm*`), not reallocated before each layer.
- Page-table mappings are established per command and invalidated after command completion.
- Shared tensors reuse the same page sets across producer/consumer stages.
- Ring buffer pages are pre-allocated at pipeline setup, then reused by head/tail offset movement.
- C1-C8 buffer monitor behavior is driven by pipeline tick state transitions in `pipeline.cpp`; Chipyard runtime must preserve those transition semantics when porting/optimizing.

Mandatory reference mapping (must be checked before changing corresponding runtime modules):

- `pipeline.cpp`: C1-C8 pipeline-buffer monitor/tick state machine and synchronization ordering.
- `pipeline_stage.cpp`: stage execution sequencing and stage-level dependency timing.
- `pipeline_buffer.cpp`: buffer status fields, transitions, and ring/shared/isolate interaction details.
- `spm_manager.cpp`: SPM page allocation strategy behavior (all-bank and placement policy behavior).
- `runtime.cpp`:
  - `AllocSpmAllBankPageInter`: all-bank interleaved SPM allocation baseline.
  - `PipelineRuntime` methods: lifecycle orchestration and pipeline scheduling behavior.

Therefore Chipyard runtime will follow the same behavior:

- **Physical page allocation granularity**: per runtime action/segment.
- **Virtual mapping granularity**: per DMA/send/fetch/flush command.

## 4. Architecture Decisions for Chipyard Runtime

### 4.1 Execution and Concurrency Model

- Keep per-stage pthread workers.
- Add CPU affinity policy so each stage worker is pinned to one Linux CPU/hart.
- Maintain stage-to-core mapping table (static for V1 correctness, expandable later).
- Each CPU issues commands to its local Gemmini + DMA path.

### 4.2 DMA/Gemmini Control Policy

- V1 correctness mode default: blocking fence (`dma_backend=blocking_fence`, `gemmini_mode=blocking_fence`).
- Runtime must still preserve inter-stage overlap through multi-thread, multi-core pipeline execution.
- Mandatory next milestone: command overlap implementation (submit ahead + delayed wait/fence with dependency checks).
- Current status:
  - `poll_progress_thread` DMA backend has a functional progress-thread completion path.
  - `async_experimental` Gemmini mode now supports issue-without-immediate-fence.
  - stage worker performs overlap prefetch for C1/C5 double-buffer inputs while Gemmini compute is in-flight, then fences before export visibility.
  - C2/C6 submit-ahead overlap is implemented with ordered completion retirement and topology-teardown drain.
  - overlap trace metrics are implemented via `--trace` (`key=value`):
    - DMA/Gemmini activity totals and overlap estimates
    - submit-ahead/retire counters for C2/C6
    - prefetch attempt/success counters for C1/C5 compute-window prefetch path
    - cycle calibration anchors (`trace_cycle_overhead`, `trace_cycle_ref`, `trace_ns_ref`)
    - per-event timeline records (`event_<idx>=stage,kind,cycle,ns,...`) for FPGA-side correlation
  - remaining P6 work: target-side overlap validation on FPGA and metric calibration against hardware timeline.

### 4.3 Operator Support

- `conv`: `tiled_conv_auto` path.
- `resadd`: add runtime descriptor and call `tiled_resadd_auto` (Gemmini path), with CPU fallback only for unsupported corner cases.

### 4.4 Model Input/Output Binding

- Introduce deterministic I/O ID generation equivalent to `Model.get_model_in_out_ids()`:
  - model inputs: input tensor ids not produced by any layer output.
  - model outputs: output tensor ids not consumed by any later input/weight.
- Add helper script under `pipeline-runtime` to derive and save `{input_ids, output_ids}` from `layers.yaml`.
- Runtime loads this metadata and binds `--input` to input ids, `--golden` to output ids.

## 5. Final Execution Plan

### Phase P0: Baseline Freeze and Instrumentation

Scope:

- Freeze current runnable baseline and capture deterministic logs.
- Add trace points for stage/core assignment, DMA submit/wait, Gemmini begin/end.

Deliverables:

- Updated `IMPLEMENTATION_STATUS.md` snapshot and trace fields.

Exit criteria:

- Baseline command remains `RC=0` with trace enabled.

### Phase P1: HybridMapper-Compatible Model I/O Metadata [Done 2026-02-28]

Scope:

- Add `pipeline-runtime/scripts/extract_model_io_ids.py`.
- Implement logic equivalent to HybridMapper `get_model_in_out_ids`.
- Output JSON (example: `model_io_ids.json`).

Deliverables:

- Script + README usage.
- Validation by comparing script output with HybridMapper model outputs for at least `bertmini`.

Exit criteria:

- Runtime can parse I/O metadata without manual tensor id configuration.

### Phase P2: Strict End-to-End Input/Golden Compare [Done 2026-02-28]

Scope:

- In runtime:
  - load `--input` and map to model input tensor addresses.
  - run pipeline.
  - read model output tensors and compare with `--golden`.
- Print mismatch summary (`count`, `first index`, `max abs diff`, tensor id).
- Return non-zero on mismatch.

Deliverables:

- Changes in `src/prt_runtime.c`, `src/main.c`, and related headers.
- `README.md` and `TESTPLAN.md` update.

Exit criteria:

- Correct golden returns `RC=0`; tampered golden returns non-zero with mismatch report.

### Phase P3: Completion Semantics from Batch Progress (Replace Watchdog-Only Exit) [Done 2026-02-28]

Scope:

- Use `--batch` and sink tensor/subbatch progress to determine normal termination.
- Keep watchdog as deadlock guard only.

Deliverables:

- Runtime termination state machine updated:
  - normal exit when sink progress reaches target subbatch
  - watchdog timeout now returns error (`PRT_ERR_TIMEOUT`)
  - sink resolution policy:
    - last segment uses strict model-output sinks
    - intermediate segments use terminal-export sinks in current topology

Exit criteria:

- Runtime ends on completion without watchdog timeout in normal runs.
- Timeout case returns non-zero instead of success.

### Phase P4: Page Management Parity with MudnacSim

Scope:

- Ensure full parity on:
  - all-bank interleave and hilbert preference.
  - shared tensor page reuse.
  - ring min/max page policy by tensor type.
  - command-lifetime mapping/unmapping.
- Add assertions for page leaks and stale mappings.

Deliverables:

- Strengthened page manager and debug checks.

Exit criteria:

- No page leak in 100-batch stress; all assertions pass.

### Phase P5: Conv + ResAdd Full Runtime Coverage

Scope:

- Build per-stage task descriptor for `conv` and `resadd`.
- Route by `layer.type` to Gemmini adapter API.

Deliverables:

- `prt_gemmini_adapter` operator dispatch extension.

Exit criteria:

- Full `layers.yaml` executes without unsupported-op abort.

Current status:

- Core dispatch path is implemented:
  - `conv` descriptor + `tiled_conv_auto`
  - `resadd` descriptor + `tiled_resadd_stride_auto`
- Remaining work under P5 is end-to-end validation on target RISC-V/FPGA runs and edge-case parameter coverage.

### Phase P6: Mandatory DMA/Gemmini Overlap [In Progress: 2026-03-01]

Scope:

- Implement overlap while preserving correctness:
  - per-core in-flight DMA tokens.
  - delayed fence/wait at dependency boundaries.
  - ring/pipebuf readiness gates stay authoritative.
- Keep blocking mode as safe fallback knob.

Deliverables:

- New overlap mode (`poll_progress_thread` path becomes functional).
- Async compute boundary mode (`gemmini_mode=async_experimental`) with stage-local entry prefetch overlap.
- Overlap metrics in trace (DMA active cycles vs Gemmini active cycles).

Exit criteria:

- Verified concurrent DMA and compute windows in trace on multi-core runs.
- Verified correctness under both:
  - `blocking_fence + blocking_fence`
  - `poll_progress_thread + async_experimental`

### Phase P7: FPGA/Linux Integration Hardening

Scope:

- Validate on Buildroot Linux runtime constraints (basic libc + pthread).
- Affinity and core mapping robustness under target kernel.
- Robust error handling for missing files/invalid yaml/oom.

Deliverables:

- Integration checklist and troubleshooting guide.

Exit criteria:

- Stable repeated runs on target SoC with correctness preserved.

## 6. Test and Acceptance Matrix

Required gates:

1. C1-C8 category tests (TB1-TB9).
2. Strict e2e compare pass/fail behavior.
3. Conv+resadd mixed model correctness.
4. 100-batch no-deadlock/no-leak.
5. Overlap verification traces (mandatory before performance phase close).

## 7. Open Risks and Mitigations

1. Core-affinity mismatch on Linux target:
   - Mitigation: optional affinity disable flag + runtime core map print.
2. YAML schema drift:
   - Mitigation: add strict schema checks and explicit parser errors.
3. Overlap hazards (ordering/race):
   - Mitigation: dependency fence points + deterministic stress tests.

## 8. Immediate Next Coding Order

1. P4 remaining hardening:
   - page-leak/stale-allocation assertions in stress runs
   - shared/ring placement parity checks against MudnacSim traces.
2. P5 remaining validation:
   - `conv` + `resadd` parameter-coverage runs on target RISC-V/FPGA.
3. P6 remaining overlap work:
   - C2/C6 submit-ahead and delayed completion handling.
   - overlap trace metrics and target-side measurement.
4. Target-side profiling and regression hardening on FPGA SoC.
