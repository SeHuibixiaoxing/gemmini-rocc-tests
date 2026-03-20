# Next Session Prompt

```text
You are taking over pipeline-runtime / HybridMapper / FireSim FPGA debugging work in:

/home/ubuntu/chipyard

Read these first and treat them as the current source of truth:

1. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/STATUS.md
2. /home/ubuntu/chipyard/conference/mudnac_hybridmapper_collab_docs/PROCESS.md
3. /home/ubuntu/chipyard/AGENTS.md

Current mission:

- Keep the current single-layer-stage contract.
- Do not implement MudnacSim fake multi-layer-stage semantics.
- Do not modify model definition files.
- Keep HybridMapper original behavior intact; only use the added exporter path for pipeline-runtime artifacts.
- Finish stage-1 objective: bertmini end-to-end closure on FireSim F2 with cpu golden vs fpga backend compare.

Hard constraints from the user:

- pipeline-runtime only reads the new interface.
- generated layer mapping uses local zero-based SPM addresses; runtime must rebase/allocate at execution time.
- accelerator allocation is still runtime-dynamic; physicalAccIds are not the active path in current generated mappings.
- FireSim flow must be:
  marshal build -> marshal install -> launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm
- Do not hand-edit sims/firesim/deploy/workloads/*
- Use f2.6xlarge only.
- Keep docs updated while working.

Superseding 2026-03-20 update. This overrides older references below to the
"first DMA set_src hang" as the current top blocker:

- A new workload-entry bug was found and fixed in:
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh
- Root cause:
  - small-regression images use host-init-pipeline-noexec.sh, which correctly sets
    PIPELINE_RUNTIME_ONLY_MARKER=0
  - but the overlay directory was reused, and stale
    /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests-coupleddma/.pipeline_runtime_only
    was never removed
  - therefore the "small regression" image accidentally exec'd bertmini runtime
- Fix:
  - stage_coupleddma_overlay() now removes .pipeline_runtime_only before restaging
  - do not revert this

Newest small-regression FPGA result after that fix:

- Rebuilt/reinstalled workload image:
  - build log:
    /home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-build-2026-03-20--03-42-28-D7W7NPYBCLNEMHJ5.log
  - install log:
    /home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-install-2026-03-20--03-42-58-N30FKHHIYUO9PM0G.log
- Fresh run:
  - runworkload log:
    /home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--03-48-28-runworkload-KHO2Y8NS8WNJO98U.log
  - results dir:
    /home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-20--03-48-28-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles
- The guest now correctly runs:
  - gemmini matrix verify
  - DMA matrix verify
  - coverage
  - nonblocking
- The key DMA verdict is now confirmed on FPGA:
  - CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS
- Therefore:
  - the Linux host<->SPM bounce-buffer workaround did fix the misaligned full-page DMA shape that had been the leading hypothesis
  - older notes that still frame "misaligned 1B copy at first DMA submit" as the primary live blocker are now historical
- One separate issue remains in the small-regression workload:
  - nonblocking scenario 1 fails
  - visible evidence:
    - SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=0
    - NONBLOCKING_SUMMARY s1=0 s2=1 s3=1 s4=1
    - ALL_TESTS_FAIL matrix_ret=0 coverage_ret=0 nonblocking_ret=1
  - treat this as a distinct Linux nonblocking issue, not as evidence that the misaligned DMA fix failed

Newest bertmini FPGA result after the small-regression verdict:

- Reused run farm tag f2rerocccoupleddmarun on instance:
  - i-0ac5afcb3d07d950a
  - private: 192.168.1.84
  - public: 44.246.247.25
- Re-ran infrasetup with:
  - /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml
- Fresh bertmini run:
  - runworkload log:
    /home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--04-13-35-runworkload-16O7E4UXGU82BQRM.log
  - results dir:
    /home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-20--04-13-35-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime
- This run no longer shows the old first-DMA hang.
- New positive evidence now visible on FPGA:
  - init load-model-bin end
  - init load-input-blob end
  - segment=0 begin
  - worker stage=0 ready
  - many repeated:
    - [prt-raw] dma post-src
    - [prt-progress] dma-wait fence-enter ...
    - [prt-progress] dma-wait fence-done ...
  - full_byte_mode_hint=1 is now also visible on successful DMA waits
- The current freshest blocker has moved again:
  - after:
    - [prt-progress] worker stage=0 subbatch=0 begin op=1 acc=0 dma=2 tiles=2
  - the guest process crashes with:
    - unhandled signal 11 code 0x2
    - badaddr: 0000003fb21ba400
    - epc : 0000003fb323d424
    - /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh ... Segmentation fault
    - BERTMINI_PIPELINE_RUNTIME_FAIL
- Therefore the active blocker is now:
  - guest-side memory corruption / invalid-pointer access during pipeline-runtime execution after DMA submits have already succeeded
  - not a CoupledDMA first-submit hang
- A follow-up static audit of `pipeline-runtime` DMA call sites is now also complete:
  - all active Linux/RISC-V host-backed paths (`stage_prepare_exec_views` fixed-tensor preload,
    `prt_process_c1` dram->spm, `copy_tensor_pages_to_model_aliases` spm->model-alias,
    `prt_process_c2` spm->dram) now funnel through the host<->SPM helpers in
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    and therefore inherit the bounce-buffer workaround
  - the remaining direct `prt_dma_submit()` overlap paths in
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c`
    are contiguous page-aligned `SPM -> SPM` copies only, not the same host-backed bug class
  - `prt_dma_copy_spm_va()` still permits future arbitrary-offset `SPM -> SPM` copies, but currently has no call sites;
    treat it as latent risk, not the active blocker

Current priority for the next session:

- Preserve the small-regression DMA verdict:
  - dma_dram_to_shared_misaligned_fullpage PASS on FPGA
- Do not spend another session re-proving the old first-DMA hang.
- Focus instead on the new bertmini crash:
  - understand which runtime buffer / tensor / stage structure corresponds to badaddr 0x0000003fb21ba400
  - correlate the crash with the first segment's worker-stage execution after repeated successful DMA transfers
  - use the current FPGA evidence as proof that DMA now progresses beyond the old stop point
- The bertmini run farm from this rerun has already been terminated:
  - terminaterunfarm log:
    /home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--04-38-16-terminaterunfarm-S8FN3IUKR4V3HNNI.log
  - instance i-0ac5afcb3d07d950a reached shutting-down

What is already done:

- HybridMapper exporter exists:
  /home/ubuntu/chipyard/conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py
- pipeline-runtime new interface and offline mapping path are already in place.
- coupled-DMA VA/PA fixes are already implemented:
  - completion flag translated with virt_to_phys
  - Linux/RISC-V dram<->spm path chunked by host page and translated per chunk
  - C2 export overlap disabled for DRAM path
- parse-once layer-mapping validation is already implemented.
- HugeTLB/PTBR path is already implemented.
- host pipeline-runtime clean build currently passes.
- latest local smoke after the newest edits:
  METHODS=ours2 BATCH=1 SKIP_EXPORT=1 SKIP_BUILD=1 \
  bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
  returns BERTMINI_HOST_CLOSURE_PASS.

The most important current blocker:

- Latest FPGA failure is not FireSim infra.
- Linux reaches S99run and the bertmini wrapper.
- guest process rerocc_pipeline now reaches well past the old first-DMA stop:
  - [prt-early] runtime_init done
  - artifacts validate end
  - init load-model-bin end
  - init load-input-blob end
  - segment=0 begin
  - worker stage=0 ready
  - repeated dma post-src
  - repeated dma-wait fence-enter / dma-wait fence-done
- The current freshest failure is:
  - guest Segmentation fault / unhandled signal 11
  - BERTMINI_PIPELINE_RUNTIME_FAIL
- So the current focus is no longer the first DMA submit path itself; it is the guest-side crash that happens after DMA progress.

Latest static conclusions you should preserve:

- The earlier "first DMA set_src hang" hypotheses are now historical context, not the active top blocker.
- The small-regression FPGA rerun already proved:
  - dma_dram_to_shared_misaligned_fullpage PASS
- The bertmini FPGA rerun already proved:
  - DMA progresses beyond post-src and fence-done many times before the guest crashes

- Current runtime does not effectively use poll_progress_thread in this configuration.
  Because `spm_xlate_enable` is on, `prt_runtime_init()` forces:
  - `sync_mode -> BLOCKING_DEBUG`
  - `dma_backend -> BLOCKING_FENCE`
  - `gemmini_mode -> BLOCKING_FENCE`
- Therefore the current hang should be reasoned about primarily as:
  - `dma_blocking_submit()`
  - `hw_dma_set_dst() / hw_dma_set_src()`
  - `hw_dma_fence()`
  not as progress-thread polling.

- The earlier suspicion that shared-scratchpad local 1-byte accesses are intrinsically illegal is now downgraded.
  Static review shows:
  - `TLRAM` advertises `TransferSizes(1, beatBytes)` for Get/Put
  - `TLFragmenter(min=beatBytes, ...)` forbids fragmenting *to* sub-beat, but allows requests whose original size is already `<= min`
  So the better hypothesis is not "illegal TL", but "under-tested 1-byte copy behavior in GemminiCoupledDMA".

- pipeline-runtime currently has a very plausible way to generate exactly that under-tested path:
  - `PRT_PAGE_SIZE_BYTES = 1024`
  - Linux `dram->spm` copies only split on host page boundaries
  - model/input blobs have `malloc` paths and are not forced to 64B alignment
  - the first live FPGA request already showed:
    - `src offset = 0x10`
    - `dst offset = 0x00`
    - `bytes = 1024`
  In `GemminiCoupledDMA`, this statically forces the whole request into `1B Get/Put` mode because wide copy only happens when both ends are beat-aligned.

- known-good Linux coupled-DMA tests do not cover this shape well:
  - they use page-aligned `mmap` buffers
  - they preserve matching src/dst offsets
  - they also issue an explicit `fence rw, rw` before programming DMA
- pipeline-runtime currently lacks that explicit pre-submit `fence rw, rw`.
  Keep this as a secondary software suspect, but the primary hardware-side lead is still the misaligned 1-byte copy path.

- A newer static check found that this is probably not just stage-thread migration across harts:
  - `prt_runtime.c` already calls `stage_bind_current_thread()` and pins each stage worker to a fixed CPU
  - so simple `RROPC2` remapping caused by stage-worker migration is a lower-priority suspect now
  - however, it is still important to verify whether:
    - `custom2 -> cfg` stayed mapped to the expected cfg
    - the target `RRCFG<cfg>` still shows `acq=1` and the expected manager id
    across `set_dst -> set_src`

Latest code changes already landed:

- `pipeline-runtime/src/prt_dma.c` now has narrower DMA logs around the first submit/wait path:
  - `src_mod64`
  - `dst_mod64`
  - `done_mod64`
  - `initial_wide_hint`
  - `full_byte_mode_hint`
  - `submit-fence begin/done`
  - `dma-wait fence-enter/done`

- `dma_blocking_submit()` now snapshots:
  - `token_id`
  - `stage_idx`
  - `tensor_id`
  - `src/dst/bytes`
  into locals before the first DMA programming instructions, so later logs do not disappear merely because the token struct was corrupted after `set-dst`.

- A minimal experimental software probe is already in place:
  - `dma_blocking_submit()` now issues `fence rw, rw` before `set_dst/set_src`
  - This is not yet validated on FPGA; treat it as a probe, not a fix

- New ReRoCC narrow logs are also already landed:
  - `prt_rerocc.c` now logs `rr-acquire armed ... raw_cfg/cfg_acq/cfg_mgr/opc_map`
  - `prt_dma.c` now logs, at both `before-set-dst` and `before-set-src`:
    - `rr_cpu`
    - `rr_cfg / rr_mgr / rr_opc`
    - `rr_opc_map`
    - `rr_cfg_raw / rr_cfg_acq / rr_cfg_mgr`
  - the purpose is to distinguish:
    - ReRoCC opcode/cfg mapping drift
    - lost cfg acquire / wrong manager binding
    - vs. Coupled-DMA `SRC_INFO` acceptance itself being stuck

Latest minimal validation already done:

- host build with progress enabled passed
- RISC-V Linux cross-build of `rerocc_pipeline_runtime-linux` passed
- host closure passed:
  - `METHODS=ours2 BATCH=1 SKIP_EXPORT=1 SKIP_BUILD=1 bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh`
  - result: `BERTMINI_HOST_CLOSURE_PASS`
- A fresh unsandboxed FPGA rerun has now been completed far enough to capture the new narrow DMA evidence.

Immediate blocker before the next rerun:

- A new `marshal build` was attempted to rebuild the image with the narrowed DMA logs and submit-side `fence rw, rw`.
- The workload compile itself succeeded and the overlay staging steps succeeded.
- The build failed only at the final image overlay / mount step:
  - `guestmount --pid-file ... -a ...img -m /dev/sda .../disk-mount`
  - `libguestfs: error: /usr/bin/supermin exited with error status 1`
- `libguestfs-test-tool` was then run with:
  - `LIBGUESTFS_DEBUG=1 LIBGUESTFS_TRACE=1 libguestfs-test-tool`
  and it narrowed the failure to:
  - `supermin: exception: Sys_error("/var/tmp/supermin...tmpdir: Permission denied")`
- Therefore the current blocker before any new FPGA rerun is:
  - the execution environment must allow `libguestfs` / `supermin` to write under `/var/tmp/.guestfs-*` and `/var/tmp/supermin*.tmpdir`
- Do not misinterpret this particular failure as:
  - a workload compile regression
  - a bad overlay payload
  - a new guest-side runtime failure
- Preserve the current FPGA baseline until `marshal build` / `marshal install` can be rerun successfully with an environment that permits that `/var/tmp` access.

Important execution rule learned on 2026-03-19:

- Do not trust sandbox-side `libguestfs` / `guestmount` / `supermin` failures for this workflow.
- The same workload was rebuilt and installed successfully once the commands were run directly in the real manager environment:
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh`
  - then run `marshal build` / `marshal install` unsandboxed
- Therefore, future FireMarshal build/install and FireSim manager runs should default to the real unsandboxed environment first.
- Treat sandbox-only `/var/tmp`, `/boot/vmlinuz-*`, or socket setup failures as tooling artifacts unless they are reproduced unsandboxed.

Superseding live FPGA update:

- The fresh unsandboxed rerun did eventually prove that Linux boot is just very slow here.
- Do not treat the temporary early-boot stop near `software IO TLB` as the final blocker.
- That run later reached:
  - `S99run`
  - `[bertmini]`
  - `[prt-early]`
  - `[prt-progress]`
  - `segment=0 begin`
  - `worker stage=0 ready`
  - the first `dma-submit`
- The new narrow DMA evidence on FPGA is now:
  - visible:
    - `submit-fence begin`
    - `submit-fence done`
    - `set-dst done`
    - `before-set-src`
  - still missing:
    - `set-src done`
    - `dma-wait fence-enter`
    - `dma-wait fence-done`
- Therefore the current freshest stop point is no longer `set-dst done`; it has moved forward to:
  - `dma-submit token=1 before-set-src src=0x104ad6410`
- Since `heartbeat.csv` kept advancing while UART stopped there, the current best interpretation is:
  - not stuck in `hw_dma_submit_fence()`
  - not stuck before logging `before-set-src`
  - more likely stuck in `hw_dma_set_src(...)` itself or the immediately triggered hardware interaction
- The run farm for that fresh rerun has already been terminated:
  - instance `i-0887427d22c518b31`
  - `terminaterunfarm --forceterminate` issued successfully

One more correction from the newer active rerun:

- A later rerun on:
  - instance `i-0dbfa64b1edfad8a5`
  - private IP `192.168.1.123`
  - results dir `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--10-47-21-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
  initially looked stuck for a long time at:
  - `running /etc/init.d/S10mdev`
  - `Starting mdev: OK`
- During that silent window, `heartbeat.csv` still advanced from about `817` to `925`, so it was tempting to call it a boot hang.
- That would have been wrong:
  - the same live `uartlog` later advanced to
    - `running /etc/init.d/S40network`
    - `Starting network: OK`
    - `running /etc/init.d/S99run`
    - `launching firemarshal workload run/command`
  - and `heartbeat.csv` had continued to about `1093`
- Preserve this rule:
  - if the last visible line is only `Starting mdev: OK` and there is no explicit failure, do not terminate early just because the guest is silent for a long time
  - wait until the run either clearly reaches `[bertmini]` / `[prt-early]` / DMA logs, or shows a real fatal error

Newest live FPGA update after that:

- The same active rerun later advanced all the way through:
  - `artifacts validate end`
  - `init load-model-bin end elapsed_ms=783 size=17055744`
  - `init load-input-blob end elapsed_ms=5 size=65536`
  - `segment=0 begin`
  - `worker stage=0 ready`
  - the first `dma-submit`
  - `rr-acquire armed`
  - `dma-submit token=1 acquired cfg=0 manager=2 opcode=2`
- But the latest stable UART line did not move past DMA programming.
  It stopped earlier than the previous `before-set-src` baseline, and ended as a half-line:
  - `[prt-progress] dma-submit token=1 before-set-dst ... initial_wide_hi`
- While stuck there:
  - `uartlog` line count stayed fixed at `826`
  - `heartbeat.csv` kept advancing from about `1422` to `1491`
- Therefore the newest confirmed stop point is now:
  - during the `before-set-dst` progress log itself
  - not yet at `set_src done`
  - and not even a full `before-set-dst` line
- This is important because this rerun already had the `submit-fence begin/done` logs removed.
  So the current best interpretation is:
  - the remaining `before-set-dst` logging itself is still too intrusive
  - especially the combination of:
    - a very wide `PRT_PROGRESS_LOG(...)`
    - `fprintf/fflush`
    - `dma_debug_current_cpu()` calling `getcpu`
- The run farm for this newest rerun was terminated after confirming the stall:
  - instance `i-0dbfa64b1edfad8a5`
  - `terminaterunfarm` log `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--11-14-22-terminaterunfarm-P4UDIWS6XSQZWFUK.log`
  - EC2 state confirmed `shutting-down`

Newest update after removing `before-set-dst` and `getcpu`:

- A newer rerun rebuilt and installed the workload after removing:
  - the `before-set-dst` progress log
  - the adjacent `dma_debug_current_cpu()` / `getcpu`
- That rerun reached:
  - `artifacts validate end`
  - `init load-model-bin end`
  - `init load-input-blob end`
  - `segment=0 begin`
  - `worker stage=0 ready`
  - `dma-submit token=1 ... begin`
  - `rr-acquire armed ...`
- But the stable UART end became even earlier:
  - only a half-line:
    `[prt-progress] dma`
- Meanwhile:
  - `uartlog` line count stayed fixed at `825`
  - `heartbeat.csv` kept advancing from about `1288` to `1489`
- This interpretation should be stated as an inference from code order:
  - after `rr-acquire armed ...`, the next expected line in the current code is
    `dma-submit token=1 acquired cfg=0 manager=2 opcode=2`
  - so the freshest stop point is now most likely during the `acquired` log itself
  - not at `before-set-dst` anymore
- Therefore the next minimal experiment should shift again:
  - remove or drastically shrink the `acquired` log
  - keep `rr-acquire -> submit_fence -> set_dst -> set_src` as close as possible to the known-good Linux regressions
  - only after that rerun should you reinterpret the stall as a hardware-side `set_dst/set_src` issue
- This rerun was also terminated after confirming the stall:
  - instance `i-0ba7379b03ec31bb3`
  - `terminaterunfarm` log `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--12-10-05-terminaterunfarm-9PK9B1KG9WRVKZ4W.log`
  - EC2 state confirmed `shutting-down`

That "next minimal experiment" has now already been applied locally:

- `pipeline-runtime/src/prt_dma.c` no longer prints
  `dma-submit token=... acquired cfg=... manager=... opcode=...`
- local host build passed
- local RISC-V Linux cross-build passed
- but as of this handoff, that latest source change has NOT yet gone through a new
  `marshal build -> marshal install -> launchrunfarm -> infrasetup -> runworkload`
  cycle
- so the very next execution step is to rebuild/install and re-run on FPGA, then check whether the latest visible line finally moves past `acquired`

One more very important update from the newer, heavier-log rerun:

- A later live rerun again reached:
  - `rr-acquire armed`
  - `before-set-dst`
  - `submit-fence begin/done`
  - `set-dst done`
- But this time UART did not print a full `before-set-src` line.
- Instead the file ended stably with only:
  - `...[prt-prog`
- Meanwhile `heartbeat.csv` still advanced from about `1530s` to `1609s`.
- This means the newer extra logging is now intrusive enough that the physical latest UART stop point has moved earlier to:
  - immediately after `set-dst done`
  - during the next `[prt-progress] ...` write itself
- Do not forget the distinction:
  - the earlier, lower-intrusion rerun still says the semantic freshest stop point is `before-set-src`
  - the newest, higher-intrusion rerun says critical-path `stdout/fflush` is itself now a first-class suspect
- That newer rerun was also terminated after confirming the stall:
  - instance `i-03f2c024eac1a8850`
  - `terminaterunfarm --forceterminate` completed

Very important new instrumentation:

pipeline-runtime/src/main.c now emits early stderr writes:

- [prt-early] enter main
- [prt-early] live stdio ready
- [prt-early] defaults ready
- [prt-early] arg parse done
- [prt-early] calling runtime_init
- [prt-early] runtime_init done

These are write(2)-based and do not depend on stdio buffering.

Your first task is NOT to redesign semantics.
Your first task is to preserve and reason from the newest live FPGA evidence, whose last visible line is now `dma-submit token=1 before-set-src ...`.

Required next steps:

1. Source the correct environments:
   - source /home/ubuntu/chipyard/env.sh for FireMarshal
   - cd /home/ubuntu/chipyard/sims/firesim
   - source sourceme-manager.sh for FireSim manager
2. Constraint to preserve:
   - whenever you hit a Gemmini / DMA interface-call issue, first copy the calling pattern from the Linux regression tests instead of inferring from baremetal or inventing a new sequence
   - the three primary references are:
     - `rerocc_dma_matrix_linux_coupleddma.c`
     - `rerocc_lc_gemmini_matrix_linux_coupleddma.c`
     - `rerocc_lc_coverage_linux_coupleddma.c`
3. Preserve both pieces of 2026-03-19 evidence:
   - slow Linux boot is not by itself a blocker
   - the freshest true stop point is `before-set-src`
4. Focus on the first DMA submit path around `hw_dma_set_src()` first, then `hw_dma_fence()`.
5. If you re-run, still use the standard FireSim flow on f2.6xlarge.
6. Inspect uartlog and heartbeat.csv, not just manager exit code.
7. Specifically look for the new narrow log sequence:
   - `submit-fence begin`
   - `submit-fence done`
   - `set-dst done`
   - `before-set-src`
   - `set-src done`
   - `dma-wait fence-enter`
   - `dma-wait fence-done`
8. Also compare against the known-good Linux/coupleddma FPGA tests:
   - `rerocc_dma_matrix_linux_coupleddma.c`
   - `rerocc_lc_gemmini_matrix_linux_coupleddma.c`
   - `rerocc_lc_coverage_linux_coupleddma.c`
   - `rerocc_lc_nonblocking_linux_coupleddma.c`
9. Preserve the newest static conclusion from that comparison:
   - known-good Linux tests keep `rr_set_opc -> set_dst -> set_src` extremely clean
   - they do not place `printf/fflush` or `getcpu()` in that critical region
   - `GemminiCoupledDMA` `SRC_INFO` entry ready only depends on `copyReqQ.io.enq.ready`
   - therefore a stall exactly at `hw_dma_set_src(...)` is more likely ReRoCC client/manager-side ready gating than CoupledDMA refusing the command at its input
10. That minimal experiment has already been applied:
   - the `dma-submit token=... acquired ...` log has been removed from `prt_dma.c`
   - real-environment `marshal build` and `marshal install` for that source state both succeeded
   - a fresh FireSim rerun is already active
11. Resume from the current live rerun instead of starting over:
   - run host instance: `i-0f287e0d6f70d49ec`
   - private IP: `192.168.1.45`
   - current infrasetup exit code: `0`
   - current runworkload tmux session: `rerocc-prt-runworkload-20260319e`
   - current runworkload log:
     `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--12-39-50-runworkload-ZHSWWPICUGO8HY5O.log`
   - current result dir:
     `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--12-39-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
12. That live rerun has now advanced past Linux boot and then truly stuck:
   - it reached:
     - `segment=0 flush-spm-xlate end`
     - `segment=0 begin`
     - `worker stage=0 ready`
     - `dma-submit token=1 ... begin`
   - the final visible UART tail became the half-line:
     `[prt-progress] rr-acquire armed stage=0 manager=2 opcode`
   - `uartlog` stayed fixed at `824` lines while `heartbeat.csv` kept advancing from about `1834s` to `1903s`
   - that run was then terminated on purpose before analysis
13. Therefore the freshest stop point has moved again:
   - previously:
     full `rr-acquire armed ...` followed by half `[prt-progress] dma`
   - now:
     the half-line is on `rr-acquire armed ...` itself
   - this increases suspicion that the `rr-acquire armed` progress log is now the final intrusive log in the critical region
14. The corresponding run farm has already been reclaimed:
   - terminaterunfarm log:
     `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-15-29-terminaterunfarm-DR6TGLF9XXYYPRQS.log`
   - instance `i-0f287e0d6f70d49ec` was confirmed in `shutting-down`

Relevant files:

- FireMarshal source workload:
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json
- FireMarshal host-init:
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh
- Guest wrapper:
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
- Runtime config:
  /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml

Latest useful logs:

- Failing build that accidentally used PIPELINE_RUNTIME_PROGRESS=0:
  /home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-18--17-03-51-IDVF3J19O0XU6A43.log
- Latest successful infrasetup:
  /home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-18--17-11-01-infrasetup-7PXKKLO0N3L1QFC2.log
- Latest successful build with progress enabled:
  /home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-19--05-05-44-YOBJ7U7YQUN3WPLT.log
- Latest successful install:
  /home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-19--05-06-17-AWJC9LIQJLEF9NOU.log
- Current active runworkload log:
  /home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--12-39-50-runworkload-ZHSWWPICUGO8HY5O.log
- Current active live result dir:
  /home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--12-39-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime
- Current corresponding terminaterunfarm log:
  /home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-15-29-terminaterunfarm-DR6TGLF9XXYYPRQS.log

How to interpret the next FPGA run:

- If there is no [prt-early] at all:
  wrong image / stale install / wrong workload / wrong runtime config
- If it stops before [prt-early] runtime_init done:
  focus on main() earliest path / prt_runtime_init()
- If it reaches [prt-progress] init ready and segment=0 begin:
  the old early-startup blocker is gone
- If it again stops at dma-submit token=1 set-dst done:
  focus on the first DMA programming path, especially the misaligned `1B Get/Put` coupled-DMA path, not on YAML/model parsing
- If it reaches dma-submit token=1 before-set-src:
  the current blocker has moved forward
- If it reaches dma-wait begin and then hangs:
  focus on why `hw_dma_fence()` never observes coupled-DMA idle
- If the new logs disappear immediately after `set-dst done` even though submit-side logging was snapshotted:
  increase suspicion that the CPU or stack frame itself is being corrupted, not just the token's fields

What not to do next:

- Do not widen scope to resnet50 or multi-pipeline now.
- Do not implement multi-layer-stage semantics.
- Do not manually edit deploy/workloads.
- Do not claim success based on runworkload exit code alone.

Definition of progress for the next session:

- Either explain and fix why execution stops after dma-submit token=1 set-dst done,
  or move the last visible FPGA line past that point.
- Keep STATUS.md and PROCESS.md consistent with the exact latest finding.

Superseding 2026-03-19 13:35 UTC update:

- The active rerun described above is no longer active.
- A newer minimal rerun was completed after deleting the `rr-acquire armed ...`
  progress log from:
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c`
- That exact source state was rebuilt and installed successfully in the real
  environment:
  - build log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-19--13-21-57-NVD171NHBMBLGJCG.log`
  - install log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-19--13-22-18-CFMRC8TJZD1LH6KN.log`
- The newer launchrunfarm / infrasetup / runworkload were:
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-22-45-launchrunfarm-324FHKQLM414K16C.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-23-46-infrasetup-B0BNCLFZ7OACL35T.log`
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-28-38-runworkload-LXPXJP14YYNQCLV9.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--13-28-38-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
- That newest rerun did NOT reach the previous user-space stop point.
  Instead:
  - `uartlog` stopped in early Linux boot at `Initmem setup node 0 ...`
  - then simulator stdout printed:
    - `Simulator deadlock detected at target cycle 0. Terminating.`
    - `*** FAILED *** (code = 1) after 0 cycles`
- Important nuance:
  - the corresponding live `heartbeat.csv` had first advanced up to roughly
    `1421084536, 163`
  - then later became:
    - `0, 171`
    - `0, 176`
  - so the literal string `target cycle 0` should currently be interpreted as a
    late bridge/clock symptom after `tcycle` reset to zero, not as proof that
    the target never executed
- Therefore the current state is now:
  - previous useful DMA-critical-path hypothesis remains relevant
  - but there is an additional simulator-level symptom to analyze:
    `heartbeat` progress followed by `clock.tcycle() -> 0 -> deadlock`
  - this newest run does not directly confirm or refute the old
    `rr_acquire -> set_src` critical-region hypothesis because execution never
    got back to that observed stop point

Superseding 2026-03-19 later static update:

- The freshest high-confidence software-visible stop point is no longer
  `before-set-src`; it is:
  - visible:
    - `[prt-raw] dma post-fence`
    - `[prt-raw] dma post-dst`
  - missing:
    - `[prt-raw] dma post-src`
- Preserve this static conclusion:
  - in
    `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`
    the local DMA-side ready logic is:
    - `readyDest = isDest`
    - `readySrc = isSrc && copyReqQ.io.enq.ready`
  - so `DEST_INFO` being accepted does not imply `SRC_INFO` will be
    accepted, but the first sequential DMA request also does not naturally
    explain a permanently full `copyReqQ`
- A stronger current hypothesis comes from the ReRoCC front-end path:
  - `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/client/Client.scala`
  - `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/manager/Manager.scala`
  show that software returning from `set_dst` only proves the ReRoCC client
  accepted the instruction
  - it does NOT prove:
    - manager-side `sInstAck` has already returned
    - cfg credit is already replenished
    - the DMA RoCC itself has already consumed the command
  - therefore a stall inside `set_src` may still be upstream of
    `GemminiCoupledDMA`, in the ReRoCC client/manager ready/ack path
- The newest minimal diagnostic probe has already been added in:
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  between `post-dst` and `set_src`
  - new raw markers:
    - `[prt-raw] dma pre-mon`
    - `[prt-raw] dma post-mon`
  - the probe performs a read-only `FUNCT_READ_MONITOR(MON_VALID)`
- Interpret the next FPGA run like this:
  - if `post-mon` appears and `post-src` does not:
    the stop is more specific to `SRC_INFO` acceptance / `readySrc`
  - if UART stops after `post-dst` and before `post-mon`:
    the stop has likely moved earlier into the ReRoCC front-end or
    manager-side ack/response path
- Keep the existing user constraint:
  - when investigating Gemmini / DMA interface-call problems, prioritize the
    Linux coupleddma regression call pattern first
  - keep the critical `set_dst -> set_src` region as clean as possible except
    for the current minimal diagnostic probe

Newest live rerun after adding that monitor probe:

- The new probe source state has already been rebuilt and installed in the
  real environment:
  - build log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-19--16-59-09-GDTWENPF0C7E9BTS.log`
  - install log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-19--17-02-37-RUA0WG35Y310UDZA.log`
- The new real FireSim flow is:
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--17-03-05-launchrunfarm-JUAXXK2SV8B3P6M4.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--17-03-33-infrasetup-Y25803CDSP6PYJDL.log`
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--17-09-48-runworkload-0XQX5G0CG7TDUVB8.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--17-09-48-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
- Current run host:
  - instance `i-0d236b83fe1bfeea8`
  - private IP `192.168.1.75`
- At the latest check, local copied-back `uartlog` was not yet available, so
  the authoritative live evidence was the remote files:
  - `/home/ubuntu/sim_slot_0/uartlog`
  - `/home/ubuntu/sim_slot_0/heartbeat.csv`
- Latest live state from those remote files:
  - `uartlog` line count: `114`
  - stable last UART line:
    - `[    0.000000] software IO TLB: mapped [mem 0x00000000fbfff000-0x00000000fffff000] (64MB)`
  - `heartbeat.csv` still advanced to about:
    - `4269143560, 477`
- Preserve this interpretation:
  - this rerun has not yet re-entered `S99run` / `[bertmini]` / DMA logs
  - but it also has not yet shown a true deadlock signature
    such as `heartbeat -> 0` or `Simulator deadlock detected at target cycle 0`
  - therefore do not kill it just because it sits for a long time near
    `software IO TLB`
- The corresponding run farm has already been reclaimed:
  - instance: `i-0e0ef189cc43eac9e`
  - private IP: `192.168.1.138`
  - terminaterunfarm log:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-34-44-terminaterunfarm-G6GSWWFLULX9ARGN.log`
  - EC2 state was confirmed as `shutting-down`
- If you continue from here, do not restart from stale runworkload state.
  Resume from the newest facts above.
- Keep using the user-required environment rule exactly:
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh`
  - do not add `--skip-ssh-setup`

Superseding 2026-03-19 14:15 UTC update:

- A second same-image rerun was completed specifically to test whether the
  earlier Linux-boot `tcycle -> 0` deadlock was reproducible.
- That rerun used the exact same workload image and source state as the
  13:28 run, with no code change and no reinstall between them.
- Logs for that second rerun:
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-41-13-launchrunfarm-IL0IY3CCYU5G9DZV.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-41-52-infrasetup-IP4PRS4XMQUCHE9X.log`
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--13-47-50-runworkload-FRKDZBN8N50QLC4W.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--13-47-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
  - run host instance: `i-073b0c6f3ca6cd3b2`
  - run host private IP: `192.168.1.140`
- Most important outcome:
  - the earlier Linux-boot
    `Simulator deadlock detected at target cycle 0`
    did NOT reproduce
  - this second rerun advanced normally through:
    - `S99run`
    - `[bertmini]`
    - `[prt-early] runtime_init done`
    - `artifacts validate end`
    - `init load-model-bin end`
    - `init load-input-blob end`
    - `segment=0 begin`
    - `worker stage=0 ready`
- It then re-hit a stable user-space stop point:
  - live UART line count stayed fixed at `823`
  - the last stable UART line was the truncated first DMA-submit log:
    `[prt-progress] dma-submit token=1 stage=0 tensor=0 bytes=1024 src=0x104afe410 dst=0x40008400 src_acc=2 dst_acc=2 src_mod64=0x10 dst_mod64=0x00 initial_wide_hint=0 full_`
  - while `heartbeat.csv` advanced from about `1437s` to `1633s`
- This is now the highest-confidence current blocker.
- Important reinterpretation:
  - the 13:28 Linux-boot `tcycle -> 0` deadlock should currently be treated as
    an intermittent platform/bridge-side symptom, not the stable primary
    blocker for this workload image
  - the stable primary blocker has moved back to the first long
    `dma-submit ... begin` progress log itself
- Preserve one especially important static fact:
  - in
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    that first truncated `dma-submit ... begin` log executes before:
    - `prt_rr_acquire_scope(...)`
    - `hw_dma_submit_fence()`
    - `hw_dma_set_dst(...)`
    - `hw_dma_set_src(...)`
  - therefore this newest stable evidence strengthens suspicion that the
    remaining blocker is still the intrusive `PRT_PROGRESS_LOG(stdout + fflush)`
    path itself, not yet the actual DMA hardware programming instruction
- The run farm for this second rerun has already been reclaimed:
  - terminaterunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--14-15-32-terminaterunfarm-1QYMFSFUN07NUEOA.log`
  - instance `i-073b0c6f3ca6cd3b2` was confirmed as `shutting-down`
- That smallest source-level experiment has now already been applied locally:
  - in
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    the first long
    `dma-submit ... initial_wide_hint=... full_byte_mode_hint=... begin`
    progress log has been removed
  - local validation passed:
    - `make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all PIPELINE_RUNTIME_PROGRESS=1`
    - `make -C generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests -f generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc PIPELINE_RUNTIME_PROGRESS=1 rerocc_pipeline_runtime-linux`
- Therefore the next real experiment should not re-edit that area first.
  The next real experiment should be:
  - `marshal build`
  - `marshal install`
  - `launchrunfarm -> infrasetup -> runworkload`
  using this newest source state
- Only if that new image still hangs should you move on to the next remaining
  intrusive log or to deeper DMA/ReRoCC sequencing changes.

Superseding 2026-03-19 15:10 UTC update:

- A newer full real-environment cycle has already been completed after removing
  the first long `dma-submit ... begin` progress log:
  - build:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-19--14-27-13-19Z81FH5EL3S3N0G.log`
  - install:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-19--14-27-46-5PYW6NEFRNC0O4IT.log`
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--14-28-05-launchrunfarm-J3RLG9O7Q8MEB45D.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--14-28-30-infrasetup-EDLWABEDWLAR9JHQ.log`
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--14-35-31-runworkload-DR6N3HI83I2HRX1R.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--14-35-31-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
  - terminaterunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-02-23-terminaterunfarm-USLLUV2327W1KXAS.log`
- Most important outcome from that rerun:
  - the earlier Linux-boot
    `Simulator deadlock detected at target cycle 0`
    did not reproduce
  - guest execution advanced much farther and reached:
    - `S99run`
    - `[bertmini]`
    - `[prt-early] runtime_init done`
    - `artifacts validate end`
    - `init load-model-bin end`
    - `init load-input-blob end`
    - `init ready`
    - `segment=0 begin`
    - `worker stage=0 ready`
- The stall also moved forward:
  - `uartlog` line count stayed fixed at `823`
  - `heartbeat.csv` kept advancing from about `1356s` to `1560s`
  - the last stable UART line ended at:
    `worker stage=0 ready ... dma=2 tiles=2`
- Preserve this interpretation:
  - the deleted first long `dma-submit ... begin` log really was part of the
    stable blocker
  - but after removing it, the run still hangs later, now somewhere after
    `worker stage=0 ready` and before the next visible DMA-path log
  - therefore the current best narrowing target is the region around:
    - `prt_rr_acquire_scope(...)`
    - then acquire-success to DMA-programming preparation
    - then `submit_fence/set_dst/set_src`
- A newer minimal source patch has already been applied locally to narrow that
  gap with very short raw stderr markers:
  - in
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_progress.h`
    added:
    - `PRT_PROGRESS_RAW_LINE(...)`
      implemented with `write(STDERR_FILENO, ...)`
  - in
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    added:
    - `[prt-raw] dma pre-acquire`
    - `[prt-raw] dma acquire-ok`
    - `[prt-raw] dma pre-program`
  - and removed the CSR debug reads between `set_dst` and `set_src`, keeping the
    critical call sequence closer to the Linux coupled-DMA regressions
- That newest raw-marker source state has now been locally validated:
  - host build passed:
    `make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all PIPELINE_RUNTIME_PROGRESS=1`
  - RISC-V Linux cross-build passed:
    `make -C generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests -f generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc PIPELINE_RUNTIME_PROGRESS=1 rerocc_pipeline_runtime-linux`
- But that newest source state has NOT yet gone through a new real-environment:
  - `marshal build`
  - `marshal install`
  - `launchrunfarm -> infrasetup -> runworkload`
- Therefore the next exact step is:
  - `source /home/ubuntu/chipyard/env.sh`
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh`
  - then rebuild/reinstall and rerun FPGA with this raw-marker source state

Superseding 2026-03-19 15:55 UTC update:

- That raw-marker source state has now been fully rebuilt, installed, run on
  FPGA, and the run farm has already been reclaimed:
  - build:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-19--15-09-48-13N9052K6C0HBD89.log`
  - install:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-19--15-10-20-TM7GIZPOADVIYWV9.log`
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-13-38-launchrunfarm-FV8FKGPEQGL344T6.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-14-03-infrasetup-RCD8RTC3UME6FFWB.log`
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-24-42-runworkload-7N0BWOO04AG25KVE.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--15-24-42-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
  - terminaterunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-52-24-terminaterunfarm-WAHTOWWR7DVQMI2U.log`
  - run host:
    - instance `i-0e331841de6639b5e`
    - private ip `192.168.1.125`
    - later confirmed `shutting-down`
- This rerun again proved that long Linux silence is not the blocker:
  - it advanced through:
    - `S40network`
    - `S99run`
    - `[bertmini]`
    - `[prt-early] runtime_init done`
    - `artifacts validate end`
    - `init load-model-bin end`
    - `init load-input-blob end`
    - `init ready`
    - `segment=0 begin`
    - `worker stage=0 ready`
- Most important new FPGA evidence:
  - the new raw markers really appeared:
    - `[prt-raw] dma pre-acquire`
    - `[prt-raw] dma acquire-ok`
    - `[prt-raw] dma pre-program`
  - after that, UART never advanced again
- Stable-stuck confirmation:
  - `uartlog` line count stayed fixed at `826`
  - the final stable tail was:
    - `worker stage=0 ready ... dma=2 tiles=2`
    - `[prt-raw] dma pre-acquire`
    - `[prt-raw] dma acquire-ok`
    - `[prt-raw] dma pre-program`
  - while `heartbeat.csv` kept advancing from about `1487s` to `1635s`
- Preserve this interpretation:
  - `prt_rr_acquire_scope(...)` definitely returned successfully
  - the stall is no longer plausibly before or inside acquire
  - the current stop point is now narrowed to the tiny critical region after
    `dma pre-program`, i.e. around:
    - `hw_dma_submit_fence()`
    - `hw_dma_set_dst(...)`
    - `hw_dma_set_src(...)`
  - with the current markers, you still cannot distinguish exactly which of
    those three instructions (or their immediate hardware interaction) is the
    first stuck point
- Therefore the next minimal experiment should NOT widen scope.
  It should only add the narrowest possible raw markers in that exact region:
  - `post-fence`
  - `post-set-dst`
  - `post-set-src`
- Keep the existing discipline:
  - do not reintroduce long `stdout/fflush` logs there
  - do not add syscall-heavy debug in that critical section
  - continue to copy the Linux coupleddma regression call pattern first

Superseding 2026-03-19 16:00 UTC update:

- That next minimal patch has already been applied locally in:
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  with these changes:
  - keep:
    - `dma pre-acquire`
    - `dma acquire-ok`
    - `dma pre-program`
  - add:
    - `dma post-fence`
    - `dma post-dst`
    - `dma post-src`
  - delete the long `set-src done ...` stdout progress log to avoid it becoming
    the next instrumentation-induced stop point
- Local validation for that source state already passed:
  - host build:
    `make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all PIPELINE_RUNTIME_PROGRESS=1`
  - RISC-V Linux cross-build:
    `make -C generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests -f generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc PIPELINE_RUNTIME_PROGRESS=1 rerocc_pipeline_runtime-linux`
- That newer source state has also already gone through a new real-environment:
  - build:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-19--15-55-36-Y5OMV91059DLZSK5.log`
  - install:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-19--15-56-24-UNUJCBBA4X2AHP5S.log`
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-57-08-launchrunfarm-45KKIYBY5IVPEJ18.log`
- The next run is already active enough to matter:
  - current run host instance: `i-09f9a88800f991a35`
  - private ip: `192.168.1.59`
  - current active infrasetup tmux session:
    `rerocc-prt-infrasetup-20260319g`
  - current infrasetup log:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--15-58-23-infrasetup-K2F5KHCAAYTX2PBM.log`
- Therefore if you resume later, do not restart from the older `dma pre-program`
  run. Resume from this newer post-fence/post-dst/post-src experiment first.

Superseding 2026-03-19 16:45 UTC update:

- That newer post-fence/post-dst/post-src experiment has now already run far
  enough to produce the next narrowing result, and the run farm has already been
  terminated:
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--16-06-42-runworkload-P6ND1HPK9IA5W0M6.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--16-06-42-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
  - terminaterunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--16-42-09-terminaterunfarm-EGMWN4IFFP84XP2S.log`
  - run host:
    - instance `i-09f9a88800f991a35`
    - private ip `192.168.1.59`
    - later confirmed `shutting-down`
- This run again advanced normally through:
  - `S99run`
  - `[bertmini]`
  - `[prt-early] runtime_init done`
  - `segment=0 begin`
  - `worker stage=0 ready`
- Most important new evidence:
  - visible:
    - `[prt-raw] dma pre-acquire`
    - `[prt-raw] dma acquire-ok`
    - `[prt-raw] dma pre-program`
    - `[prt-raw] dma post-fence`
    - `[prt-raw] dma post-dst`
  - still missing:
    - `[prt-raw] dma post-src`
- Stable-stuck confirmation:
  - `uartlog` line count stayed fixed at `828`
  - while `heartbeat.csv` advanced from about `1939s` to `2038s`
- Preserve this newest interpretation:
  - `hw_dma_submit_fence()` has returned
  - `hw_dma_set_dst(...)` has returned
  - the current freshest stop point is now at:
    - `hw_dma_set_src(...)` itself
    - or the immediate hardware interaction triggered by `set_src`
  - this is now stronger than the older `pre-program` baseline, so do not
    restart analysis from `submit_fence` or `set_dst` as if they were still the
    primary unknowns

Superseding 2026-03-19 16:45 UTC update:

- The follow-up no-`post-mon` experiment has now been restarted in the real
  environment and is currently still active:
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--17-46-54-launchrunfarm-ALOWOP3N81O133J8.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--17-52-36-infrasetup-BF0WRZ3JCQGAAS0B.log`
    - tmux session:
      `rerocc-prt-infrasetup-20260319-nopostmon2`
    - wrapper exit code:
      `0`
  - current runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--18-10-53-runworkload-0BPLLUCO4SR97OVI.log`
  - current results dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-19--18-10-53-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-f2-rerocc-linux-bertmini-pipeline-runtime`
  - current tmux session:
    `rerocc-prt-runworkload-20260319-nopostmon2`
  - current run host:
    - instance `i-008177957ef9d1d40`
    - private ip `192.168.1.66`
- Live state as of about 2026-03-19 18:21 UTC:
  - `runworkload` is still running
  - manager still reports:
    - `1/1 simulations are still running`
  - remote `/home/ubuntu/sim_slot_0/uartlog` has advanced beyond the earliest
    kernel lines:
    - line count reached about `217`
    - visible examples now include:
      - `smp: Brought up 1 node, 2 CPUs`
      - `printk: console [ttySIF0] enabled`
      - `NET: Registered PF_INET6 protocol family`
      - `9pnet: Installing 9P2000 support`
  - remote `/home/ubuntu/sim_slot_0/heartbeat.csv` is still advancing:
    - about `5111228308, 565`
- Preserve this interpretation:
  - this run has NOT yet reached user-space
  - it has NOT yet reached:
    - `S99run`
    - `[bertmini]`
    - `[prt-early]`
    - any `[prt-raw] dma ...` markers
  - therefore, do NOT declare this new run stuck yet
  - treat it as an in-progress Linux boot until UART line count actually
    stabilizes while heartbeat continues, or until it reaches the pipeline
    runtime markers
- Immediate next step from this point:
  - keep monitoring:
    - `/home/ubuntu/sim_slot_0/uartlog`
    - `/home/ubuntu/sim_slot_0/heartbeat.csv`
  - wait until the run either:
    - reaches user-space and then the `dma pre-/post-*` markers
    - or satisfies the user's true-stuck criterion

Superseding 2026-03-19 18:21 UTC update:

- That restarted no-`post-mon` run has now already advanced all the way back to
  the first DMA submit path, then truly stuck, and the run farm has already
  been reclaimed:
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--18-10-53-runworkload-0BPLLUCO4SR97OVI.log`
  - terminaterunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-19--18-39-10-terminaterunfarm-8AEEGUDJH4MUAF02.log`
  - terminaterunfarm tmux session:
    `rerocc-prt-terminaterunfarm-20260319-nopostmon2`
    - wrapper exit code:
      `0`
  - run host:
    - instance `i-008177957ef9d1d40`
    - private ip `192.168.1.66`
    - later confirmed `shutting-down`
- This run again advanced normally through:
  - Linux boot
  - artifact validation
  - model/input loading
  - `segment=0 begin`
  - `worker stage=0 ready`
- Most important new evidence from this run:
  - visible:
    - `[prt-raw] dma pre-acquire`
    - `[prt-raw] dma acquire-ok`
    - `[prt-raw] dma pre-program`
    - `[prt-raw] dma post-fence`
    - `[prt-raw] dma post-dst`
    - `[prt-raw] dma pre-mon`
  - still missing:
    - `[prt-raw] dma post-src`
- Stable-stuck confirmation:
  - remote `uartlog` line count stayed fixed at `829`
    across two checks separated by about 90 seconds
  - while remote `heartbeat.csv` advanced from about:
    - `14547403827, 1547`
    to:
    - `15727096512, 1665`
- Preserve this interpretation carefully:
  - this is NOT evidence that the stop point truly moved earlier than before
  - in the no-`post-mon` source state, the marker immediately after
    `hw_dma_read_monitor(...)` was intentionally removed
  - therefore a final visible stop at:
    - `[prt-raw] dma pre-mon`
    only narrows the active window to:
    - `hw_dma_read_monitor(DMA_MON_VALID)`
    - `hw_dma_set_src(...)`
    - or the immediate hardware interaction after them
- But this run does provide an important new conclusion:
  - removing the final `post-mon` raw `write()` did NOT prevent the real FPGA
    hang
  - so that `post-mon` syscall is not the sole root cause
- Combined with the previous `post-mon visible / post-src missing` run, the
  strongest current interpretation remains:
  - monitor read had already succeeded in the earlier run
  - removing `post-mon` still leaves the system truly hung
  - therefore the main suspect should stay on:
    - `hw_dma_set_src(...)`
    - Coupled-DMA `SRC_INFO` acceptance / `copyReqQ.io.enq.ready`
    - or ReRoCC client-side ready gating right at `set_src`
- Immediate next step for the next session:
  - do NOT spend more cycles on proving that `post-mon` logging alone causes
    the hang
  - instead narrow the real `set_src` path itself, while still following the
    user constraint to prioritize Linux coupleddma regression call patterns

Superseding 2026-03-19 18:40 UTC update:

- Preserve this newer static interpretation:
  - stop framing the issue as just a `post-mon` logging problem
  - the more realistic active window is now:
    - `hw_dma_read_monitor(DMA_MON_VALID)`
    - `hw_dma_set_src(...)`
    - or the immediate hardware interaction after them
- Why this matters:
  - in
    `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`
    `monitor read` is not a passive no-op; it is a response-producing command:
    - `readyMonitor = isMonitor && !respValid`
    - `acceptMonitor` sets:
      - `respValid := true`
      - `respData := monitorData`
  - `set_src` remains the asymmetric command:
    - `readySrc = isSrc && copyReqQ.io.enq.ready`
    - actual copy request enqueue happens on `acceptSrc`
- Cross-layer implication:
  - `monitor read` goes through the ReRoCC writeback path:
    - manager `sWrite` packaging in
      `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/manager/Manager.scala`
    - client response reconstruction in
      `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/client/Client.scala`
  - so the current `pipeline-runtime` critical region is effectively:
    - `set_dst`
    - a response-generating monitor instruction
    - `set_src`
  - that is materially different from the known-good Linux coupleddma tests
- Most important comparison to preserve:
  - the Linux coupleddma regressions the user wants prioritized keep the path
    very clean:
    - `fence -> set_dst -> set_src -> wait`
  - they do not insert `read_monitor` between `set_dst` and `set_src`
- Therefore the next session should prioritize this experiment first:
  - remove or move
    `hw_dma_read_monitor(DMA_MON_VALID)`
    out of the `set_dst -> set_src` critical region
  - keep the sequence as close as possible to the Linux coupleddma regressions:
    - `rr_acquire -> rr_set_opc -> fence -> set_dst -> set_src`
  - then rerun the full real-environment flow
- Keep the weaker explanations downgraded unless new evidence appears:
  - plain queue-depth exhaustion is not the best first explanation
    because current static defaults are not tiny:
    - CoupledDMA `copy_queue_depth = 8`
    - ReRoCC `IBufEntries = 4`

2026-03-20 local implementation update:

- The source tree has moved forward again since the last validated FPGA image.
  Preserve this source/image skew explicitly:
  - current source
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    no longer issues `hw_dma_read_monitor(DMA_MON_VALID)` in the critical region
  - but the last rebuilt Linux binary and staged overlay binary are still from around
    `2026-03-19 17:44 UTC`
  - those binaries still contain:
    - `[prt-raw] dma pre-mon`
  - therefore the last *validated* runtime/image baseline is still the
    `2026-03-19--18-10-53` no-`post-mon` run, not the current working-tree source

- A new software workaround has now been implemented locally for Linux host<->SPM DMA:
  - the runtime now has per-stage host-page bounce buffers
  - when a host<->SPM DMA chunk has:
    - `bytes >= 64`
    - and `src_mod64 != dst_mod64`
    it no longer submits the original host pointer directly
  - instead it stages through the bounce page at an offset whose `mod64` matches the
    SPM-side address
  - this targets the exact suspected first bertmini request shape:
    - `src offset = 0x10`
    - `dst offset = 0x00`
    - `bytes = 1024`

- A matching Linux coupleddma regression case has also been added locally:
  - file:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/rerocc_lc_coverage_linux_coupleddma.c`
  - case name:
    `dma_dram_to_shared_misaligned_fullpage`
  - shape:
    - DRAM source offset `0x10`
    - shared-SPM destination offset `0x00`
    - `1024B`

- Important: these 2026-03-20 source changes have NOT yet been rebuilt into:
  - `rerocc_pipeline_runtime-linux`
  - FireMarshal image
  - FireSim F2 replay

- Therefore the next session must start with rebuild + replay, not more stale-log theorizing.
  Required order:
  1. rebuild the Linux runtime binary from the current source
  2. verify the rebuilt binary:
     - still contains `[prt-raw] dma post-src`
     - no longer contains `[prt-raw] dma pre-mon`
  3. rebuild/install the workload image in the real environment
  4. run the small Linux coupleddma regression image first as a fast gate
  5. only then rerun the bertmini dedicated F2 workload

- Interpretation rule for the next fresh FPGA evidence:
  - if the monitor-free + bounce-buffer build finally reaches `[prt-raw] dma post-src`,
    move the blocker forward to wait/completion
  - if it still truly hangs before `post-src`, the software alignment workaround was
    insufficient and the next narrowing step should move to hardware-side `SRC_INFO`
    ready/credit/ack behavior

2026-03-20 implementation + replay update:

- The local rebuild/replay step that was previously pending has now been partially completed:
  - host `pipeline-runtime` rebuilt successfully
  - `rerocc_pipeline_runtime-linux` rebuilt successfully from current source
  - rebuilt runtime binary verification now passes:
    - still contains `[prt-raw] dma post-src`
    - no longer contains `[prt-raw] dma pre-mon`
  - rebuilt Linux coupleddma coverage binary also contains the new
    `dma_dram_to_shared_misaligned_fullpage` case

- FireMarshal images were also rebuilt/installed from this newer source state:
  - small regression image:
    - build log:
      `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-build-2026-03-20--02-30-01-E095QTWN9EB8ENBQ.log`
    - install log:
      `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-install-2026-03-20--02-31-14-KIYWOOOTTC9A4CPS.log`
  - bertmini image:
    - build log:
      `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-build-2026-03-20--02-41-58-S0XNWQHZJA1NWBGR.log`
    - install log:
      `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-install-2026-03-20--02-57-22-TDWAI9N5HQDTPU6D.log`

- The first fresh FPGA run of the small regression image did *not* provide a clean DMA gate yet.
  It exposed a separate workload packaging bug first:
  - launchrunfarm:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--02-31-56-launchrunfarm-X3UB9V1G86R0DSX8.log`
  - infrasetup:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--02-32-20-infrasetup-UPK8QF920C97JJAM.log`
  - runworkload:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--02-35-32-runworkload-2QNPQVPJ2JLDYBCY.log`
  - result dir:
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-20--02-35-32-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles`
  - guest progressed all the way into:
    - `running /etc/init.d/S99run`
    - `launching firemarshal workload run/command`
    - `[rerocc-lc-linux-regression] ...`
  - but then failed with:
    - missing `/root/rerocc-linux-tests-coupleddma/rerocc_lc_matrix_linux_coupleddma_verify-linux`
    - missing `/root/rerocc-linux-tests-coupleddma/rerocc_lc_coverage_linux_coupleddma-linux`
    - missing `/root/rerocc-linux-tests-coupleddma/rerocc_lc_nonblocking_linux_coupleddma-linux`
    - `ALL_TESTS_FAIL matrix_ret=127 coverage_ret=127 nonblocking_ret=127`
  - so that run was invalid as a DMA-behavior verdict

- Root cause of that regression-image failure is now understood and fixed locally:
  - file:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh`
  - bug:
    `build_linux_binaries()` emitted the coupleddma executables into
    `overlay/root/rerocc-linux-tests-coupleddma/`,
    but `stage_coupleddma_overlay()` then deleted that same directory with `rm -rf`,
    removing the freshly built binaries before image packaging
  - fix:
    preserve the overlay directory contents produced by `build_linux_binaries()`,
    and only copy the extra generic binaries + wrapper script on top
  - after this fix, local overlay verification shows the required binaries are present:
    - `rerocc_lc_matrix_linux_coupleddma_verify-linux`
    - `rerocc_lc_coverage_linux_coupleddma-linux`
    - `rerocc_lc_nonblocking_linux_coupleddma-linux`

- A corrected small regression image has already been rebuilt/reinstalled after that overlay fix:
  - build log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-build-2026-03-20--03-01-54-85CHWK0F0AHOYDW7.log`
  - install log:
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-install-2026-03-20--03-02-41-ZLKA3NPJSAOTUCJG.log`

- However, the *current* blocker has now moved again.
  It is no longer the stale source/image skew, and no longer the missing overlay binaries.
  It is now run-farm launch itself:
  - current tmux session:
    `regression-pipelinefiles-launch-r2`
  - current launchrunfarm log:
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-03-20--03-02-52-launchrunfarm-U5Y12CNH5R4OMD91.log`
  - no new F2 instance has launched yet
  - current errors include:
    - `VcpuLimitExceeded`
    - `f2.6xlarge` unsupported in `us-west-2a`
    - `f2.6xlarge` unsupported in `us-west-2d`
  - `aws ec2 describe-instances` currently shows only the manager itself running:
    - `i-08b9950158e875a41`
    - `c5.2xlarge`
    - `firesim-manager-wzy`

- Therefore the next session should *not* restart from old DMA hypotheses.
  Resume from this exact state:
  1. inspect whether `regression-pipelinefiles-launch-r2` has finally acquired an `f2.6xlarge`
  2. if launch succeeds, continue the corrected small regression sequence:
     - `infrasetup`
     - `runworkload`
     - verify whether the guest now executes the coverage binary and whether
       `dma_dram_to_shared_misaligned_fullpage` passes or fails
  3. if launch keeps failing, treat AWS F-instance quota/capacity as the live blocker and
     resolve that before touching DMA code again
  4. only after the corrected small regression gives a real software/DMA verdict,
     run the already rebuilt bertmini dedicated image
