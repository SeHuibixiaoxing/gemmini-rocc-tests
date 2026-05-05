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
