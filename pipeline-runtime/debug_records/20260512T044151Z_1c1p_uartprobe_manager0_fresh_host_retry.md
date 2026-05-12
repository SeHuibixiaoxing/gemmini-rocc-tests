# 20260512T044151Z 1C1P uartprobe manager0 fresh-host retry

## Scope

- Target: 1C1P pair-manager hwdebug Linux uartprobe on FireSim F2.
- Runtime config:
  `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug_uartprobe_nodebug.yaml`
- HWDB:
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
- Build recipes:
  `config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
- AGFI under test: `agfi-098bce7d5e0c3d937`

## Constraints reread

- Re-read `pipeline-runtime/README.md`,
  `pipeline-runtime/docs/constraints/hard_constraints.md`, and
  `pipeline-runtime/docs/testing/pipeline_runtime_optimization_actions_20260505_draft.md`.
- FireMarshal and FireSim commands must use the repository tmux wrappers.
- FireSim run order remains `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`.
- Freshness must be checked before `infrasetup` / `runworkload`.
- F2 instances must be reclaimed promptly after live inspection.
- `doneflag` remains invalid as DMA completion evidence.

## Static conclusion

- 1C1P pair-manager exposes a single manager id, `0`.
- The pair wrapper uses instruction/custom encoding to distinguish DMA and Gemmini behavior; it does not expose separate logical manager ids `0` and `1` for one pair.
- Therefore the 1C1P Linux uartprobe wrapper must default:
  - `DMA_BASE_ID=0`
  - `UARTPROBE_DMA_MANAGER_ID=0`
- The prior F2 result at
  `sims/firesim/deploy/results-workload/2026-05-12--04-04-56-rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug-f2-rerocc-linux-uartprobe-1c1p1-hwdebug-nodebug/`
  failed before DMA issue:
  - `binary_stage_last=seed-failed`
  - `uartprobe.deep.log` showed `state=before-acquire ... dma_mgr=1`
  - `acquire timeout cfg=1 dma_mgr=1`

## Freshness already established before this retry

- FireMarshal clean/build/install completed through
  `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`.
- Local guest image `/firemarshal.sh` was verified with `debugfs` and contains manager 0 defaults.
- Local image SHA:
  `2bdf904ba624492162ecce9684262759a2bf7f05db73107cc324f19ae7fe1e40`
- Previous remote host `192.168.1.230` had matching driver and image SHA, and guest
  `/firemarshal.sh` also contained manager 0 defaults.

## Current F2 slot event

- Previous runfarm instance:
  - instance: `i-050f9c3167a2bf171`
  - private IP: `192.168.1.230`
  - cluster tag: `firesim-1c1p1-hwdebug-linux-nodebug`
- `infrasetup` failed in driver readiness preflight, before guest/runtime execution:
  - `FireSim fingerprint sample[00]: init=0x00000001 presence=0x00000001 write_shadow=0x00000001`
  - `FireSim fingerprint: 0x1`
  - `Invalid FireSim fingerprint`
- This is not evidence of a pipeline-runtime or manager-id runtime failure.
- The bad host was terminated using:
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh --session-name hwdebug-1c1p-uartprobe-nodebug-terminate-badslot-20260512 terminaterunfarm --forceterminate ...`
- Termination exit code: `0`.
- EC2 state after termination request: `shutting-down`.

## Fresh host retry

- Started:
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh --session-name hwdebug-1c1p-uartprobe-nodebug-launch-fresh-manager0-20260512 launchrunfarm ...`
- At record time, AWS had not allocated a fresh `f2.6xlarge`; FireSim was retrying due to insufficient F2 capacity.

## Next checks

1. When a fresh F2 is allocated, run `infrasetup` through `firesim-tmux-run.sh`.
2. Verify remote freshness on the run host:
   - driver SHA
   - guest image SHA
   - guest `/firemarshal.sh` from the image via `debugfs`
3. Run workload through `firesim-tmux-run.sh`.
4. Inspect `uartprobe.status`, `uartprobe.binary.stage`, `uartprobe.deep.log`,
   `uartprobe.runner.stage`, and `uartlog`.
5. Terminate the runfarm after artifact collection / live inspection.


## 2026-05-12 follow-up: fingerprint/init-wait failure

Fresh-host retry allocated `i-03b104f4eb973933a` / `192.168.1.13` and loaded
`agfi-098bce7d5e0c3d937`. The first `infrasetup` used a temporary host-driver
instrumentation patch in `master.cc` and failed in readiness preflight with
`init=presence=write_shadow=0x1`; that patch was removed before the second
`infrasetup`.

After removing the extra `INIT_DONE` / `PRESENCE_WRITE` sampling, the second
`infrasetup` still failed before guest execution. A manual remote run of:

```text
sudo ./FireSim-f2 ... +check-fingerprint
```

printed `entered simulation flow execution` but never printed `finished waiting`
before the 30s timeout. Therefore the current failing point is
`simulation_t::wait_for_init()` waiting for `SimulationMaster.INIT_DONE`, not the
Linux workload, not uartprobe input, and not the 1C1P DMA manager-id code path.

Hardware/shell status collected with `sudo fpga-describe-local-image -S 0 -M` on
that host showed the AGFI loaded and no AWS shell timeout/error counters:
`ocl-slave-timeout=0`, `dma-pcis-timeout=0`, `pcim-*error=0`. This means the AWS
shell did not report an OCL slave timeout for the failed preflight, but the
FireSim driver still did not observe `INIT_DONE`.

Local collateral audit:

- Current `FireSim-generated.sv` SHA matches the build-result copy used for the
  AGFI bitstream:
  `d92ed1e2691cae83dfcef7eabb9d37b664e07ee45499302baa4e31226980a1b3`.
- The `SimulationMaster` MCR layout in current generated const is:
  - `INIT_DONE = 9376`
  - `PRESENCE_READ = 9380`
  - `PRESENCE_WRITE = 9384`
- Current nodebug driver SHA after removing temporary instrumentation:
  `6dd4556587ef7b46160131d1ec92343ec51d4a8bda81fe8b3eaea2d142b0e90c`.
- Historical `2026-05-12--02-38-37-infrasetup-WWKP0OOG5H6I293E.log` shows the
  same AGFI and same readiness plusargs successfully reached:
  `entered simulation flow execution`, `finished waiting`, then
  `FireSim fingerprint: 0x46697265`.

The failing host was terminated with:

```text
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh --session-name hwdebug-1c1p-uartprobe-nodebug-terminate-after-initwait-20260512 terminaterunfarm --forceterminate ...
```

EC2 reported `i-03b104f4eb973933a` as `shutting-down`. A later quick F2 query
showed no active F2 instances.

Conclusion: the latest failure is a FireSim host/slot/driver-readiness problem
before guest boot. It is invalid evidence for uartprobe, no-DMA, or DMA manager
behavior. The next F2 attempt should use a fresh runfarm and should not reuse a
slot after any failed preflight. If `infrasetup` passes, proceed immediately to
the manager-0 uartprobe workload; if it fails again at `wait_for_init()`, collect
`fpga-describe-local-image -M`, terminate the host, and return to local driver /
platform readiness analysis.

## 2026-05-12 local host-driver mitigation

After another fresh F2 (`i-0ad1eaa56786e1f10`) failed `infrasetup`, the full
manager log showed all three automatic preflight attempts reached
`finished waiting` but read `FireSim fingerprint: 0x1`. A subsequent manual
`+check-fingerprint` on the same host printed only `entered simulation flow
execution` and timed out. `fpga-describe-local-image -M` again showed no AWS shell
OCL/DMA/PCIM error counters.

Static audit of generated `SimulationMaster` showed:

- `INIT_DONE` becomes `1` after the init delay, so `0x1` is a valid value there.
- `PRESENCE_READ` mirrors `rFingerprint`.
- `PRESENCE_WRITE` is a read/write shadow reset to `0x46697265`.
- When `PRESENCE_WRITE != rFingerprint`, hardware assigns
  `rFingerprint := PRESENCE_WRITE`.

Therefore a plausible failure mode is that `PRESENCE_WRITE` has been polluted to
`0x1`, causing `PRESENCE_READ` to also become `0x1`. This matches the observed
`INIT_DONE/PRESENCE_READ/PRESENCE_WRITE == 0x1` pattern from the earlier sampled
run and explains why this is still before any guest/Linux/runtime execution.

Applied a host-driver-only mitigation in
`sims/firesim/sim/midas/src/main/cc/bridges/master.cc`: if the first
`PRESENCE_READ` is not `0x46697265`, write canonical `0x46697265` back to
`PRESENCE_WRITE` and re-read up to 16 times before failing. This does not change
hardware or guest behavior; it only repairs the SimulationMaster fingerprint
shadow before readiness validation.

Local validation:

```text
git -C sims/firesim diff --check -- sim/midas/src/main/cc/bridges/master.cc
# pass
cd sims/firesim && source sourceme-manager.sh --skip-ssh-setup && cd sim && make ... f2
# pass, warnings only
```

New local 1C1P nodebug driver SHA:
`3dbc066bec4f256b4edb3b12183501422d1cce54d4cf983d184437385a2d4afe`.

The failed fresh F2 was terminated; EC2 reported
`i-0ad1eaa56786e1f10` as `shutting-down`.

Next F2 action should use a fresh runfarm and only validate `infrasetup` first.
If readiness passes with this driver, proceed to the already-fixed manager-0
uartprobe workload. If it still fails, collect `-M`, terminate, and revisit
hardware MCR/reset behavior instead of running Linux.


## 2026-05-12T06:42Z - 1C1P readiness fingerprint 0x1 triage and controlled bypass

Current blocker is still before Linux/workload: FireSim `infrasetup` driver readiness. Multiple fresh F2 hosts loaded `agfi-098bce7d5e0c3d937` successfully and AWS shell counters stayed clean, but `+check-fingerprint` returned `FireSim fingerprint: 0x1` or occasionally timed out after `entered simulation flow execution`. This is not evidence about uartprobe, manager id, DMA, no-DMA, or pipeline-runtime user code.

Static checks performed:
- HWDB/build recipes point to `FireSimGemminiReRoCCPairDummy8x8C1P1Sbus64NICDebugConfig` with `WithTargetCycleDebug_WithPrintfSynthesis_WithSynthAsserts_FRFCFS16GBQuadRank_BaseF2Config`.
- Local generated const for that config is `INIT_DONE=0x24a0`, `PRESENCE_READ=0x24a4`, `PRESENCE_WRITE=0x24a8`.
- Generated SV routes `SimulationMaster_0` through control interconnect slave 11, whose decode range is `0x24a0 <= addr < 0x24b0`, matching the driver consts.
- Therefore the `0x1` result is unlikely to be a simple local driver/AGFI address-map mismatch. It looks like a FireSim/F2 readiness/reset/fingerprint-shadow anomaly, because `0x1` equals the normal `INIT_DONE` value after `wait_for_init()`.
- Same AGFI had earlier successful readiness runs on 2026-05-12 at 01:55, 02:38, 03:09, 03:23, and 04:01 UTC with `0x46697265`, then later fresh hosts often reported `0x1`. This argues against a pipeline-runtime workload regression.

Rejected fix:
- A previous host-driver-only attempt wrote `PRESENCE_WRITE=0x46697265` on mismatch. Fresh F2 validation still reported `0x1`/timeout, so that writeback change was removed.

Controlled experiment now staged:
- Added host-driver plusarg `+firesim-allow-fingerprint=<value>`. Default behavior is unchanged. Only when explicitly set will `master_t::check_fingerprint()` accept the specified nonstandard fingerprint and print a warning.
- Added `+firesim-allow-fingerprint=0x1` only to the 1C1P nodebug runtime `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug_uartprobe_nodebug.yaml`.
- Rebuilt the local F2 driver successfully for the target-cycle-debug 1C1P config. This is a diagnostic bypass to continue into Linux/workload and determine whether the remaining problem is only readiness fingerprint or a deeper hardware/software issue. Do not treat a run that relies on this bypass as proof that the fingerprint mechanism is healthy.

## 2026-05-12T06:36Z - runworkload pre-start readiness timeout and manager fix

Fresh F2 `i-079e9038c798f9d80` / `192.168.1.73` was launched and `infrasetup`
completed with local driver SHA
`50ad19677d25adf29452d2f91f2020a6ea1a7d7a70bf9b545d9ec9cde456c517`.
The full infrasetup log
`2026-05-12--06-25-23-infrasetup-TZSMSZZN5ZBRQJ8L.log` proves the controlled
bypass was actually exercised: the preflight command included
`+firesim-allow-fingerprint=0x1`, printed `FireSim fingerprint: 0x1`, then
`Warning: accepting FireSim fingerprint override 0x1`, and passed.

The subsequent `runworkload` on the same fresh host failed before Linux/workload
execution. The failure was in the manager's `pre_start_sim_slot()` readiness
check, not in guest code:

- manager log: `2026-05-12--06-29-13-runworkload-LPQ756FTI15GDTWU.log`
- all three pre-start `+check-fingerprint` attempts timed out with rc 124
- manual remote `+check-fingerprint` after failure printed only
  `entered simulation flow execution` and timed out, so it was stuck before
  `finished waiting`, i.e. in `simulation_t::wait_for_init()`
- AFI was still loaded as `agfi-098bce7d5e0c3d937`; remote `FireSim-f2` SHA
  matched the local rebuilt driver

Conclusion: the immediate runworkload blocker is the second, pre-workload
`+check-fingerprint` driver invocation. `infrasetup_instance()` already validates
readiness after flashing the AGFI. Starting another short-lived driver in
`pre_start_sim_slot()` can put this F2/design into a state where `INIT_DONE` is
not observed, preventing the real workload from starting.

Applied manager-only fix in
`sims/firesim/deploy/runtools/run_farm_deploy_managers.py`: keep the infrasetup
readiness check, but make `EC2InstanceDeployManager.pre_start_sim_slot()` a
no-op. This does not change RTL, AGFI, guest workload, DMA/no-DMA behavior, or
host driver fingerprint semantics. It only removes the destructive/redundant
second readiness probe before `sim-run.sh`.

Local validation:

```text
python -m py_compile sims/firesim/deploy/runtools/run_farm_deploy_managers.py
git -C sims/firesim diff --check -- deploy/runtools/run_farm_deploy_managers.py sim/midas/src/main/cc/bridges/master.cc sim/midas/src/main/cc/bridges/master.h deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug_uartprobe_nodebug.yaml
```

The failed F2 was terminated via FireSim manager; no active `f2.*` instances
remained immediately after termination.

## 2026-05-12T06:50Z - readiness probe contaminates SimpleNIC stream

The skip-`pre_start_sim_slot` manager change allowed `runworkload` to start the
real driver instead of failing in the second pre-start readiness probe. Evidence:

- fresh F2: `i-005300e30504ca388` / `192.168.1.219`
- infrasetup log: `2026-05-12--06-40-08-infrasetup-FHWRC0598QYGQL9A.log`
- runworkload log: `2026-05-12--06-43-08-runworkload-3O61N4C69HWGI9CK.log`
- result dir:
  `sims/firesim/deploy/results-workload/2026-05-12--06-43-08-rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug-f2-rerocc-linux-uartprobe-1c1p1-hwdebug-nodebug/`

The real workload still exited before Linux boot, but at a later and clearer
point. `uartlog` shows the driver reached `finished waiting`, accepted
`FireSim fingerprint: 0x1`, then entered `simplenic_t::init()` and failed:

```text
SIMPLENIC DEBUG [init_pull_expected_empty] requested_bytes=58560 actual_bytes=64 ...
FAIL. Exactly 1 tokens should be present in the cpu-bound stream on init
```

Static source check of `generators/firechip/bridgestubs/src/main/cc/bridges/simplenic.cc`
shows this path performs an initial pull from the target-to-host CPU stream and
exits if any bytes are present. The observed `actual_bytes=64` is exactly one
host stream bigtoken. Since the only prior target execution on this fresh slot
was the infrasetup `+check-fingerprint` readiness driver, the readiness probe is
not passive for NIC designs: it can leave a target-to-host token behind, which
causes the real workload's SimpleNIC init empty-stream assertion to fail.

Applied a scoped manager/runtime fix:

- `EC2InstanceDeployManager.verify_slot_driver_readiness()` now honors runtime
  plusarg `+firesim-skip-driver-readiness-preflight=1` and logs that it skipped
  the probe.
- The 1C1P nodebug uartprobe runtime now includes that plusarg.
- `pre_start_sim_slot()` remains a no-op, so runworkload does not launch a
  second short-lived `+check-fingerprint` driver.

This is a manager/runtime-only change; no RTL, AGFI, guest image, DMA path, or
host driver rebuild is required. The normal real workload driver still performs
its own fingerprint check inside `execute_simulation_flow()`.
