# NIC gdbserver 1BP PCIS timing builds - 2026-05-05

This note records the build scheduling decision made after the user requested
a parallel ordinary TIMING build and explicit handling of the current critical
paths.

## Fixed target

- Target: `FireSimRocketNICNoTraceConfig + BaseF2Config`
- Frequency: 30 MHz
- Scope: single-core Rocket + NIC + no TraceIO + 1BP behavior
- Excluded: `FireSimRocketNICNoTrace8BPConfig` and any new hardware-breakpoint
  expansion
- Historical software-good reference AGFI: `agfi-0079cbbca617eca4e`

## Source checkpoints

- F2 platform submodule: `828e90e` (`Add PCIS register slice timing experiment`)
- FireSim submodule: `6936e6d2b` (`Add PCIS register-slice 1BP NIC build experiments`)
- Top-level chipyard: `1e196be5` (`Checkpoint PCIS register-slice build recipes`)

The F2 change inserts `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR1` at the PCIS shell
boundary before the existing `axi_clock_converter_512_wide`. The instance name
matches the existing `small_shell_cl_pnr_user.xdc` SLR1 placement constraint.
Static pre-build checks passed:

- `git diff --check` in the F2 tree
- `git diff --check` in the FireSim tree
- YAML parse for all four PCIS build configs
- Vivado 2024.2 `xvlog` parse of `axi_register_slice_bmstub.v` and the modified
  `cl_firesim.sv`

## Build scheduling

At 2026-05-05 06:37 UTC, the ordinary TIMING + PCIS build was launched:

- tmux session: `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
- build config: `deploy/config_build_f2_rocket_singlecore_nic_notrace_timingpcisreg1bp_30mhz.yaml`
- recipe: `deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_timingpcisreg1bp_30mhz.yaml`
- build strategy: `TIMING`
- build host: `i-0ba6729824eb22932`, private IP `192.168.3.248`

The first TIMING_HOLDFIX + PCIS launch at 2026-05-05 06:37 UTC failed before
creating a build host because the account was at the 32 vCPU EC2 limit:

- tmux session: `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0637`
- result: exit code 1
- failure: `VcpuLimitExceeded`
- FireSim log:
  `sims/firesim/deploy/logs/2026-05-05--06-37-18-buildbitstream-N0FEG6ALNQ16TEZ7.log`

To keep progress under the vCPU limit, the older ordinary TIMING no-PCIS
comparison build was stopped and its build host was reclaimed:

- tmux session: `rocket-singlecore-nic-timing1bp-build-20260505-0510`
- build host: `i-001da349e4e62d05b`, private IP `192.168.0.6`
- stopped because it had already reproduced the historical bad TIMING pattern
  and was lower priority than TIMING_HOLDFIX + PCIS
- termination command: `aws ec2 terminate-instances --instance-ids i-001da349e4e62d05b`
- termination confirmed by `aws ec2 wait instance-terminated --instance-ids i-001da349e4e62d05b`

The stopped TIMING no-PCIS build had already shown:

- post-place WNS `-3.581`
- `Route 35-514`: router disabled hold fixing because of many hold violators
- post-route phys_opt started from roughly `WNS=-3.187`, `WHS=-4.029`
- later phys_opt still referenced `PIPE_STATUS0/sm_sh_status0[20]` and
  `ctrlInterconnect/xbar/router` paths, so this build was not a promising
  candidate for gdbserver validation

After reclaiming capacity, TIMING_HOLDFIX + PCIS was relaunched at
2026-05-05 06:39 UTC:

- tmux session: `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
- build config:
  `deploy/config_build_f2_rocket_singlecore_nic_notrace_timingholdfixpcisreg1bp_30mhz.yaml`
- recipe:
  `deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_timingholdfixpcisreg1bp_30mhz.yaml`
- build strategy: `TIMING_HOLDFIX`
- build host: `i-069ea3337cb1e7d55`, private IP `192.168.3.128`

## Active builds after this checkpoint

As of 2026-05-05 06:40 UTC:

- `rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517`
  - original no-PCIS TIMING_HOLDFIX comparison
  - host `i-093a29cdf23e009b0`, private IP `192.168.1.36`
  - status: running in route
  - latest key timing seen: post-place WNS `-2.128`; route intermediate
    around `WNS=-1.951`, `WHS=-3.703`; no `Route 35-514` seen yet
- `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
  - ordinary TIMING + PCIS register slice
  - host `i-0ba6729824eb22932`, private IP `192.168.3.248`
  - status: running, remote build script started
- `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
  - TIMING_HOLDFIX + PCIS register slice
  - host `i-069ea3337cb1e7d55`, private IP `192.168.3.128`
  - status: running, build host boot in progress or just booted

## Next checks

Monitor at 1200 second intervals unless a build finishes earlier. For each build,
capture:

- exit code file under `tmp/firesim-aws-f2/tmux/`
- `Route 35-514`, `post_route.VIOLATED`, AGFI/AFI, and timing summary lines
- whether the final DCP is a clean `post_route.dcp` or violated
- generated `AGFI_INFO` and FireSim result-build directory

Only a clean or plausible AGFI should proceed to `infrasetup` and the remote
gdbserver software-breakpoint smoke workflow.

## 2026-05-05 07:00 UTC monitor checkpoint

No build had completed by this checkpoint, and no new AGFI/AFI was available.
EC2 state showed only the manager plus three build hosts:

- manager: `i-08b9950158e875a41`, `c5.2xlarge`, private IP `192.168.1.149`
- original TIMING_HOLDFIX: `i-093a29cdf23e009b0`, `m8i.2xlarge`,
  private IP `192.168.1.36`
- TIMING + PCIS: `i-0ba6729824eb22932`, `m8i.2xlarge`,
  private IP `192.168.3.248`
- TIMING_HOLDFIX + PCIS: `i-069ea3337cb1e7d55`, `m8i.2xlarge`,
  private IP `192.168.3.128`

Build status:

- `rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517`
  - still running in route
  - no `Route 35-514` hold-fix bailout seen
  - repeated `Route 35-469` warnings remain
  - latest visible intermediate timing:
    `WNS=-2.049`, `TNS=-5286.672`, `WHS=-1.015`, `THS=-611.616`
- `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
  - still running
  - in placement / floorplanning
  - no post-place timing yet
- `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
  - still running
  - in placement / floorplanning
  - no post-place timing yet

Interpretation:

- The original TIMING_HOLDFIX build is still not clean, but it is materially
  different from ordinary TIMING: the router has not printed `Route 35-514`.
- The PCIS register-slice builds have not reached the point where the critical
  path effect can be judged.
- Continue monitoring at the next 1200 second interval.

## 2026-05-05 07:22 UTC monitor checkpoint

No build had completed by this checkpoint, and no new AGFI/AFI was available.
The same three build hosts remained running.

Build status:

- `rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517`
  - still running in route
  - still no `Route 35-514` hold-fix bailout seen
  - repeated `Route 35-469` warnings continue
  - latest visible route intermediate remains:
    `WNS=-2.049`, `TNS=-5286.672`, `WHS=-1.015`, `THS=-611.616`
- `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
  - reached post-place / phys_opt
  - post-placement WNS: `-2.039`
  - phys_opt visible summary: `WNS=-1.911`, `TNS=-2420.912`,
    `WHS=-3.713`, `THS=-6016.338`
  - no route-stage verdict yet
- `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
  - reached post-place / phys_opt
  - post-placement WNS: `-2.039`
  - post-phys_opt visible summary: `WNS=-1.911`, `TNS=-2420.912`,
    `WHS=-3.713`, `THS=-5972.714`
  - phys_opt completed successfully and started writing
    `post_phys_opt.dcp`
  - no route-stage verdict yet

Interpretation:

- The PCIS register slice appears to have materially improved setup pressure
  before route compared with the no-PCIS TIMING_HOLDFIX comparison:
  setup TNS is roughly `-2421` instead of roughly `-4238` at comparable
  post-phys_opt visibility.
- Hold pressure is still severe before route (`WHS=-3.713`), so the decisive
  question remains whether routing can avoid ordinary TIMING's `Route 35-514`
  bailout.
- The visible phys_opt critical processing for PCIS builds now includes
  `SH_DDR` / `PIPE_DDR_STAT0` paths rather than only the PCIS boundary. If the
  PCIS builds fail, the next static target should be those DDR-status/status
  pipeline paths, not 8BP logic.
- Continue monitoring at the next 1200 second interval.

## 2026-05-05 07:47 UTC monitor checkpoint

No build had completed by this checkpoint, and no new AGFI/AFI was available.
The same three build hosts remained running:

- original TIMING_HOLDFIX: `i-093a29cdf23e009b0`, private IP
  `192.168.1.36`
- TIMING + PCIS: `i-0ba6729824eb22932`, private IP `192.168.3.248`
- TIMING_HOLDFIX + PCIS: `i-069ea3337cb1e7d55`, private IP
  `192.168.3.128`

Build status:

- `rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517`
  - still running in route
  - still no `Route 35-514` hold-fix bailout seen
  - route entered high-effort hold fixing:
    `Route 35-444 Design has unmet hold violation, router is invoking high
    effort hold fixing`
  - latest visible intermediate timing:
    `WNS=-2.049`, `TNS=-5286.672`, `WHS=-1.015`, `THS=-611.616`
  - late phys_opt processing references `SH_DDR` and `PIPE_DDR_STAT0`, so DDR
    status/reset-related paths remain part of the timing pressure
- `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
  - reached route and reproduced the ordinary TIMING failure mode:
    `Route 35-514 Design has a large number of hold violators. ... Router is
    turning off hold fixing`
  - route completed, then started post-route phys_opt at 2026-05-05 07:45 UTC
  - visible route/post-route timing remains violated:
    `WNS=-1.951`, `TNS=-2604.675`, `WHS=-3.731`, `THS=-2895.875`
  - this build is not a good AGFI candidate unless later output unexpectedly
    produces a clean routed checkpoint, which is unlikely after `Route 35-514`
- `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
  - still running in route
  - no `Route 35-514` observed yet
  - repeated `Route 35-469` warnings continue
  - post-phys_opt setup TNS remains materially better than the no-PCIS
    comparison (`~ -2421` vs `~ -4238` before route), but route still reports
    heavy hold pressure
  - route congestion / failing-endpoint output still names
    `WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/...`, which means
    the shell-boundary PCIS slice improved the situation but did not fully
    remove PCIS/RL_SHIM timing pressure

Interpretation:

- The user-requested ordinary `TIMING` parallel experiment has now done its
  diagnostic job: even with the PCIS shell-boundary register slice, plain
  `TIMING` repeats the historical `Route 35-514` hold-fix bailout. This is the
  same class of failure seen in earlier non-TIMING / bad-strategy attempts, so
  it should not be the primary path to a gdbserver validation AGFI.
- `TIMING_HOLDFIX+PCIS` remains the most useful active build because it keeps
  hold fixing enabled while preserving the PCIS setup improvement.
- If both `TIMING_HOLDFIX` builds fail, the next RTL-side timing experiment
  should be chosen from the observed route references, not from 8BP logic:
  either add another carefully placed PCIS/RL_SHIM boundary slice or reduce the
  `SH_DDR` / `PIPE_DDR_STAT0` status/reset path pressure. That change should be
  launched only after the current `TIMING_HOLDFIX+PCIS` route verdict is known,
  to avoid burning build capacity on a speculative variant.

## PCIS2SLR follow-up prepared

After the 07:47 UTC checkpoint, the next timing experiment was prepared but not
yet launched. The change extends the PCIS shell-boundary register-slice wrapper
from one slice to two slices:

- `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2`
- `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR1`

Reasoning:

- The single-slice PCIS experiment improved pre-route setup TNS, so the PCIS
  boundary is a real contributor.
- The ordinary `TIMING+PCIS` build still reproduced `Route 35-514`, so one SLR1
  slice is not enough for the plain TIMING strategy.
- The AWS `cl_dma_pcis_slv` example stages PCIS through `AXI4_REG_SLC_PCIS_SLR2`
  and then `AXI4_REG_SLC_PCIS_SLR1`, while the previous FireSim experiment only
  inserted the SLR1 slice. Matching the AWS staging pattern is less speculative
  than changing 8BP logic or altering unrelated target RTL.
- The new slice only adds AXI register-slice latency at the shell boundary. It
  does not add hardware breakpoints and does not touch the 1BP debug module
  behavior.

Static checks completed:

- `git diff --check` for the F2 RTL/XDC edits
- `git diff --check` for the FireSim build YAML files
- Python `yaml.safe_load` for the new build and build-recipes YAML
- Vivado 2024.2 `xvlog` parse of `axi_register_slice_bmstub.v` and modified
  `cl_firesim.sv`, using the current generated
  `FireSimRocketNICNoTraceConfig + BaseF2Config` defines include directory

Prepared build config:

- build config:
  `sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slr1bp_30mhz.yaml`
- recipe:
  `sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slr1bp_30mhz.yaml`
- target: `FireSimRocketNICNoTraceConfig + BaseF2Config`
- strategy: `TIMING_HOLDFIX`
- frequency: 30 MHz

Capacity plan:

- The ordinary `TIMING+PCIS` build has already served its diagnostic purpose by
  reproducing `Route 35-514`.
- If no build host is free, reclaim `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
  / `i-0ba6729824eb22932` before launching the PCIS2SLR build.

## 2026-05-05 07:59 UTC PCIS2SLR launch checkpoint

The ordinary `TIMING+PCIS` comparison build was reclaimed to free capacity:

- tmux session: `rocket-singlecore-nic-timingpcisreg1bp-build-20260505-0637`
- build host: `i-0ba6729824eb22932`
- private IP before termination: `192.168.3.248`
- reason: it had already reproduced `Route 35-514`, with visible violated
  route/post-route timing around `WNS=-1.951`, `WHS=-3.731`
- termination command:
  `aws ec2 terminate-instances --instance-ids i-0ba6729824eb22932`
- termination confirmed by `aws ec2 wait instance-terminated`
- resulting tmux exit code: `1`, expected because the build host was terminated
  intentionally

The PCIS2SLR build was then launched:

- tmux session:
  `rocket-singlecore-nic-timingholdfixpcis2slr1bp-build-20260505-0758`
- command:
  `scripts/firesim-tmux-run.sh --session-name rocket-singlecore-nic-timingholdfixpcis2slr1bp-build-20260505-0758 buildbitstream -b config_build_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slr1bp_30mhz.yaml -r config_build_recipes_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slr1bp_30mhz.yaml`
- build config:
  `sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slr1bp_30mhz.yaml`
- recipe:
  `sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slr1bp_30mhz.yaml`
- build strategy: `TIMING_HOLDFIX`
- build host: `i-0b6c74e8e317c86d0`
- private IP: `192.168.3.88`

Launch status:

- local `replace-rtl` started successfully and copied the modified
  `cl_firesim` collateral into the config-specific F2 developer design
- no PCIS2SLR route/timing result yet
- no PCIS2SLR AGFI/AFI yet

Active builds after launch:

- `rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517`
  - no-PCIS TIMING_HOLDFIX comparison
  - host `i-093a29cdf23e009b0`, private IP `192.168.1.36`
- `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
  - single-slice PCIS TIMING_HOLDFIX comparison
  - host `i-069ea3337cb1e7d55`, private IP `192.168.3.128`
- `rocket-singlecore-nic-timingholdfixpcis2slr1bp-build-20260505-0758`
  - two-slice PCIS TIMING_HOLDFIX experiment
  - host `i-0b6c74e8e317c86d0`, private IP `192.168.3.88`

Next monitor target remains a 1200 second interval unless one build exits or
prints an AGFI/AFI earlier.

## 2026-05-05 08:07 UTC TIMING + PCIS2SLR + DDR-stat experiment prepared

The user asked to keep an ordinary `TIMING` strategy build in parallel and to
handle the currently visible critical paths explicitly.

Earlier evidence explains the risk:

- plain `TIMING` without PCIS changes already reproduced the bad hold pattern:
  post-place WNS around `-3.581`, `Route 35-514`, and post-route/physopt around
  `WNS=-3.187`, `WHS=-4.029`
- plain `TIMING+PCIS` also reproduced `Route 35-514`, with visible post-route
  timing around `WNS=-1.951`, `WHS=-3.731`
- therefore a new plain `TIMING` build is expected to fail unless the structural
  timing paths have changed enough; it is useful as a diagnostic/control, but
  not the safest primary candidate

The next ordinary `TIMING` control has two structural changes relative to the
failed `TIMING+PCIS` build:

- PCIS shell boundary now uses the two-stage SLR2 -> SLR1 register-slice wrapper
  already launched in the active `TIMING_HOLDFIX+PCIS2SLR` build
- DDR status request/response pipes are split into separate data/control
  `lib_pipe` instances, matching the AWS `aws_v3_0_top` style more closely:
  `addr`, `wdata`, and `rdata` use resetless data pipes, while `wr`, `rd`,
  `ack`, and `int` remain reset-controlled

The DDR stat split intentionally preserves the existing
`NUM_CFG_STGS_CL_DDR_ATG` latency. It is meant to reduce reset/data fanout and
avoid keeping the data bits on the same async-reset pipe as the control bits.
This targets the route/physopt references to `SH_DDR`,
`PIPE_DDR_STAT_ACK0`, `DDR_STAT_PIPE_DATA`, and related DDR-status/reset nets.

Prepared FireSim configs:

- ordinary TIMING:
  `sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_timingpcis2slrddrstat1bp_30mhz.yaml`
- ordinary TIMING recipe:
  `sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_timingpcis2slrddrstat1bp_30mhz.yaml`
- TIMING_HOLDFIX fallback:
  `sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slrddrstat1bp_30mhz.yaml`
- TIMING_HOLDFIX fallback recipe:
  `sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_timingholdfixpcis2slrddrstat1bp_30mhz.yaml`

Static checks completed:

- F2 `git diff --check` for `cl_firesim.sv` and the XDC file
- FireSim `git diff --check` for the new YAML files
- Python `yaml.safe_load` for all four new config files
- Vivado 2024.2 `xvlog` parse of `axi_register_slice_bmstub.v` and the modified
  `cl_firesim.sv`, using the current generated
  `FireSimRocketNICNoTraceConfig + BaseF2Config` include directory

Capacity plan:

- account capacity is currently saturated by the manager plus three
  `m8i.2xlarge` build hosts
- the no-PCIS `TIMING_HOLDFIX` comparison is the least useful active build:
  it has worse pre-route setup pressure than PCIS variants and is not testing
  the current PCIS2SLR fix
- reclaim that host first if no build naturally finishes before launch, then
  start `TIMING+PCIS2SLR+DDRSTAT`

## Build-time reduction and no-AGFI triage policy

The slow part of this loop is not Verilog generation or driver compilation; it
is Vivado implementation. The highest-leverage acceleration is therefore to
avoid low-information full builds and to stop doomed builds early.

Useful bitstream-build acceleration levers:

- Keep parallel builds only when they answer different questions. Do not spend
  capacity on several variants that are all expected to fail in the same way.
- Treat ordinary `TIMING` builds as diagnostic until proven otherwise. If a
  `TIMING` build prints `Route 35-514`, the old failure mode has repeated:
  the router disabled hold fixing because of too many hold violators. In this
  project's recent history, waiting for the rest of that run has not produced a
  good AGFI, so the build host should normally be reclaimed after collecting the
  relevant route/timing evidence.
- Use `TIMING_HOLDFIX` for serious candidate builds when hold pressure is the
  blocker. It is slower, but it directly targets the observed hold-fix bailout.
- Consider a larger build instance only for a high-confidence final candidate.
  The current `m8i.2xlarge` choice allows three parallel build hosts under the
  observed 32 vCPU account limit. A larger instance may reduce single-build
  latency but will reduce parallelism and will not make Vivado route scale
  linearly.
- Lower-frequency builds can separate logic-function failure from timing
  closure failure, but they are not a replacement for the final 30 MHz target.
- The F2 scripts write intermediate DCPs, and `step_user.tcl` can reopen
  checkpoints for implementation phases. That is useful for manual diagnosis
  when only strategy/XDC/post-place choices changed. It is not a clean
  replacement for a FireSim manager build when RTL changed or when a final AGFI
  is required.

Useful no-bitstream work:

- Keep using old AGFI controls for software/runtime changes. The old AGFI plus
  recovered driver already passed the current remote gdbserver software
  breakpoint matrix, so it is the fastest way to check switch, driver, rootfs,
  SSH tunnel, and expect-harness regressions.
- Use static diffs against the known-good state and `main` for FireSim configs,
  HWDB freshness, generated RTL shape, driver bundle hashes, and 1BP-vs-8BP
  separation.
- Run cheap structural checks before every build: YAML parse, `git diff --check`,
  Vivado `xvlog`, pblock cell-name checks, and generated RTL/interface width
  checks.
- Mine existing failed DCPs/reports for `report_timing`,
  `report_high_fanout_nets`, `report_qor_suggestions`, pblock membership, and
  reset/fanout evidence. This can guide the next RTL/XDC change without waiting
  for a new AGFI.
- Prefer small local tests for `ShmemPort`/`SSHPort` packet handling,
  checksum/flit packing, SimpleNIC bridge token protocol, PCIS wrapper
  handshake, and DDR stat pipe latency. Full Linux metasim is too slow for this
  loop and does not exercise the same F2 shell/SLR/timing implementation paths.
