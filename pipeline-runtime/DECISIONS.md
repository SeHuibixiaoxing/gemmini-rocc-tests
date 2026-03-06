# Pipeline Runtime Decisions (V1)

## Locked Decisions

1. Runtime model: Linux user-space C runtime with pthread.
2. Scope: static pipeline only; no QoS dynamic scheduling in V1.
3. Input format: parse HybridMapper YAML directly on target. Current implementation uses an in-repo lightweight parser; `libyaml` can be swapped in later.
4. Core count: configurable via CLI `--num-cores` (default 4, capped by `PRT_MAX_CORES`).
5. Runtime shared-spad page size defaults to 1KB (`--spm-page-bytes` override); split paging remains fixed as DRAM 4KB (OS/MMU) + shared-spad 1KB (runtime).
6. Operator coverage: `conv` + `resadd` runtime dispatch paths in V1 correctness phase.
7. Correctness policy: INT8/INT16 exact match, FP with tolerances (`atol=1e-3`, `rtol=1e-3`).
8. DMA exchange policy: point-to-point by producer/consumer mapping.
9. Buffer compatibility target: preserve 8-category pipeline-buffer behavior from MudnacSim runtime.
10. Model tensor addresses are interpreted as offsets from model base and resolved with `--model-bin` + optional `--model-offset`; `addr_base` subtraction remains as a compatibility fallback.
11. Runtime page budget is configurable via `--pages-per-acc` to support larger mappings (e.g. 8-core bertmini pipelines).
12. Pipeline parser accepts stage records with `accUtil` appearing before `globalStageId` to match HybridMapper `entire_model` YAML ordering.
13. Page allocation honors preferred accelerator banks when provided; strategy is preferred-first with all-bank fallback.
14. Shared tensor placement preference follows `shared_tensor_is_read_first` metadata (`1`: prefer entry-owner stages, `0`: prefer export-owner stages).
15. Shared C4 buffers with the same `(segment, tensor)` reuse one canonical physical page layout per slot; each pipebuf holds a cloned descriptor list to avoid double-free while preserving ppn identity.
16. Topology page allocations are treated as segment-lifetime resources and are explicitly released on topology teardown to avoid cross-segment allocator pressure.
17. Runtime performs allocator-idle checks after topology teardown (`tensor_alloc_count` and `page_used` bitmap) and treats non-empty state as runtime error.
18. Final-segment completion uses strict model-output sinks; terminal-export sinks are only used for intermediate segments.
19. Shared C4 topology construction must pass slot-wise ppn aliasing validation across all same `(segment,tensor)` pipebuf instances; mismatch is treated as runtime error.
20. `poll_progress_thread` is no longer a placeholder: DMA progress thread owns pending-token completion and timeout-aware waits; `blocking_fence` remains default safety mode.
21. `gemmini_mode=async_experimental` semantics are fixed to "issue compute without immediate fence"; stage worker performs C1/C5 `no_use` prefetch during the compute window and fences at the export-visibility dependency boundary.
22. In `GemminiLearningConfigSpadNoC`, RoCC control is tile-local: a CPU hart can only issue Gemmini/DMA commands to accelerators attached to its own tile; it cannot directly target another tile's Gemmini/DMA instance.
23. Tile-local DMA is allowed to access any globally mapped shared-scratchpad address (including remote tiles' shared spad windows), so cross-tile data movement is implemented via global addressing rather than remote command dispatch.
24. Runtime thread scheduling must preserve CPU-to-tile ownership assumptions (bind stage threads to fixed CPUs/harts); otherwise command routing may drift due to OS migration.
25. Although the SoC config may include an SBUS scratchpad node, pipeline runtime data movement must not depend on it; runtime uses only Gemmini-internal shared scratchpad windows for inter-stage/inter-tile exchange.
26. In overlap mode (`poll_progress_thread`), C2/C6 export DMA uses submit-ahead with delayed completion retirement; `subbatch_offset` only advances on completion, and retirement is enforced in submitted subbatch order.
27. Runtime overlap observability is emitted through `--trace` as deterministic `key=value` text; metrics are software-observed counters/interval estimates and must be calibrated against FPGA hardware timeline before performance conclusions.
28. FPGA calibration prework uses cycle/ns dual timestamps plus per-event timeline dumping (`event_<idx>`); cycle-read overhead is explicitly measured and exported as `trace_cycle_overhead`.
29. ReRoCC + CoupledDMA target track is active; per-segment resources are managed by a `ScheduleAction` lifecycle object in runtime.
30. Stage manager ownership is derived from action allocation, not CPU tile affinity; runtime binds Gemmini and DMA manager IDs separately per stage.
31. `stage.accUtil` is mandatory in pipeline YAML and constrained to `{1,2,4,8,16,32}`; invalid values are parse/runtime errors.
32. Runtime config now includes explicit manager topology fields:
    - `num_gemmini_mgrs`
    - `num_dma_mgrs`
    - `gemmini_mgr_base_id`
    - `dma_mgr_base_id`
    - `sync_mode`
33. Runtime default mode is async (`poll_progress_thread` + `async_experimental`); `sync_mode=blocking_debug` forces blocking backends.
34. Gemmini async submit path is fixed to "issue without immediate ReRoCC fence"; dependency-boundary fence is manager-scoped (`rr_fence` per manager used by stage task).
35. ResAdd async path uses a runtime-local no-fence kernel loop (built from `sp_tiled_resadd`) so fence is controlled by runtime dependency boundaries.
36. Conv split policy is output-centric and deterministic:
    - prefer OC split when `out_channels >= tile_count`
    - otherwise use generic 2D spatial rectangle split with halo repack
    - if `tile_count > out_row_dim * out_col_dim`, return split error (no silent downgrade).
37. ResAdd split policy is output-domain 2D rectangle split on `(I, J)`; if `tile_count > I * J`, return split error.
38. DRAM paging and shared-scratchpad paging are explicitly two independent systems:
    - DRAM: OS/MMU page table (4KB).
    - shared-scratchpad: runtime-managed page table (default 1KB).
39. Dedicated DMA for shared-spad data movement uses software-side translation and per-page command submission based on runtime SPM page table.
40. Gemmini compute-related load/store DMA uses front-end dual-path translation:
    - DRAM accesses follow existing TLB translation path.
    - shared-spad accesses follow dedicated SPM translation path.
41. Mudnac residency constraint is locked: during compute, a tensor is either fully in shared-spad or fully in DRAM (no mixed placement).
42. Hardware validation gates are strict and ordered:
    - metasim baremetal -> FPGA baremetal -> FPGA Linux.
    - software-side e2e validation starts only after all three hardware gates pass.
43. Runtime now exposes SPM page-table observability:
    - `spm_ptbr_pa`, `spm_pte_count`, `spm_fault_count`, `spm_last_fault_vaddr`, `spm_last_fault_cause`.
44. Runtime CLI now carries SPM translation controls:
    - `--spm-page-bytes`, `--spm-xlate-enable`, `--spm-xlate-range-base`, `--spm-xlate-range-size`, `--hw-validate-only`.
45. Current async policy caveat: while SPM page-DMA retire is synchronous per-page, runtime forces blocking-debug mode when `spm_xlate_enable=1` to preserve correctness.
46. Hardware gate script defaults to 5 consecutive workload passes per gate (`GATE_RUNS=5`) before allowing transition to the next gate.
47. Quick coupled-DMA hardware workloads default to cross-page payload (`--bytes 65536`) for both baremetal and Linux matrix verification profiles.
48. `pipeline_runtime --hw-validate-only` is a first-class init/preflight mode and does not require model/pipeline YAML inputs.

## Non-Goals in V1

1. Full operator coverage.
2. Adaptive core count at runtime.
3. Dynamic QoS scheduler parity.
4. Performance SLA as hard release gate.
