# Implementation Status

## Completed in this change

0. ReRoCC + CoupledDMA migration baseline (2026-03-05)
- Added `ScheduleAction` resource lifecycle module:
  - `include/prt_schedule_action.h`
  - `src/prt_schedule_action.c`
- Added ReRoCC manager scope helpers:
  - `include/prt_rerocc.h`
  - `src/prt_rerocc.c`
- Runtime segment loop now does:
  - `action_generate -> action_alloc_acc -> action_alloc_spm -> build_topology -> action_bind_topology -> execute -> action_release`
- Action-scoped allocation keys are tracked for SPM ownership/release.
- Added stage manager metadata in runtime:
  - primary gemmini manager id
  - primary dma manager id
  - stage tile count
  - stage manager list
- Added strict parser enforcement that every stage must provide `accUtil`.
- Added new CLI/runtime config fields:
  - `--num-gemmini-mgrs`
  - `--num-dma-mgrs`
  - `--gemmini-base-id`
  - `--dma-base-id`
  - `--sync-mode async|blocking_debug`
- Switched default CLI backend to async mode:
  - DMA: `poll_progress_thread`
  - Gemmini: `async_experimental`
- DMA submit path now acquires/releases ReRoCC scope on target DMA manager (RISC-V path).
- Gemmini adapter now uses manager-aware execution and stage tile count:
  - conv: OC split preferred, spatial split fallback
  - resadd: row split
- Host regression smoke revalidated with bertmini dummy artifacts:
  - build: `make -C pipeline-runtime -j4` pass
  - blocking debug run with input/golden pass (`RC=0`)
  - async default run with input/golden pass (`RC=0`)

1. Runtime framework
- Added `prt_runtime_init/run/destroy`.
- Added CLI binary `pipeline_runtime`.

2. Blocking synchronization foundation
- Added `pipebuf` and `ringbuf` lock/cond synchronization.
- Added blocking waits: `prt_pipebuf_wait_full/empty`, `prt_ring_wait_ready/idle`.

3. DMA/Gemmini pluggable backends
- Added submit/wait API and backend op tables.
- Default mode is blocking fence.
- Added placeholders for progress-thread and async Gemmini modes.

4. Page management
- Added per-page allocation/release with all-bank interleave.
- Added Hilbert order preference for 4-core setup.

5. C1-C8 execution entrypoints
- Implemented scheduler functions for C1-C8 with blocking predicates.
- Implemented fanout counter handling skeleton for C3.

6. Real pipeline topology build from YAML (replaced mock)
- Removed `seed_mock_topology`.
- `prt_runtime` now builds `pipebuf/ring/pair` graph from parsed `segments/stages`:
  - C1/C2 from `DRAM|DRAM_DEPEN`
  - C3/C5/C6 from `ISOLATE_SPM` + ring config
  - C4 from `SHARED_SPM`
  - C7/C8 from `ALL_RINGBUFFER`
- Built isolate/shared pair links by `tensor_id` across stage export->entry.

7. Page sizing connected to `tensor_spm_util_*`
- YAML loader now parses:
  - `tensor_spm_util_in_stage`
  - `tensor_spm_util_shared`
  - `tensor_spm_util_in_ringbuffer`
- Runtime page allocation now uses parsed util:
  - stage buffer slot pages from `tensor_spm_util_in_stage` (split across double-buffer slots)
  - shared fallback from `tensor_spm_util_shared`
- ring slot pages from `ring_buffer_size_per` or fallback by `tensor_spm_util_in_ringbuffer / ring_buffer_count`

8. Hardware-path alignment updates
- DMA blocking backend now uses per-request completion flag storage in token (no single shared static flag).
- Pipebuf page address now maps to shared-spad global address space (`0x40000000 + ppn * 4KB`) for DMA requests.
- Gemmini adapter now supports optional real conv execution via `prt_gemmini_conv_desc_t` + `tiled_conv_auto` when `task->opaque_task` is provided; fallback remains fence-only.
- Pipeline parser now captures `layerIdList` (first id) into stage metadata for stage->layer binding.

9. Relative model-offset integration
- Model YAML parser now captures per-layer `index/type/param/tensorIds/address/address2` plus top-level address range.
- CLI now supports `--model-bin` and `--model-offset`; runtime resolves tensor addresses as model-relative offsets (`ptr = model_blob + model_offset + address`) with `address - addr_base` fallback for compatibility.
- Topology builder now maps DRAM pipebuf addresses from model layer tensor offsets when available (fallback to synthetic addresses).
- Stage worker now passes optional stage conv descriptor to Gemmini backend when stage->layer bind can be resolved.

10. Core/page configurability and real-YAML parser robustness
- `PRT_MAX_CORES` increased to 32.
- CLI now supports:
  - `--num-cores`
  - `--pages-per-acc`
- Pipeline YAML parser now supports stage blocks where `accUtil` appears before `globalStageId` (common in HybridMapper `entire_model/*.yaml`).

11. Cross-repo dummy runtime data + pseudo CPU golden workflow
- Added script:
  - `/home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py`
- Script outputs:
  - `dummy_weight/model.bin`
  - `dummy_input/input.bin`
  - `dummy_input/golden/golden.bin`
  - `dummy_weight/manifest.json`
- Verified end-to-end runtime smoke with:
  - model: `bertmini`
  - pipeline: `entire_model/8_256_16_19_64_ours2.yaml`
  - config: `--num-cores 8 --pages-per-acc 4096 --model-offset 0`
  - result: `RC:0`

12. Model I/O ID extraction utility + architecture plan refresh
- Added script:
  - `scripts/extract_model_io_ids.py`
- Script extracts `input_ids`/`output_ids` from `layers.yaml` using semantics aligned to HybridMapper `Model.get_model_in_out_ids()`.
- Updated architecture doc with locked decisions:
  - conv+resadd scope
  - correctness-first milestones
  - MudnacSim-aligned page strategy (allocate per action/segment, map/unmap per command)
  - mandatory DMA/Gemmini overlap as a post-correctness milestone.

13. Strict runtime e2e compare and model parser ordering fix
- Runtime now supports strict compare path:
  - loads `--input` blob and maps it to model input tensor IDs before execution
  - loads `--golden` blob and compares model output tensor data after execution
  - prints per-tensor mismatch + summary and returns `PRT_ERR_MISMATCH` on mismatch
- Model YAML parser now handles layer records where `address`/`address2` appear before `index` in each list item (HybridMapper `layers.yaml` ordering).
- Verified with bertmini:
  - `--input + --golden` returns `RC:0` for matching golden
  - 1-byte-tampered golden returns non-zero with mismatch summary.

14. Batch-complete termination semantics (P3)
- Runtime stop condition now uses sink subbatch progress and `--batch`, not watchdog-only exit.
- Watchdog timeout is now deadlock guard and returns `PRT_ERR_TIMEOUT` (non-zero).
- Sink resolution policy:
  - last segment: sinks must match model output tensor IDs
  - intermediate segments: use terminal export sinks in current topology
- Verified with bertmini:
  - `--batch 1` + input/golden returns `RC:0` without timeout exit
  - huge batch + tiny watchdog returns timeout non-zero
  - mismatch path still returns `PRT_ERR_MISMATCH`.

15. ResAdd operator dispatch in runtime/Gemmini adapter (P5 core path)
- Added stage op kind (`NONE/CONV/RESADD`) and per-stage descriptor selection.
- Runtime now builds `resadd` descriptors from `layers.yaml` stage-layer binding and model addresses.
- Gemmini adapter now dispatches:
  - `conv` -> `tiled_conv_auto`
  - `resadd` -> `tiled_resadd_stride_auto`
- Host/non-RISC-V path remains API-compatible fence/no-op (same as existing conv behavior on host).

16. P4 partial: page preference parity + buffer monitor deadlock fix
- Fixed stage buffer role split:
  - stage worker now collects only `is_entry` pipebufs into entry wait set.
  - stage worker now collects only non-entry pipebufs into export readiness set.
  - this removes export-buffer-as-entry deadlock risk for C4/C7 paths.
- `shared_tensor_is_read_first` is now parsed and carried in segment metadata.
- Page allocator now honors `preferred_accs` (previously ignored):
  - allocates on preferred banks first.
  - falls back to non-preferred banks in distance-sorted order.
- Topology builder now passes preferred acc sets to page allocation:
  - stage-local buffers prefer their stage acc.
  - shared buffers derive preference from `shared_tensor_is_read_first` (entry-first vs export-first owners).
  - ring buffers prefer union of owner-stage accs (entry+export).
- Shared SPM page reuse is now explicit:
  - for each `(segment_idx, tensor_id)` in C4, runtime builds one canonical page-set per buffer slot.
  - all producer/consumer C4 pipebufs clone the same canonical page descriptors (same physical ppn layout).
- Topology-lifetime page keys are now tracked and released in `runtime_release_topology()`:
  - per-slot allocation keys are recorded when pages are allocated.
  - segment-to-segment swizzle runs now reclaim page-table entries/page_used bits between segments.
- Runtime now asserts allocator-idle invariants after topology release:
  - `tensor_alloc_count == 0`
  - `page_used` bitmap has zero live pages
- Runtime now validates shared aliasing invariant at topology build:
  - for each shared C4 tensor in the same segment, slot-wise ppn layout must match across all producer/consumer pipebufs.

17. P6 partial: poll-progress DMA backend is now functional
- `poll_progress_thread` now starts a real progress thread and tracks pending DMA tokens in a protected queue.
- `dma_poll_submit` enqueues tokens; progress thread marks completion and signals waiters.
- `dma_poll_wait` supports timeout and queue removal.
- `dma_poll_submit_and_wait` uses managed token lifetime for safe async completion tracking.
- Verified on host flow:
  - normal run (`--dma-backend poll_progress_thread`) returns `RC=0`
  - tampered golden returns mismatch (`PRT_ERR_MISMATCH`)
  - tiny-watchdog run returns timeout (`PRT_ERR_TIMEOUT`)
- Regression checks:
  - normal `--input + --golden --batch 1` run returns `RC=0`
  - tampered golden returns mismatch (`PRT_ERR_MISMATCH`)
  - tiny watchdog + huge batch returns timeout (`PRT_ERR_TIMEOUT`)

18. P6 partial: async Gemmini issue + in-compute C1/C5 prefetch overlap
- `gemmini_mode=async_experimental` now issues Gemmini work without implicit immediate fence.
- Stage worker now executes C1/C5 prefetch into double-buffer `no_use_idx` during async compute window.
- Dependency boundary is explicit: runtime calls `prt_gemm_fence` before publishing export buffers.
- Regression checks:
  - `blocking_fence + blocking_fence` path remains `RC=0` on normal run.
  - `poll_progress_thread + async_experimental` returns `RC=0` on 10-batch run.
  - mismatch/timeout behaviors are preserved.

19. Tile-local ownership hardening: stage-thread affinity pinning
- Stage worker binds itself to a deterministic CPU chosen from the current allowed cpuset using stage owner index (`stage_acc_id`) as selector.
- This preserves the `CPU/hart -> tile-local Gemmini/DMA` control assumption under Linux scheduling.
- Regression checks:
  - baseline correctness run remains `RC=0`.
  - `poll_progress_thread + async_experimental` run remains `RC=0`.

20. P6 progress: C2/C6 submit-ahead overlap and delayed completion retirement
- Added DMA token lifecycle APIs:
  - `prt_dma_try_wait()`
  - `prt_dma_token_cleanup()`
- Added export DMA progression path:
  - `prt_progress_export_dma()` retires C2/C6 in-flight DMA in subbatch order.
  - completion updates buffer state (`full=0`, `subbatch_offset++`) only after DMA completes.
- C2/C6 in `poll_progress_thread` mode now:
  - submit DMA without immediate wait.
  - store per-slot in-flight token + submitted subbatch id.
  - rotate double-buffer immediately after submit to enable next-iteration overlap.
- Stage dependency gate (`stage_wait_exports_ready`) now actively polls export DMA completion for C2/C6 instead of pure blocking empty-wait.
- Topology teardown now drains pending C2/C6 DMA completions before page release, then destroys pipebuf token objects safely.
- Regression checks:
  - baseline (`blocking_fence`) `RC=0`
  - mismatch path returns non-zero
  - timeout path returns non-zero
  - overlap path (`poll_progress_thread + async_experimental`, batch 10) `RC=0`

21. P6 progress: overlap metrics and trace output
- `--trace <path>` now writes runtime metrics in `key=value` form.
- Added runtime counters for:
  - DMA submit/complete/inflight-peak/busy-ns
  - Gemmini issue/fence/busy-ns
  - overlap hooks (`prefetch_*`, `export_submit_ahead_count`, `export_retire_count`)
- Added derived overlap estimates:
  - `overlap_est_ns`
  - `dma_util_pct`, `gemm_util_pct`, `overlap_est_pct`
- Verified trace file generation in both:
  - blocking baseline (`--trace /tmp/prt_trace_blocking.txt`)
  - async overlap path (`poll_progress_thread + async_experimental`, `--trace /tmp/prt_trace_async.txt`)

22. FPGA calibration prework: cycle/event timeline instrumentation
- Added cycle timestamp API (`rdcycle` on RISC-V, monotonic fallback on host).
- Added cycle-read overhead calibration (`trace_cycle_overhead`).
- Added per-run cycle/ns anchor fields (`trace_cycle_ref`, `trace_ns_ref`).
- Added event timeline dump to trace:
  - `event_format=idx,stage,kind,cycle,ns,aux0,aux1`
  - event kinds include run/dma/gemm/export submit-ahead/retire.
- Added bounded event buffer with drop accounting:
  - `trace_event_count`
  - `trace_event_drop_count`
- Regression checks remain green (`RC1=0`, `RC2!=0`, `RC3!=0`, `RC4=0`).

23. ReRoCC+CoupledDMA hardening (2026-03-05): ScheduleAction views + true dependency-boundary fencing
- `ScheduleAction` SPM source now captures observable page views:
  - flattened `in_stage_pages[stage][tensor][slot]`
  - flattened `ring_pages[tensor][slot]`
  - tracked alloc-keys + total page count
- Gemmini adapter now uses no-immediate-fence issue semantics:
  - issue path acquires manager scope, submits conv/resadd work, then releases scope without fence
  - dependency-boundary fence executes `rr_fence` per manager used by current stage task
- Added runtime-local no-fence resadd issue kernel path (built on `sp_tiled_resadd`) to avoid per-issue fence inside `tiled_resadd_stride_auto`.
- Multi-core split policy upgraded to output-centric generic 2D partition:
  - conv: OC split first, then rectangle spatial split with halo repack
  - resadd: rectangle split on `(I, J)` domain
  - cannot split cases now return explicit error instead of silent fallback.
- Host smoke revalidated:
  - async mode run with manager CLI args returns `RC=0`
  - `--sync-mode blocking_debug` run returns `RC=0`

24. Translation architecture freeze (2026-03-05): dual paging split
- Requirement freeze:
  - DRAM paging uses OS/MMU page table (4KB).
  - shared-scratchpad paging uses runtime-managed page table (default 1KB).
- Dedicated spad-move DMA path:
  - translation is software-side and submission is per shared-spad page.
- Gemmini compute load/store path:
  - translation is done in Gemmini load/store DMA front-end with dual path:
    - DRAM-TLB path
    - shared-spad pager path
- Status:
  - architecture decision is locked;
  - code migration is pending (current runtime allocator/mapping still uses provisional 4KB constant).

25. SPM page-table + paged DMA integration (2026-03-05, this session)
- Runtime page size default switched to 1KB (`PRT_PAGE_SIZE_BYTES=1024`), configurable by `--spm-page-bytes`.
- Implemented runtime SPM page-table APIs in `prt_page_table`:
  - `prt_spm_map_tensor()`
  - `prt_spm_unmap_tensor()`
  - `prt_spm_translate_range()`
  - `prt_spm_ptbr_pa()/pte_count()/fault_*()`
- `ScheduleAction` now records SPM translation metadata:
  - `spm_ptbr_pa`
  - `spm_pte_count`
  - `spm_fault_count`
  - `spm_last_fault_vaddr`
  - `spm_last_fault_cause`
- Added paged DMA APIs in `prt_dma`:
  - `prt_dma_copy_spm_va()`
  - `prt_dma_copy_spm_pages()`
  - `prt_dma_copy_dram_to_spm_pages()`
  - `prt_dma_copy_spm_pages_to_dram()`
- Scheduler C1/C2/C3/C5/C6 now uses page-granular copy APIs for shared-spad movement.
- Added SPM translation CLI/config fields:
  - `--spm-page-bytes`
  - `--spm-xlate-enable`
  - `--spm-xlate-range-base`
  - `--spm-xlate-range-size`
  - `--hw-validate-only`
- Added Gemmini translation control interface surface:
  - `include/rerocc_gemmini_spm_xlate.h`
  - `prt_gemmini_spm_xlate_*` in `prt_rerocc` (currently stubbed unless `PRT_ENABLE_GEMMINI_SPM_XLATE_INSN` is enabled).
- Added hardware validation gate runner:
  - `sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
  - gate order: metasim baremetal -> FPGA baremetal -> FPGA Linux.
- Runtime guardrail:
  - while per-page DMA retire is synchronous, `spm_xlate_enable=1` currently forces blocking-debug backend selection for correctness.

26. Hardware validation harness hardening + cross-page Linux DMA updates (2026-03-05, this session)
- Gate runner now executes each hardware gate workload 5 consecutive times by default:
  - `sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
  - override with `GATE_RUNS=<n>`.
- `pipeline_runtime --hw-validate-only` now runs as a true init/preflight mode and returns `HW_VALIDATE_ONLY_PASS` on success.
- Linux coupled-DMA matrix test now supports payloads larger than Linux OS page size:
  - `rerocc-linux-tests-coupleddma/rerocc_dma_matrix_linux_coupleddma.c`
  - uses software page-walk (`virt_to_phys`) and per-page DMA submit/wait loop.
- Linux Gemmini matrix now checks `conv + resadd` in each manager case:
  - `rerocc-linux-tests/rerocc_gemmini_conv_matrix.c`.
- Cross-page payload defaults were raised in quick marshal profiles:
  - `marshal-config/rerocc_lc_linux_coupleddma_quick.json` -> `--bytes 65536`
  - `marshal-config/rerocc_lc_baremetal_coupleddma_quick.json` -> `--bytes 65536`

## Intentionally deferred (already represented in APIs)

1. Full YAML strict parser for HybridMapper schema.
2. Progress-thread backend with real hardware completion monitoring.
3. Target-side overlap validation and metric calibration for C2/C6 submit-ahead path.
4. End-to-end TB1-TB9 automated test harness.
5. Full tensor shape/stride-driven allocation and byte-accurate transfer sizing.

## Current defaults

- `sync_mode = async` requested by CLI, but currently coerced to `blocking_debug` when `spm_xlate_enable=1`
- `sync_mode = blocking_debug` forces blocking backends for debug
- `page_size default = 1KB` (`--spm-page-bytes` configurable)
- `shared-spad page size target = 1KB`
- `num_cores = 4` by default, configurable via CLI `--num-cores` (capped by `PRT_MAX_CORES`)
- `watchdog_timeout_ms = 5000`
