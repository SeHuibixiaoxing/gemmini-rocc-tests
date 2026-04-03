# Next Session Prompt

```text
You are taking over pipeline-runtime work in:

/home/ubuntu/chipyard

Read these first, in order:

1. /home/ubuntu/chipyard/AGENTS.md
2. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/README.md
3. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/CURRENT_STATUS.md
4. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/blockers_and_lessons.md
5. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/DECISIONS.md

Current frozen state as of 2026-04-02:

- The current bertmini mainline on FireSim Linux/F2 no longer hangs.
- The latest decisive run completed through segment 31, powered off cleanly, and copied logs back.
- The current mainline failure is:
  golden mismatch on tensor 48
- That mismatch investigation is intentionally deferred for now.

Decisive evidence:

- runtime config:
  /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_reloadlogs.yaml
- hwdb:
  /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb.yaml
- build recipe:
  /home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_coupleddma_10mhz.yaml
- result dir:
  /home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/
- key runtime log facts:
  - segment=31 threaded backend complete target_subbatch=8
  - golden mismatch: tensor=48 bytes=65536 mismatch=1823 first_idx=12 actual=8 golden=119 max_abs=135
  - runtime_run failed: mismatch (-13)
- key status facts:
  - state=finished
  - exit_code=1
  - uart_log_enable=0
  - guest_log_enable=1
  - guest_deep_log_enable=1
- key uart facts:
  - Simulation complete.
  - *** PASSED *** after 44898716942 cycles
  - Script done on 2026-04-02 12:31:53+00:00 [COMMAND_EXIT_CODE="0"]

Interpretation you must preserve:

- Do not rewrite the current story back into "DMA hang" unless a new fresh run clearly regresses from completion back to hang.
- Do not rewrite the current story into "UART issue" unless a new fresh run proves the file-only mainline has regressed.
- The correct current description is:
  bertmini mainline completes, but the current CPU-derived golden disagrees at tensor 48.

Hard constraints:

- Use only f2.6xlarge.
- Use only FireSim manager flow:
  launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm
- Run FireSim manager commands only through:
  /home/ubuntu/chipyard/scripts/firesim-tmux-run.sh
- Run FireMarshal only through:
  /home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh
- Before every infrasetup or runworkload, first verify the workload image/rootfs is the freshly rebuilt one.
  At minimum compare:
  - workload json common_bootbinary/common_rootfs
  - latest marshal build/install logs
  - image path + mtime + size
  Prefer adding sha256sum.
- After any image/rootfs/binary/AGFI change, rerun infrasetup before runworkload.
- If a run is interrupted or times out, rerun infrasetup before the next runworkload.
- After each run finishes, fails, or is manually interrupted, terminate the run farm promptly and verify EC2 state until no instance remains running.
- Stay in Chinese.

Logging policy you must preserve:

- Keep Linux boot logs on UART.
- Keep bin/runtime logs in guest files, not UART.
- Coarse log:
  /root/pipeline-runtime-debug/bertmini-batch8.log
- Fine log:
  /root/pipeline-runtime-debug/bertmini-batch8.deep.log
- Fine log can be gated by segment/stage/subbatch using:
  --deep-log-segment
  --deep-log-global-stage
  --deep-log-local-stage
  --deep-log-subbatch
  --deep-log-stage-radius
  --deep-log-subbatch-radius
- If you need more observability, prefer file-log gating over adding more UART/stdout output.

Stable technical facts:

- default_hw_config:
  firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz
- AGFI:
  agfi-06eb561d00d5c5dc1
- TARGET_CONFIG:
  GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA
- Gemmini datatype baseline:
  - inputType = SInt(8.W)
  - accType = SInt(32.W)
  - spatialArrayOutputType = SInt(20.W)
- Convolution out-of-bounds behavior is zero padding, not edge clamp.
- With spm_xlate_enable=1, the runtime forces blocking_debug sync mode, so the current scene is effectively blocking for DMA/Gemmini.
- Important implementation debt to preserve:
  the current runtime `num_cores` is not a clean "CPU hart count" parameter.
  It is currently overloaded to mean:
  - CPU/hart-related bound
  - accelerator slot / page-domain count
  - page allocator scale via `num_cores * pages_per_acc`
  That is why the code currently forces:
  `num_cores >= num_gemmini_mgrs`
  This is implementation residue, not a frozen architectural requirement.
  Do not reinterpret it as:
  "CPU count must equal or exceed Gemmini count by design."
  If this line is revisited later, the right direction is to split:
  - `num_cpu_harts`
  - `num_acc_slots/page_domains`
  - `num_gemmini_mgrs`
  - `num_dma_mgrs`
  rather than keeping them coupled through one field.

Important warning about the current golden:

- The current runtime artifact manifest is still mode: fresh.
- The fresh export path regenerates runtime_model.bin and runtime_input.bin via the dummy runtime data path.
- The current golden.*.bin files come from host closure using the CPU backend.
- The runtime still contains temporary hard-coded semantics:
  - conv activation = RELU
  - conv output scale = 1.0
  - resadd A/B/C_scale = 1.0
  - resadd relu = 0
- Therefore:
  the current mismatch is real, but it is not yet absolute proof of an RTL bug.

Stable workloads to keep:

- Mainline regression:
  rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync
- Small Linux smoke:
  /home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-20--06-16-29-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles/
  with markers:
  - DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024
  - CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS
  - SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1
  - NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1
  - ALL_TESTS_PASS
  - COMMAND_EXIT_CODE="0"

Old blocker retained only as regression signature:

- Historical hang capture:
  /home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/bertmini-b8-fileonly-sync6-run-manualmon3-20260401-192.168.1.44-host-watchdog-20260401T152744Z.guest-deep-log.txt
- Historical frontier:
  segment=3 stage=0 tensor=6 page=124 ... submit-begin -> [prt-marker] dma
- Reopen this line only if a fresh run regresses from completion back to hang.

If mismatch work is explicitly resumed later, do it in this order:

1. Fix the current host pipeline_runtime full-build blockers.
2. Regenerate golden.*.bin with host closure.
3. Rebuild and reinstall the Linux workload image.
4. Re-verify image/rootfs freshness.
5. Run FireSim again with the frozen workload above.
6. Terminate the run farm immediately after collecting evidence.
```
