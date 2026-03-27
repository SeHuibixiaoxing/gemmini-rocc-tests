# Next Session Prompt

```text
You are taking over pipeline-runtime / HybridMapper / FireSim FPGA debugging in:

/home/ubuntu/chipyard

Read these first, in order:

1. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/HANDOFF.md
2. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/STATUS.md
3. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/PLAN.md
4. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/LESSONS_LEARNED.md
5. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/PROCESS.md
6. /home/ubuntu/chipyard/AGENTS.md
7. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/TESTPLAN.md

Mission:

- Get `bertmini` passing end-to-end on Linux on FireSim F2.
- Temporarily do NOT start with multi-action validation.
- First bring back the previous `bertmini` single-action path on the refactored runtime.
- Keep the current single-layer-stage contract unless the existing mapping format clearly requires something else.
- Do not regress HybridMapper original behavior.
- Do not discard local uncommitted changes in this workspace.

Current status:

- 2026-03-26 latest static audit closed the current understanding of ReRoCC routing limits.
  - ReRoCC client hardware has:
    - up to `16` cfg slots
    - exactly `4` `rropc` route slots
  - but the current software stack emits:
    - all Gemmini instructions on `custom3`
    - DMA on `custom2`
  - therefore, on the current head:
    - one hart can time-multiplex across many Gemmini managers
    - but one hart has only `1` live Gemmini route lane without software/hardware changes
    - plus `1` DMA route lane
  - current runtime cfg selection is:
    - `cfg = ((stage_id * 2) + lane) % 16`
    - `lane=0` for DMA
    - `lane=1` for Gemmini
  - therefore one hart currently has only `8` non-conflicting stage-context pairs before cfg alias
  - do not conflate:
    - number of acquired managers
    - number of cfg contexts
    - number of simultaneously directly-issuable Gemmini lanes
- `rr_set_opc()` can be rebound before remote Gemmini execution finishes.
  - already-issued requests keep their own `cfg/client_id/manager_id`
  - `rr_release(cfg)` does not clear the `rropc` mapping
  - rebinding is only safe after you no longer need further issue/fence/fault ops on the old lane
- This static audit does NOT close the active Linux/F2 blocker.
  - the active blocker is still the guest runtime functional deadlock in the pointwise OS path
  - future-load scalability work must be kept separate from current hang triage

- The baremetal correctness gate is now fully green.
- The host closure gate is green again on the current head.
  - Revalidated in this session:
    - native `pipeline_runtime` build passes
    - `METHODS=ours2 BATCH=1 SKIP_BUILD=1 SKIP_EXPORT=1 WATCHDOG_MS=120000 ./scripts/run_bertmini_host_closure.sh`
      now returns `BERTMINI_HOST_CLOSURE_PASS`
    - RISC-V Linux `rerocc_pipeline_runtime-linux` cross-build passes again with:
      - `PIPELINE_RUNTIME_PROGRESS=1`
      - `PIPELINE_RUNTIME_PROGRESS_RAW=1`
      - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
      - `PIPELINE_RUNTIME_GEMMINI_PHASE=1`
      - `PIPELINE_RUNTIME_ONLY_MARKER=1`
    - the rebuilt guest binary contains:
      - `[gemmini-phase]`
      - `pointwise-inner ...`
      - `scope-drain ...`
      - `conv-sync ...`
      - `spm-pt pool init ...`
  - The host-only failure from the previous session is now understood:
    - old symptom:
      - `segment=3`
      - `action_alloc_spm failed rc=not_implemented(-8)`
    - root cause:
      non-`__riscv` PT chunk sizing was incorrectly limited to one host page,
      so `segmentSpmPageSpan=1289` needed a `12288B` PT slice which could never fit.
    - fix:
      PT chunk sizing now scales to at least one full action-local PT slice on host builds.
- A fresh FireMarshal install has already been completed after this rebuild:
  - log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-25--16-55-39-22LXE3XYE2TOQ013.log`
  - installed workload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json`
  - the overlay source binary was refreshed before install, so the next F2 replay should see the new
    `spm-pt pool init / scope-drain / pointwise-inner precall` markers
- The HybridMapper -> pipeline-runtime pre-orchestrated contract is now implemented.
  - segment metadata now includes:
    - `segmentSpmPageSpan`
    - `bufferBinding*List`
  - stage metadata now includes:
    - `execBaseVPage`
    - `localSpmTensorAddrList / localSpmFirstVPageList / localSpmPageCountList / localSpmTensorBytesList`
    - `entryBufferIdList / exportBufferIdList`
  - runtime now gives each pipeline segment/action its own dedicated alias VA window and vpage interval
  - runtime now gives each pipeline segment/action its own dedicated shared-spad page table / PTBR backing
  - runtime now allocates action page tables from a contiguous PT backing pool with HugeTLB-first policy
  - runtime only decides accelerator assignment, physical-page allocation, page-table binding, and xlate range setup
- The fresh exporter path for `bertmini` is fixed again.
- The decisive baremetal run is:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-24--12-47-02-rerocc-lc-baremetal-coupleddma-explicit-interleaved-small-f2-rerocc-baremetal-explicit-interleaved-small`
- Final guest result there is:
  - `ALL_TESTS_PASS`
  - `*** PASSED *** after 27505375802 cycles`
- Therefore baremetal is no longer the active blocker.
- Host runtime is also no longer the active blocker.
- Linux packaging / FireMarshal / FireSim F2 bring-up is no longer the only blocker.
- The current active blocker is now inside guest runtime execution on F2:
  - `segment 0`
  - `stage 0`
  - canonical pointwise fallback
  - OS pointwise path around bias `config_ld`
- The latest decisive F2 replay used:
  - runtime config:
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml`
  - manager log:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-26--05-00-12-runworkload-HJJRV1N3IIPJWISK.log`
  - results dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-26--05-00-12-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
- That run has already been terminated to avoid further F2 cost.
- Latest confirmed guest progress from `uartlog`:
  - `load-model-bin end elapsed_ms=769 size=17055744 offset=0`
  - `segment=0 workers-launched=1 sinks=1 target_subbatch=16`
  - `pointwise-inner stage=0 mgr=0 matmul-call-enter fallback=OS`
  - `matmul-os spaddrs-dc`
  - `matmul-os-biascfg-enter`
  - `matmul-os-biascfg-pre-ld`
  - there is still no confirmed `matmul-os-biascfg-post-ld`
- A direct Linux/F2 repair attempt already changed the bias path toward the reference implementation:
  - bias `config_ld` switched to `id=2`
  - bias move-in switched to `mvin3`
  - `D_stride` switched to `sizeof_D/low_D`
  - but the live boundary still did not move past `biascfg-pre-ld`
- Then there was no new marker for `90s+`, while `heartbeat.csv` kept increasing.
- Treat that as a functional deadlock in the current runtime/Gemmini path.
- This session landed deeper debug instrumentation specifically for the next replay:
  - `PRT_ENABLE_PROGRESS_HOT_LOG` is now wired through both native and Linux runtime builds
  - `host-init.sh` now verifies `[gemmini-phase]` strings are present when that knob is enabled
  - pointwise runtime markers now print unconditionally on the direct path instead of only under `!emit_logs`
  - new runtime markers cover:
    - scope acquire/release/fence/drain boundaries
    - pointwise pre/post-call state dump
    - cached xlate range/PTBR/PTE/fault snapshot
  - new Gemmini OS-path markers cover the first:
    - bias mvin
    - B mvin
    - A mvin
    - preload
    - compute_preloaded
    - compute_accumulated
    - mvout
- Do not assume there is an active run farm right now.
- Hardware/shared-spad constraints now carried explicitly in software:
  - PT lookup is indexed by `(vaddr - range_base) >> page_shift`
  - `pte_count` is `16-bit`
  - with `1KB` page, single action max is `65535` PTEs (`64MB - 1KB`)

Landed code hotspots:

- Exporter contract:
  `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`
  now exports the segment resource contract and exact stage-local logical SPM layout consumed by runtime.
- Runtime/layout path:
  `pipeline-runtime/src/prt_runtime.c`
  `pipeline-runtime/src/prt_schedule_action.c`
  `pipeline-runtime/src/prt_page_table.c`
  plus the matching headers now implement:
  - per-action dedicated alias VA windows
  - per-action dedicated PTBR/PTE backing
  - contiguous vpage allocation
  - exported-layout-driven exec views
  - physical page allocation from exported buffer bindings
  - action-scoped xlate install/flush with init-time global reset
- Artifact validation path:
  `pipeline-runtime/src/prt_gemmini_artifacts.c`
  now treats pipeline YAML as the orchestration-time source of truth when exact local layout is exported.

Immediate execution queue:

1. Do not start with another blind F2 replay.
2. Do not start with multi-action validation yet.
3. First recover the previous `bertmini` single-action path on baremetal/host/Linux.
4. Preserve the current per-action alias/PTBR design; do not regress to the old global slot model.
5. Before another F2 replay, do one more static runtime-specific diff pass with the new routing conclusions in mind:
   - current single-hart Gemmini issue capacity is `1` lane, not `4`
   - current cfg hashing only gives `8` non-conflicting stage contexts
   - do not propose “single hart manages many concurrent stages” as if the current stack already supports it
6. Before another F2 replay, keep using the new debug build knobs explicitly:
   - `PIPELINE_RUNTIME_PROGRESS=1`
   - `PIPELINE_RUNTIME_PROGRESS_RAW=1`
   - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
   - `PIPELINE_RUNTIME_GEMMINI_PHASE=1`
   - `PIPELINE_RUNTIME_ONLY_MARKER=1`
7. Before another F2 replay, try to reproduce the same pointwise path with the faster baremetal sample:
   - `rerocc_lc_resadd_explicit_interleaved.c`
   - especially the `PW_I=256 / PW_J=64 / PW_K=256 / stride=256` pointwise cases
   - and the new runtime-style chunked cases:
     - `pointwise_matmul_os_chunked_128_to_64_runtime_style_cross_1kb_contiguous`
     - `pointwise_matmul_os_chunked_128_to_64_runtime_style_cross_1kb_interleaved`
   - note that these now mirror the actual runtime subpath more closely:
     - outer manager-local `J=128`
     - inner `64 + 64` chunks
     - inter-chunk managed drain
8. Linux guest rebuild with those knobs is already done once on the current head; before replaying on F2, make sure the staged workload picks up that fresh binary.
9. Only after the new logs or baremetal evidence narrow the fault further, rebuild Linux staging and replay on F2.
10. If the next Linux replay still stalls, the new last-live marker should identify whether the block is at:
   - bias mvin
   - first B mvin
   - first A mvin
   - first preload
   - first compute
   - first mvout
   - or after the Gemmini call returns but before managed drain/release
11. If the runtime-style baremetal case passes at low alias VA but Linux/F2 still stalls, next isolate:
   - rebuild the baremetal sample with `SHARED_SPAD_XLATE_RANGE_BASE=<high-runtime-like-va>`
   - then rerun just the runtime-style chunked cases
   - this isolates “high alias VA” from Linux userspace and long boot time
12. Keep future-load design notes up to date while debugging, but do not spend the next round validating them before `bertmini` is back:
   - final target is up to `64` Gemmini cores, up to `6` concurrent actions, and `6` CPUs/harts
   - current stack is not yet sufficient for “one hart drives many concurrent Gemmini stages”
   - if you propose a scalability direction, explicitly separate:
     - software-only near-term path
     - software refactor path
     - hardware resource expansion path

Hard constraints:

- Do not modify hardware / RTL.
- Do not restore `host_addr` special-casing.
- Do not collapse multi-manager execution into single-manager.
- Do not break the shared-spad `all-bank / 1KB interleaved / multi-manager` design goal.
- CPU fallback is forbidden on the bertmini path.
- Avoid pack/repack style layout-changing workarounds unless you have hard evidence they are required and still preserve the model contract.
- FireSim flow must remain:
  `marshal build -> marshal install -> launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- Do not hand-edit `sims/firesim/deploy/workloads/*`.
- FireSim manager commands must run from:
  `cd /home/ubuntu/chipyard/sims/firesim && source sourceme-manager.sh --skip-ssh-setup`
- Long FireMarshal / FireSim jobs must run in tmux via:
  - `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
  - `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- Linux boot rule:
  if there is no explicit failure signal and heartbeat is still moving, keep waiting.

Already-closed conclusions which must not be reopened without new evidence:

1. shared-spad `1KB` interleaved alias translation is not generically broken.
2. pointwise `J=128` is not blocked by a hard Gemmini hardware limit.
   - That line was closed by fixing baremetal VA / PTE overlap in the chunk-bias pages.
3. `mvin2` is not generically unable to read interleaved shared-spad.
4. standard Gemmini WS resadd works on the current hardware / aliasing design.
5. no RTL change is needed for the issues closed so far.
6. `MAX_BLOCK_LEN` is not “whole-op J legality”; it is only a DMA single-transaction width bound.
7. `load-model-bin` is not the current Linux blocker.
   - It now completes on F2 and emits its end marker.
8. action-private alias VA / PTBR install is not failing before execution.
   - The current F2 run reached `bind-topology`, `workers-launched`, and `matmul-call-enter`.

Critical validated lessons from baremetal:

1. Gemmini accumulator local-address semantics matter exactly.
   - bit31 = accumulator address
   - bit30 = accumulate-on-write
   - bit29 = full-acc-row read selector
   - `3 << (ADDR_LEN - 2)` is NOT a separate B buffer.
   - It selects accumulator rows with accumulate-on-write semantics.

2. `copy_explicit_cross_1kb_interleaved_b_mvin2` was fixed by respecting that semantic.
   - Root cause:
     dirty accumulator rows were being accumulated into.
   - Fix:
     first explicitly initialize the target acc rows through the bit30=0 view,
     then issue `mvin2`, then `mvout` through the bit30=0 view.

3. `resadd_explicit_cross_1kb_interleaved` was fixed by making the full completion chain manager-visible.
   - Root cause:
     the explicit overlap sequence lacked a full ReRoCC-visible dependency chain.
   - Fix:
     `A mvin -> rr_fence -> B mvin2 -> rr_fence -> mvout`

4. `gemmini_fence()` is not a ReRoCC manager-visible completion barrier.
   - When you need manager-visible completion, use `rr_fence(cfg_id)`.

5. Do not go back to guessed tensor strides.
   - HybridMapper now exports `tensorStride`.
   - runtime already consumes `tensorStride`.
   - Keep size / stride / pad semantics coherent end-to-end.
6. The direct comparison sample for the current live stall is:
   - `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
   because it already contains:
   - pointwise matmul with `I=256 / J=64 / K=256`
   - `A/B/C stride = 256`
   - interleaved shared-spad alias layout
   - dedicated Linux-tile `bias mvin0` diagnostics

Primary improvement direction for pipeline-runtime:

- The host path now provides positive evidence that the current
  page-placement / manager contract is coherent enough for execution.
- Therefore the next useful work is no longer “keep debugging host runtime first”.
- The next useful work is:
  - stage the current artifacts into Linux correctly
  - rebuild/install the workload image
  - replay on FireSim F2
  - localize any remaining blocker to guest-only setup vs. guest runtime semantics

Secondary improvement direction:

- Keep the HybridMapper -> YAML -> runtime metadata contract explicit.
- `stride` should remain exported and consumed explicitly.
- `pad` may still be inferred for some layers; if so, verify it against tensor sizes
  instead of silently assuming it.

What to inspect next:

1. `pipeline-runtime/src/prt_gemmini_adapter.c`
   - `conv_call_for_manager_sync_strided`
   - canonical pointwise fallback selection
   - `tiled_matmul_nn_stride_auto` caller arguments
2. `include/gemmini_nn.h`
   - `tiled_matmul_nn_stride_auto`
3. `include/gemmini.h`
   - `tiled_matmul_auto`
   - tiled inner dispatch
   - OS matmul path
   - `bias mvin0` handling
4. `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
   - pointwise matmul issue path
   - Linux tile `bias mvin0` case
   - exact acquire/flush/xlate/program/wait sequence
5. Only after that, return to:
   - `rerocc-linux-tests-coupleddma/workload/host-init.sh`
   - FireMarshal overlay
   - F2 replay

Execution plan for the next session:

1. Keep the current regression order:
   - static diff
   - baremetal targeted repro
   - host closure
   - only then Linux / FireSim F2
2. Add deeper logs before the next Linux replay.
3. Reproduce the pointwise stall on the fastest possible baremetal path.
4. If baremetal reproduces:
   - fix there first
   - keep the fix consistent with runtime semantics
5. If baremetal does not reproduce:
   - use the added Linux/F2 logs to isolate what runtime changes before entering the same Gemmini API
6. Only replay on F2 after those logs/fixes are in place.
7. If a fix changes shared-spad / resadd / pointwise semantics, rerun both:
   - the baremetal gate
   - the host closure gate

Immediate 2026-03-27 update:

1. The current fastest faithful repro is now the focused baremetal workload:
   - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus.json`
2. That focused case already reproduced a hard F2 deadlock after:
   - `CASE_PROGRESS ... phase=focused_chunk_issue_begin`
   - `matmul-nn-stride-auto-enter`
   - then host:
     `Simulator deadlock detected at target cycle 0. Terminating.`
3. The deepest-path logs were split further after that run:
   - `matmul-nn-stride-auto`: `pre/post shape/addrs/flags`
   - `matmul-auto`: `pre/post shape/addrs/flags/padded/tiles/mode`
   - `matmul-config`: `shape/strides/flags`
   - `matmul-outer`: `shape/flags`
4. The refreshed focused workload has already been rebuilt and reinstalled:
   - build log:
     `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus-build-2026-03-27--01-48-55-2OFHZOZ6WJLN73M2.log`
   - install log:
     `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus-install-2026-03-27--01-49-22-8Z72LHDU1UBWYQZM.log`
5. The current runtime-side static alignment is better than before:
   - `prt_default_conv_activation()` currently forces `RELU`
   - `prt_default_conv_output_scale()` currently returns `1.0f`
   - so the focused baremetal assumption `RELU + identity scale` now matches runtime
6. Current infrastructure blocker:
   - only use `f2.6xlarge`
   - do not retry `f2.12xlarge`: current account quota is only `32` on-demand F vCPUs, while `f2.12xlarge` needs `48`
   - FireSim logs can misleadingly collapse `VcpuLimitExceeded` into a generic "insufficient capacity" message
   - `aws ec2 describe-instances ... fsimcluster` was empty, so no runfarm instance was billing when this was recorded
7. New minimal baremetal was added specifically for the current suspected deepest stall point:
   - wrapper:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_bias_mvin3_runtime_alias_focus.c`
   - body:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
   - workload:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-bias-mvin3-focus.json`
   - installed local workload binary:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_bias_mvin3_runtime_alias_focus.riscv`
8. Why this new case matters:
   - the older helper `run_bias_mvin_linux_first_tile_case(...)` only covered `mvin0`
   - it did not directly exercise the actual suspected failing path:
     `shared-spad alias/PTW + config_ld(id=2) + mvin3/LOAD3_CMD`
9. The new minimal case preserves the runtime-style assumptions that currently matter:
   - `cfg=1`
   - `opcode=3`
   - `config_ex/st/ld0/ld1/ld2`
   - `runtime_skip_preflush=1`
   - interleaved bias alias region
   - then directly issues `prt_gemmini_issue_bias_mvin3_debug(...)`
10. Current strongest static hypothesis:
   - not `config_ld(id=2)` immediate accept by itself
   - but the first real alias-consuming `mvin3` request entering the shared-spad PTW/TLB frontend path
   - the new minimal baremetal is now the fastest test to separate:
     - `mvin3 alias/PTW frontend bug`
     - from
     - `full pointwise tiled_matmul outer-path interaction bug`
11. FireSim path for the new minimal case is also ready now:
   - workload installed at:
     `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-bias-mvin3-focus.json`
   - runtime config:
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_bias_mvin3_focus.yaml`
12. Static grep narrowed `LOAD3/state2` special handling sharply:
   - mainly only:
     - `LoadController.scala`
     - `ReservationStation.scala`
   - other files mostly only generate `LOAD3` commands
13. Therefore, interpret the next run this way:
   - if the minimal `bias_mvin3_runtime_alias_focus` deadlocks:
     prioritize `LoadController` / `ReservationStation` / shared-spad PTW frontend
   - if it passes:
     return to the full pointwise focused workload and inspect outer `matmul` sequencing
14. Newer static narrowing after that handoff:
   - for the current focused pointwise parameters:
     - `DIM=8`
     - `dim_I=256`
     - `dim_J=64`
     - `dim_K=256`
   - `tiled_matmul_auto()` collapses to:
     - `tile_I=32`
     - `tile_J=8`
     - `tile_K=32`
   - and, importantly:
     - `I0=1`
     - `J0=1`
     - `K0=1`
   - so the full focused pointwise case is effectively:
     - one outer tile
     - one `sp_tiled_matmul_os(...)` inner call
15. Based on that, a deeper focused baremetal is now ready:
   - wrapper:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_pointwise_os_inner_runtime_alias_focus.c`
   - body:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
   - workload:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-os-inner-focus.json`
   - installed deploy workload:
     `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-os-inner-focus.json`
   - runtime config:
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_os_inner_focus.yaml`
16. What the new deeper focused baremetal does:
   - still uses runtime-style:
     - shared-spad alias
     - `cfg=1`
     - `opcode=3`
     - `config_ex/st/ld0/ld1/ld2`
     - `runtime_skip_preflush=1`
   - but instead of going through:
     - `tiled_matmul_nn_stride_auto -> tiled_matmul_auto -> tiled_matmul_outer`
   - it directly calls one:
     - `sp_tiled_matmul_os(...)`
   - with the exact effective first/only inner-tile shape:
     - `I=32`
     - `J=8`
     - `K=32`
     - `pad_I=0`
     - `pad_J=0`
     - `pad_K=0`
     - `repeating_bias=1`
     - `act=RELU`
17. Binary readiness already verified locally:
   - binary:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/bareMetalC/rerocc_lc_pointwise_os_inner_runtime_alias_focus-baremetal`
   - `strings` confirmed these markers are in:
     - `pointwise_os_inner_runtime_alias_focus`
     - `sp-tiled-matmul-os-single-inner`
     - `focus-inner-pre-call`
     - `focus-inner-post-call`
     - `matmul-os-biascfg-first-iter-enter`
     - `matmul-os-pre-b-mvin2`
     - `matmul-os-pre-preload0`
18. Current recommended experiment order:
   - first:
     `rerocc-lc-baremetal-coupleddma-bias-mvin3-focus.json`
   - second, if first passes:
     `rerocc-lc-baremetal-coupleddma-pointwise-os-inner-focus.json`
   - only then go back to the full focused pointwise workload
19. Current infra status:
   - any old `*_f212` retry sessions should be considered obsolete and should be stopped
   - continue only with `f2.6xlarge` configs
   - if FireSim says "insufficient capacity", inspect the raw `launchrunfarm` log for `VcpuLimitExceeded` before assuming it is a real AZ shortage
20. New static conclusion from this session:
   - do not treat `D_sp_addr_start` and `C_sp_addr_start` as unrelated storage regions
   - they share the same accumulator row index and differ mainly in local-addr metadata
   - therefore bias may be applied by accumulator RMW on writeback when `out_sp_addr.accumulate=1`
   - so `preload(GARBAGE_ADDR, out_sp_addr)` is not, by itself, proof of a bug
21. New deepest-path instrumentation already added:
   - file:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/include/gemmini.h`
   - new helpers:
     - `prt_gemmini_debug_localaddr()`
     - `prt_gemmini_issue_preload_debug()`
     - `prt_gemmini_issue_compute_preloaded_debug()`
     - `prt_gemmini_issue_compute_accumulated_debug()`
   - these now instrument only the first:
     - `preload`
     - `compute_preloaded`
     - `compute_accumulated`
   - and print:
     - raw `rs1/rs2`
     - `is_acc`
     - `accumulate`
     - `read_full`
     - `acc_row`
     - `sp_row`
22. Local compile re-check after adding the new instruction-level debug:
   - rebuilt successfully:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/rerocc_lc_pointwise_os_inner_runtime_alias_focus-baremetal`
23. Updated interpretation rule for the next focused replay:
   - if `bias_mvin3_runtime_alias_focus` passes but `pointwise_os_inner_runtime_alias_focus` still hangs,
   - use the new first-preload / first-compute raw encodings to determine whether the direct software
     `PRELOAD + COMPUTE` issue path is mismatched with the current `ExecuteController` lane expectations
24. There is now an even narrower baremetal than `pointwise_os_inner_runtime_alias_focus`:
   - wrapper:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_pointwise_os_first_pair_runtime_alias_focus.c`
   - workload binary:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_pointwise_os_first_pair_runtime_alias_focus.riscv`
   - workload json:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-os-first-pair-focus.json`
   - deploy workload:
     `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-os-first-pair-focus.json`
   - runtime config:
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_os_first_pair_focus.yaml`
25. What that new first-pair case does:
   - keeps runtime-style alias / cfg / opcode / load-state setup
   - but only issues:
     - first `bias mvin3`
     - first `B mvin2`
     - first `A mvin0`
     - first `preload`
     - first `compute_preloaded`
   - then waits
26. Updated recommended experiment order:
   - first:
     `rerocc-lc-baremetal-coupleddma-bias-mvin3-focus.json`
   - second:
     `rerocc-lc-baremetal-coupleddma-pointwise-os-first-pair-focus.json`
   - third, only if second passes:
     `rerocc-lc-baremetal-coupleddma-pointwise-os-inner-focus.json`
   - only then:
     return to the full focused pointwise workload
27. New high-VA focused baremetal result from 2026-03-27:
   - workload:
     `rerocc-lc-baremetal-coupleddma-pointwise-stage0-highva-vpage0-focus.json`
   - runtime config:
     `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_stage0_highva_vpage0_focus.yaml`
   - deploy workload file had to be added manually:
     `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-highva-vpage0-focus.json`
   - use this hwdb/build-recipe pair, not the sample configs:
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb.yaml`
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_coupleddma_10mhz.yaml`
28. What that high-VA focused run proved:
   - it reaches the Linux-like first pointwise bias path with:
     - high alias base `0x3f9ce22000`
     - `vpage_bias=0`
     - `vpage_b=1`
     - `vpage_a=65`
     - `vpage_c=129`
   - it prints:
     - `bm0c0`
     - `bm0ce`
     - `bm0ca`
     - `bm0cb`
     - `bm0c1`
   - then continues into:
     - `matmul-os-post-bias-mvin3`
     - `matmul-os-post-b-mvin2`
     - `matmul-os-post-a-mvin0`
     - `matmul-os-preload-debug-enter`
   - so this case does **not** stably reproduce the Linux/F2 `bm0ca` stop
29. Updated interpretation after that run:
   - high VA alias base + Linux-like `0/1/65/129` vpage layout is not sufficient, by itself,
     to explain the Linux pipeline-runtime blocker
   - remaining suspects are now more Linux-specific:
     - runtime PTBR/PTE physical allocation/install path
     - longer runtime control-plane state before first pointwise issue
     - cfg/opcode/fence/release state that focused baremetal does not yet preserve
30. Latest infrastructure note:
   - that high-VA F2 run used instance `i-0e56f71078cb1fe6b`
   - after collecting the result it was reclaimed with `terminaterunfarm --forceterminate`
   - do not assume the manager-side `sample_config_hwdb.yaml` is valid for this target
31. New static correction from this session:
   - the previous focused baremetal still did **not** match Linux stage0 physical shared-spad placement
   - it matched:
     - high alias base
     - `vpage 0/1/65/129`
   - but it did **not** match Linux physical page placement:
     - Linux input starts at `a0/l0`, then interleaves with `a1/l0`
     - Linux output starts at `a0/l32`, then interleaves with `a1/l32`
     - Linux bias is `a0/l64`
     - Linux weights start at `a1/l64`, then interleave
   - the old focused wrapper instead used local page bases around `480/560/704/656`
32. New code added for that correction:
   - shared focused-stage0 local-page / slot-offset parameters are now overridable in:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
   - new exact-layout wrapper:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_pointwise_stage0_runtime_linuxphys_focus.c`
   - it now mirrors Linux stage0 with:
     - range base `0x3faf751000`
     - `vpage_bias=0`
     - `vpage_b=1`
     - `vpage_a=65`
     - `vpage_c=129`
     - input local page base / slot offset `0 / 0`
     - output local page base / slot offset `32 / 0`
     - bias local page base / slot offset `64 / 0`
     - weights local page base / slot offset `64 / 1`
33. Local compile status:
   - successfully built:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/rerocc_lc_pointwise_stage0_runtime_linuxphys_focus-baremetal`
34. FireSim baremetal entry for that new case is already wired:
   - host-init:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/host-init-pointwise-stage0-linuxphys-focus.sh`
   - workload json:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus.json`
   - deploy workload:
     `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus.json`
   - runtime config:
     `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_stage0_linuxphys_focus.yaml`
   - host-init was also run locally and produced:
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_pointwise_stage0_runtime_linuxphys_focus.riscv`
35. Updated next experiment priority:
   - before adding yet another Linux/F2 deep marker round,
   - first run the new `linuxphys_focus` baremetal
   - because it is the first focused reproducer that matches:
     - high VA
     - `vpage 0/1/65/129`
     - Linux stage0 physical shared-spad placement
36. Interpretation rule for that run:
   - if `linuxphys_focus` now reproduces the stop near `bm0ca`,
     the dominant missing variable was physical page placement
   - if it still passes,
     the search should move back to Linux/runtime-only control-path differences
     rather than page-placement hypotheses
37. New dynamic result from this session:
   - the exact-layout F2 focused baremetal `linuxphys_focus` was actually run
   - it confirmed Linux-matching:
     - range base `0x3faf751000`
     - `vpage 0/1/65/129`
     - physical page placement
   - yet it still advanced through:
     - `bm0ca`
     - `bm0cb`
     - `bm0c1`
     - `matmul-os-post-bias-mvin3`
     - `matmul-os-post-b-mvin2`
     - `matmul-os-post-a-mvin0`
     - `matmul-os-preload-debug-enter`
     - `matmul-os-post-compute-preloaded0`
   - therefore exact physical page placement is also **not** sufficient to reproduce the Linux blocker
38. New FireSim workflow gotcha fixed in this session:
   - when adding a new deploy workload json, also create the matching directory
     `sims/firesim/deploy/workloads/<benchmark_name>/`
   - otherwise `infrasetup` may fail resolving workload-relative rootfs paths
   - for `linuxphys_focus`, adding:
     `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus/README`
     fixed the problem
39. Infrastructure cleanup status:
   - the F2 run used instance `i-03f7adafb3630876d`
   - after collecting the conclusion it was reclaimed
   - current last confirmed state: `shutting-down`

Important infrastructure notes:

- `spot` was already tried and is currently not useful.
  - `f2.6xlarge spot` repeatedly returned `insufficient capacity`
  - this was not a workload / AGFI / FireSim configuration issue
- Use on-demand unless AWS capacity conditions clearly change.
- If a run is truly deadlocked, terminate the run farm before editing code.
- Do not trust FireSim manager exit codes alone; inspect guest `uartlog`.
- Current deadlock heuristic:
  if `heartbeat.csv` keeps moving but the same deepest `prt-marker` does not change for `90s+`,
  treat the run as deadlocked and reclaim the F2 instance.

Reference files:

- handoff summary:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/HANDOFF.md`
- current status:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/STATUS.md`
- current plan:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/PLAN.md`
- validated lessons:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/LESSONS_LEARNED.md`
- process / FireSim discipline:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/PROCESS.md`
- pointwise archive:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/SEG0_LAYER0_POINTWISE_ATTEMPTS.md`
- older stall archive:
  `/home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/SEG0_LAYER0_STALL.md`

Start by summarizing:

- the active Linux blocker,
- the latest ReRoCC cfg/opcode conclusion,
- the runtime page-placement / manager-contract hypothesis,
- the HybridMapper metadata contract you intend to preserve,
- and exactly which parts of `prt_runtime.c` / `prt_gemmini_adapter.c`
  you will inspect first.
```
