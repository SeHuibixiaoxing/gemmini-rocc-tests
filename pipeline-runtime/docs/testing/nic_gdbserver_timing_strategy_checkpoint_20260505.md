# 2026-05-05 NIC gdbserver timing strategy checkpoint

## Context

The software/GDB path is currently considered healthy because the historical
1bp non-8BP AGFI passed the legacy remote `gdbserver` software-breakpoint smoke
after the matching old driver bundle was restored:

- old AGFI: `agfi-0079cbbca617eca4e`
- old AFI: `afi-0ee7774f829acd4de`
- evidence: `debug_records/20260505T035417Z.md`
- capability scope: `target remote`, multiple software breakpoints,
  `continue`, `next`, `info threads`, `thread apply all bt`, register reads,
  disassembly, variable/memory reads and writes, thread/frame switching,
  Ctrl-C recovery, and `detach`

The remaining blocker is current-source hardware. The target remains the 1bp
single-core Rocket + NIC + no TraceIO + 30 MHz configuration:

- `FireSimRocketNICNoTraceConfig + BaseF2Config`
- not `FireSimRocketNICNoTrace8BPConfig`

## Why the old non-TIMING strategy failed

The historical failure after changing away from the conservative TIMING path was
not a gdbserver or Linux failure. The clearest example is
`debug_records/20260502T054938Z.md`:

- EXPLORE build produced `post_route.VIOLATED.dcp`.
- F2 initially advanced to about `3.13e8` target cycles.
- Then the FireSim master, clock, and peekpoke/OCL control surfaces read back
  as all zero, after which the run reported target-cycle-0 deadlock.

That result means the AGFI itself was not a reliable functional test vehicle.
The failure mode matched the timing report: AWS/Vivado warned the design did not
meet timing and generated a `.VIOLATED.dcp`.

On 2026-05-05 the parallel CONGESTION build repeated the same class of risk:

- tmux: `rocket-singlecore-nic-congestion1bp-build-20260505-0405`
- preliminary AGFI: `agfi-0e111a637cf0834a3`
- preliminary AFI: `afi-0fa6273c9f8c65423`
- status at 2026-05-05T05:18Z: waiting for AFI completion
- Vivado emitted `post_route.VIOLATED.dcp`
- negative path summary still included:
  - setup: `WRAPPER/RL_SHIM/PIPE_RST_OUT_N/... -> WRAPPER/CL/ddr_ready_pre_sync_meta_reg/CLR`
  - hold: `WRAPPER/CL/ocl_fsim_ar_fire_count_reg[6]/C -> WRAPPER/RL_SHIM/PIPE_STATUS1/pipe_reg[0][22]/D`

This CONGESTION AGFI can be useful only as a classification artifact. If it
fails on F2, the likely cause remains timing/OCL instability, not software.

## What changed for the conservative retry

A new conservative build path was committed before launch:

- F2 platform commit: `4ab10460e886938a1e85204e666367d9624ab177`
- FireSim commit: `586b5af5385d0b8b88dbfe30d8f1f2909ac663e7`
- top-level commit: `950f2b1e`

The key source and strategy differences are:

- Keep the PCIS 512-to-64 bridge used by the F2 BAR4 /
  `CPUManagedStreamEngine` path.
- Keep the F1Shim reset independent of `ddr_ready_sync`.
- Remove host-visible OCL/PCIS debug counters from `cl_sh_status*` and
  `cl_sh_status_vled`; these signals now return constants again.
- Add `TIMING_HOLDFIX`, which keeps the TIMING directives but adds:
  `set_param route.enableHoldExpnBailout 0`.
- Keep `set_param route.enableGlobalHoldIter 1`.

The reason for removing the status fanout is direct: the recent timing reports
showed tight/violating `PIPE_STATUS0/1` paths caused by the temporary debug
status outputs. Those outputs are not needed for the remote gdbserver smoke.

## Running builds

At 2026-05-05T05:18Z three builds were active:

- CONGESTION comparison:
  `rocket-singlecore-nic-congestion1bp-build-20260505-0405`
- TIMING comparison with the older observability-heavy source snapshot:
  `rocket-singlecore-nic-timing1bp-build-20260505-0510`
- conservative TIMING_HOLDFIX candidate:
  `rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517`

The conservative build command is:

```sh
cd /home/ubuntu/chipyard/sims/firesim
../../scripts/firesim-tmux-run.sh \
  --session-name rocket-singlecore-nic-timingholdfix1bp-build-20260505-0517 \
  buildbitstream \
  -b config_build_f2_rocket_singlecore_nic_notrace_timingholdfix1bp_30mhz.yaml \
  -r config_build_recipes_f2_rocket_singlecore_nic_notrace_timingholdfix1bp_30mhz.yaml
```

The conservative build host is:

- instance: `i-093a29cdf23e009b0`
- private IP: `192.168.1.36`
- build cluster tag: `f2rocketsinglecorenictimholdfix1bpbuild`

## Current risk assessment

This retry can still fail. `route.enableHoldExpnBailout 0` follows Vivado's own
Route 35-514 recommendation, but it can increase route runtime substantially.
The result is only trustworthy if either timing closes or the later F2 run no
longer shows OCL/control-surface collapse.

The important distinction from the older failed EXPLORE/CONGESTION attempts is
that this is still a TIMING-derived strategy and the unnecessary `PIPE_STATUS`
debug fanout has been removed. If this build still produces `.VIOLATED.dcp`, it
must not be treated as a clean hardware proof for gdbserver.

## Next pass/fail gate

When a new AGFI is available:

1. Check the build result for `.post_route.dcp` versus `.post_route.VIOLATED.dcp`.
2. Record AGFI/AFI and timing reports in a new debug record.
3. Run the clean 1bp gdbserver smoke, using the first TCP connection for real
   GDB rather than probing the `gdbserver --once` port with `nc`.
4. Commit the tested state and evidence after the key test milestone.
