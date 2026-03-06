# Pipeline Runtime ReRoCC + CoupledDMA Plan

Date: 2026-03-05  
Target: `GemminiLearningConfigSpadReRoCCNoCCoupledDMAParametric`

## Validation Gate Order (Locked)

1. Baremetal on metasim (`config_runtime_rerocc_metasim_small_baremetal_globalnoc_coupleddma.yaml`)
2. Baremetal on FPGA (`config_runtime_rerocc_fpga_small_baremetal_globalnoc_coupleddma.yaml`)
3. Linux on FPGA (`config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml`)
4. Software-side pipeline-runtime validation after all hardware gates pass.

Automation entrypoint:
- `sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
- default enforces `5` consecutive `runworkload` passes per gate (`GATE_RUNS` env override).

## 2026-03-05 Implementation Delta (This Session)

- Added runtime SPM page-table interfaces:
  - `prt_spm_map_tensor`
  - `prt_spm_unmap_tensor`
  - `prt_spm_translate_range`
  - `prt_spm_ptbr_pa/pte_count/fault_*` queries
- Added page-granular DMA copy APIs:
  - `prt_dma_copy_spm_va`
  - `prt_dma_copy_spm_pages`
  - `prt_dma_copy_dram_to_spm_pages`
  - `prt_dma_copy_spm_pages_to_dram`
- Scheduler C1/C2/C3/C5/C6 now uses page-granular software translation paths for shared-spad movement.
- Added runtime/CLI knobs for SPM translation controls:
  - `--spm-page-bytes`
  - `--spm-xlate-enable`
  - `--spm-xlate-range-base`
  - `--spm-xlate-range-size`
  - `--hw-validate-only`
- Added `ScheduleAction` SPM translation metadata:
  - `spm_ptbr_pa`
  - `spm_pte_count`
  - `spm_fault_count`
  - `spm_last_fault_vaddr`
  - `spm_last_fault_cause`
- Added Gemmini SPM translation control interface stubs:
  - `include/rerocc_gemmini_spm_xlate.h`
  - `prt_gemmini_spm_xlate_*` APIs in `prt_rerocc`
- Temporary safety fallback:
  - when `spm_xlate_enable=1`, runtime currently forces blocking-debug mode until async token-chained page DMA retire is implemented.

## 2026-03-05 Additional Hardware-First Hardening

- Hardware-gate runner now executes each gate workload 5 times before moving to the next gate:
  - `sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh`
  - env knob: `GATE_RUNS` (default `5`).
- Linux coupled-DMA matrix test now supports multi-page payloads by software page-walk and per-page DMA submit:
  - `rerocc-linux-tests-coupleddma/rerocc_dma_matrix_linux_coupleddma.c`
- Linux Gemmini matrix now validates both `conv` and `resadd` per manager:
  - `rerocc-linux-tests/rerocc_gemmini_conv_matrix.c`
- Quick marshal profiles now default to cross-page DMA payload size:
  - `marshal-config/rerocc_lc_linux_coupleddma_quick.json` uses `--bytes 65536`
  - `marshal-config/rerocc_lc_baremetal_coupleddma_quick.json` uses `--bytes 65536`
- `--hw-validate-only` is now executable in `pipeline_runtime` (initialization-only preflight path).

## Goals

1. Introduce Mudnac-style `ScheduleAction` resource lifecycle:
   - `GenerateAction -> AllocAcc -> AllocSpm -> BindTopology -> ReleaseAction`
2. Route Gemmini/DMA commands through ReRoCC manager IDs.
3. Keep C1-C8 buffer monitor semantics and stage-thread scheduling.
4. Enforce stage tiling count from `stage.accUtil` with allowed set:
   - `1/2/4/8/16/32`
5. Default runtime mode is async (`poll_progress_thread + async_experimental`), with a blocking debug mode.

## Newly Frozen Translation Strategy (2026-03-05)

- Two paging systems are explicitly separated:
  - DRAM paging: OS/MMU page table (4KB).
  - shared-scratchpad paging: runtime-managed SPM page table (default 1KB).
- For dedicated DMA used for shared-spad data movement:
  - translation is done on the software side;
  - commands are issued page-by-page based on the runtime SPM page table.
- For Gemmini compute-related load/store DMA:
  - translation is done in Gemmini load/store DMA front-end (dual-path translation);
  - DRAM path keeps existing OS/TLB flow;
  - shared-spad path uses dedicated SPM translation path.
- Keep Mudnac tensor-residency rule:
  - each tensor is either fully in shared-spad or fully in DRAM (no mixed residency).

## Implemented in this change

- Added `ScheduleAction` framework:
  - `include/prt_schedule_action.h`
  - `src/prt_schedule_action.c`
- Added ReRoCC scope helpers:
  - `include/prt_rerocc.h`
  - `src/prt_rerocc.c`
- Runtime now executes each segment with an action lifecycle:
  - generate/alloc before topology build
  - bind after topology build
  - release after segment teardown
- Topology allocation keys are tracked at action scope.
- Strict stage `accUtil` validation is enabled in YAML parsing.
- Added runtime config and CLI for manager topology:
  - `--num-gemmini-mgrs`
  - `--num-dma-mgrs`
  - `--gemmini-base-id`
  - `--dma-base-id`
  - `--sync-mode async|blocking_debug`
- DMA submits now carry manager ownership from stage binding (`cmd_acc` is mapped to DMA manager ID).
- Gemmini adapter now uses ReRoCC manager scope and supports per-stage multi-tile execution paths:
  - conv: OC split preferred, generic 2D spatial split fallback (output-domain rectangles, halo-aware repack)
  - resadd: generic 2D output-domain split
- `ScheduleAction` SPM source now exports Mudnac-style observable views:
  - `in_stage_pages[stage][tensor][slot]` equivalent flattened view
  - `ring_pages[tensor][slot]` view
  - tracked alloc keys and total pages
- Gemmini async issue path now avoids per-issue ReRoCC fence:
  - issue is non-blocking at submit point
  - dependency-boundary fence is manager-scoped (`rr_fence` per manager used by the stage task)
- Added no-fence resadd kernel path inside runtime adapter (uses `sp_tiled_resadd` loop without internal `gemmini_fence`).

## Async policy (implemented behavior)

- Default mode:
  - DMA backend: `poll_progress_thread`
  - Gemmini mode: `async_experimental`
- Stage threads remain authoritative for C1-C8 readiness and dependency boundaries.
- DMA:
  - submit returns token
  - completion retired by wait/try_wait path
- Gemmini:
  - issue path runs under selected manager scope and releases scope without immediate fence
  - dependency boundary performs manager-scoped `rr_fence` + `gemmini_fence`
  - blocking mode = issue then immediate dependency-boundary fence

## Current known gaps

- Conv path still uses `tiled_conv_auto` (no explicit internal fence in current Gemmini stack, but final overlap behavior still needs FPGA-side confirmation).
- Current multi-tile execution is manager-aware and functional on host smoke; detailed FPGA overlap calibration is still pending.
- Command dispatcher thread-pool abstraction is partially represented by existing progress-thread + stage-thread model; dedicated dispatcher module remains to be split out.
- New translation strategy above is frozen but not fully wired in runtime/hardware integration yet:
  - runtime software-side SPM page-walk submission for dedicated spad DMA;
  - Gemmini DMA front-end dual-path translation for shared-spad page table.

## Validation commands (host smoke)

```bash
python3 /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py \
  --model bertmini \
  --pipeline-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml

make -C /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j4

/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --num-gemmini-mgrs 8 \
  --num-dma-mgrs 8 \
  --gemmini-base-id 0 \
  --dma-base-id 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 500
```

## Next steps

1. Split out a dedicated dispatcher module for Gemmini+DMA submission queues.
2. Implement software-side SPM page-walk + per-page submit path for dedicated spad DMA.
3. Implement Gemmini load/store DMA front-end dual-path translation (DRAM-TLB + shared-spad pager).
4. Run FPGA 2C2G Linux validation and calibrate overlap metrics against hardware timeline.
5. Add richer `ScheduleAction` JSON export (full page identity dump per view) for direct Mudnac trace-diff.
