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
- Keep the current single-layer-stage contract unless the existing mapping format clearly requires something else.
- Do not regress HybridMapper original behavior.
- Do not discard local uncommitted changes in this workspace.

Current status:

- The baremetal correctness gate is now fully green.
- The decisive baremetal run is:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-24--12-47-02-rerocc-lc-baremetal-coupleddma-explicit-interleaved-small-f2-rerocc-baremetal-explicit-interleaved-small`
- Final guest result there is:
  - `ALL_TESTS_PASS`
  - `*** PASSED *** after 27505375802 cycles`
- Therefore baremetal is no longer the active blocker.
- The active blocker is back on the Linux `pipeline-runtime` path.
- Do not assume there is an active run farm right now.

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

Primary improvement direction for pipeline-runtime:

- The strongest remaining software-side suspicion is still the runtime’s own
  page-placement / manager contract, not the hardware.
- Fixed-weight page placement is already closer to a multi-manager view.
- Entry/export tensor local-slot page placement, exec-view rebasing, and
  manager binding may still be biased toward a single `stage_acc`.
- That would make:
  - workload partitioning = multi-manager
  - address-space / page placement = not truly multi-manager-consistent
- The next useful work is to unify runtime stage-local page placement,
  exec-view rebasing, and manager binding semantics around one contract:
  - continuous shared-spad alias VA range
  - per-manager page-table translation
  - preserved `all-bank / 1KB interleaved` physical page layout

Secondary improvement direction:

- Keep the HybridMapper -> YAML -> runtime metadata contract explicit.
- `stride` should remain exported and consumed explicitly.
- `pad` may still be inferred for some layers; if so, verify it against tensor sizes
  instead of silently assuming it.

What to inspect next:

1. `pipeline-runtime/src/prt_runtime.c`
   - stage-local tensor page placement
   - exec-view construction
   - manager binding / stage_acc usage
   - size / stride / pad validation paths
2. `pipeline-runtime/src/prt_gemmini_adapter.c`
   - whether any explicit handwritten path still violates the proven baremetal semantics
   - whether standard WS paths can be preferred where semantically valid
   - whether any pointwise direct-strided / fallback path still bypasses the intended
     pipeline-buffer contract
3. HybridMapper-produced metadata / runtime loader contract
   - confirm tensor stride / size / page-placement assumptions remain coherent
   - prefer explicit metadata over guessed layout assumptions

Execution plan for the next session:

1. Reproduce the current Linux pipeline-runtime blocker on the current head.
2. Do not touch baremetal unless a runtime change affects shared-spad / resadd / pointwise semantics.
3. If a runtime fix changes those semantics, rerun the baremetal gate before trusting Linux results.
4. Keep the debugging order:
   - software contract
   - Gemmini ISA usage
   - only then hardware suspicion

Important infrastructure notes:

- `spot` was already tried and is currently not useful.
  - `f2.6xlarge spot` repeatedly returned `insufficient capacity`
  - this was not a workload / AGFI / FireSim configuration issue
- Use on-demand unless AWS capacity conditions clearly change.
- If a run is truly deadlocked, terminate the run farm before editing code.
- Do not trust FireSim manager exit codes alone; inspect guest `uartlog`.

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
- the runtime page-placement / manager-contract hypothesis,
- the HybridMapper metadata contract you intend to preserve,
- and exactly which parts of `prt_runtime.c` / `prt_gemmini_adapter.c`
  you will inspect first.
```
