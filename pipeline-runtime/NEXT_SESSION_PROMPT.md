# Next Session Prompt

```text
You are taking over pipeline-runtime / HybridMapper / FireSim FPGA debugging in:

/home/ubuntu/chipyard

Read these first:

1. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/STATUS.md
2. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/PROCESS.md
3. /home/ubuntu/chipyard/AGENTS.md

Mission:

- Get `bertmini` passing end-to-end on FireSim F2.
- Keep the current single-layer-stage contract.
- Do not change model definition files.
- Do not regress HybridMapper original behavior.

Hard constraints:

- FireSim flow must stay:
  marshal build -> marshal install -> launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm
- Use `f2.6xlarge` only.
- Do not hand-edit `sims/firesim/deploy/workloads/*`.
- CPU fallback is forbidden on the bertmini path.
  - Do not add new CPU fallback.
  - Any existing CPU fallback on bertmini paths must be treated as historical debug residue and removed by root-fixing the Gemmini path.
- Linux boot rule:
  if there is no explicit failure signal, keep waiting.
- Gemmini semantics must be checked against:
  - `/home/ubuntu/chipyard/generators/gemmini/README.md`
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/include`
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/imagenet`

Current confirmed state on 2026-03-22:

1. Host closure passes.
   - `BERTMINI_HOST_CLOSURE_PASS`

2. Runtime assumptions are real.
   - `subbatch_size=1`
   - `HUGETLB_PAGES=1`
   - `Hugepagesize=2048 KiB`
   - total hugetlb reservation = `2 MiB`

3. The old host-backed Linux DMA blocker is not the active problem anymore.
   - active Linux `dram <-> spm` traffic is already routed through the protected helpers in `prt_dma.c`
   - small regression already proved `dma_dram_to_shared_misaligned_fullpage PASS`

4. Large layer-mapping YAML parsing was mitigated with the derived `.cache.bin` path.
   - runtime loader: `prt_gemmini_artifacts.c`
   - staging generator: `host-init.sh` + `generate_gemmini_mapping_cache.py`
   - this path was already observed working on FPGA

5. Suspected Gemmini hardware bug #1:
   canonical 1x1 pointwise fallback can hang in WS loop matmul.
   - strongest RTL suspect remains:
     `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/LoopMatmul.scala`
   - suspicious line:
     `val ldb_ahead = io.ldb_completed || io.ld_kb > k || (io.ld_ka === k && io.ld_j > j)`
   - this is only a suspected RTL bug; hardware has NOT been rebuilt with a fix

6. Software handling for bug #1 is already in place.
   - canonical 1x1 pointwise fallback requests that would have used WS are forced to OS
   - hot-path logs were thinned so repeated worker / DMA / Gemmini logs are off by default unless `PRT_ENABLE_PROGRESS_HOT_LOG=1`
   - canonical pointwise fallback is now direct-strided zero-repack Gemmini
   - canonical pointwise split-OC also has a direct-strided Gemmini path

7. These pointwise changes were validated by the latest pure-Gemmini FireSim run.
   - that run passed:
     - `segment=0`
     - `segment=1`
     - `segment=2`, including the old risky `subbatch=14` region
     - `segment=3` (both `stage=0` and `stage=1`)
     - `segment=4`
   - no CPU fallback markers were seen before the next blocker

8. Current newest blocker is no longer pointwise.
   - the latest pure-Gemmini run `bertmini-runworkload7` reached:
     - `segment=5 begin ...`
     - `worker stage=0 ready entries=2 exports=1 isolate_pairs=0 shared_pairs=0 acc=0 dma=2 tiles=2`
     - `worker stage=0 subbatch=0 begin op=2 acc=0 dma=2 tiles=2`
   - then `uartlog` stopped advancing at that exact line
   - meanwhile `heartbeat.csv` kept growing for minutes
   - no CPU fallback markers appeared
   - no Linux panic / Oops / deadlock marker appeared
   - treat this as a real resadd Gemmini primitive stall, not Linux boot and not the old pointwise UART partial-line issue

9. Current suspected Gemmini hardware bug #2:
   resadd can hang in `gemmini_loop_ws(... is_resadd=1)`.
   - live code chain:
     `prt_gemmini_adapter.c -> resadd_issue_no_fence() -> gemmini.h -> sp_tiled_resadd() -> gemmini_loop_ws(... is_resadd=1)`
   - historical archive had already suspected:
     `gemmini_flush -> tiled_resadd_auto -> gemmini_fence`

10. Historical attempt for bug #2:
    - large/spatial resadd WS paths were temporarily routed to CPU fallback
    - that path is now forbidden by user hard constraint
    - next work must keep the fix on Gemmini

11. Current local workspace state:
    - latest successful image build/install logs are:
      - `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-21--16-10-19-T6AGVCRBO6ZV0BYN.log`
      - `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-21--16-10-51-3BYKVKSQ2T0MK7O4.log`
    - current working tree also contains an unvalidated draft resadd fix in:
      `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c`
    - draft intent:
      - keep RISC-V resadd Gemmini-only
      - bypass `sp_tiled_resadd()/loop_ws`
      - try explicit `mvin/mvin2/mvout`
    - this draft has NOT yet been rebuilt, installed, or rerun

12. Current infrastructure state:
    - the stalled run farm was reclaimed
    - as of 2026-03-22 there are no active `fsimcluster` instances
    - as of 2026-03-22 there are no active `f2.*` instances
    - start from a fresh `launchrunfarm`

13. Remaining optimization / cleanup inventory:
    - pointwise chunked zero-repack is already in place; it is no longer the main optimization TODO
    - generic split-conv repack sites still remain:
      - `oc-split` default path uses `w_pack/b_pack/o_pack`
      - `spatial-split` uses `in_pack/out_pack`
    - important distinction:
      - those operator repacks are optimization targets
      - the current `segment=5` stall is NOT a repack issue; it is a resadd Gemmini primitive issue
      - `prt_dma.c` bounce-buffer copies are DMA correctness workarounds, not the same problem
    - current priority order:
      1. fix the resadd Gemmini stall
      2. zero-repack the default `oc-split` path
      3. zero-repack the `spatial-split` path

Immediate next steps:

1. Review the current diff in `prt_gemmini_adapter.c` and decide whether to keep or revise the draft resadd Gemmini-only fix.
2. Rebuild and reinstall the workload image.
3. Run the full FireSim flow from scratch:
   `launchrunfarm -> infrasetup -> runworkload`
4. Re-confirm the old pointwise danger zones still pass:
   - `segment=2 / subbatch=14`
   - `segment=3`
   - `segment=4`
5. Then focus on `segment=5` and verify the run does NOT stop at:
   `worker stage=0 subbatch=0 begin op=2 acc=0 dma=2 tiles=2`
6. If `segment=5` passes, continue to final completion.
7. Final success criterion:
   `BERTMINI_PIPELINE_RUNTIME_PASS`

If the run still stalls:

- First distinguish whether the stop point is still `segment=5 / op=2`.
- If it is, continue narrowing the failing resadd Gemmini primitive on the Gemmini path.
- Do not use CPU fallback for pointwise, resadd, or any other bertmini runtime path.
- Do not claim resadd `LOOP_WS` is fixed unless either:
  - the RTL is changed and rebuilt, or
  - the software no longer relies on that buggy primitive and the new Gemmini-only path is revalidated on FPGA.
```
