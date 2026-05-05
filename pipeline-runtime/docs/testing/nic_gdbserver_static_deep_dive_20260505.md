# NIC GDBServer 1BP Static Deep Dive - 2026-05-05

## Scope

This is a follow-up static triage note for the 1BP, non-8BP, single-core Rocket
NIC no-TraceIO 30 MHz F2 target. The goal remains producing a new AGFI from the
current hardware tree that passes the same remote `gdbserver` software-breakpoint
matrix as:

```text
AGFI: agfi-0079cbbca617eca4e
AFI:  afi-0ee7774f829acd4de
Build result:
sims/firesim/deploy/results-build/2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz/
Config: FireSimRocketNICNoTraceConfig + BaseF2Config
```

## High-confidence facts

1. The current software/control path is not the primary blocker. The old AGFI
   passed the current `remote-swbreak-expect` matrix on 2026-05-05.

2. The active builds are not 8BP builds. They use
   `FireSimRocketNICNoTraceConfig + BaseF2Config`, not
   `FireSimRocketNICNoTrace8BPConfig`.

3. The local host/switch repairs remain good:
   `sshport_repair_test`, `shmemport_marker_test`, and
   `test_lib_pipe_ddr_stat_split` all pass in the current tree.

4. Negative timing by itself is not enough to disqualify an AGFI. The old
   passing AGFI had negative post-route timing and DDR reset/ready async paths.

## RL_SHIM PCIS tight pins: important but not sufficient

The latest active TIMING_HOLDFIX PCIS2SLR route log highlights pins under:

```text
WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/r.r_pipe/...
```

This is a real pressure point, but it is not new. The old passing AGFI also had
almost the same class of tight setup/hold pins.

Comparison:

```text
old 2026-04-30 tight_setup_hold_pins.txt lines:     2002
current 2026-05-05 tight_setup_hold_pins.txt lines: 1945

old pins under DMA_PCIS_AXI_REG_SLC:     1801
current pins under DMA_PCIS_AXI_REG_SLC: 1827
```

Top old prefixes:

```text
1801 WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE
42   WRAPPER/RL_SHIM/OCL_REG_SLC/inst
29   WRAPPER/CL/SH_DDR/ddr_stat.CCF_XSDB_REQ
```

Top current prefixes:

```text
1827 WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE
42   WRAPPER/RL_SHIM/OCL_REG_SLC/inst
29   WRAPPER/CL/SH_DDR/ddr_stat.CCF_XSDB_REQ
```

Implication:

The RL_SHIM PCIS register slice is a good place to mine routed reports and
placement evidence, but it is not a smoking gun. A passing design already had
this pattern. The more relevant question is whether the current implementation
pushes this area past a runtime-sensitive threshold, or whether the observable
runtime failure is coming from another path such as OCL/control, DDR reset/status,
or CPU-managed stream congestion.

## Current PCIS shell-slice experiments

Current F2 source adds shell-boundary PCIS register slices:

```text
WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2
WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR1
```

These are inspired by AWS examples that pipeline PCIS across SLRs. Static review
shows they are not 8BP-related and should not affect Rocket hardware breakpoint
resources.

Risk:

The active route log still calls out `WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC`,
which is downstream/upstream of this shell-boundary slice depending on the
logical direction. If the routed endpoint remains dominated by RL_SHIM internal
placement, adding shell slices may improve one segment without removing the
actual tight pin class.

Decision:

Keep waiting for the current builds before making another source change. If the
PCIS-slice AGFI finishes, run it; if it fails route or fails GDB with the same
runtime signature, the next change should target evidence from the finished
route reports rather than adding another blind shell slice.

## DDR reset/status paths

The old passing build and current builds both show DDR reset/status timing
pressure. Examples already observed:

- old post-route worst paths around `ddr_ready_pre_sync_meta_reg/CLR` and
  `ddr_ready_sync_reg/CLR`;
- current post-phys-opt paths around `SH_DDR/SYNC_RST/pipe_reg[3][0]/C`;
- current/previous paths around `PIPE_DDR_STAT_ACK0` and DDR status data.

The DDR stat split pipe xsim passes, so the local semantic split is still valid.
The unresolved question is whether the reset/status timing is a harmless F2 shell
artifact like in the old AGFI, or whether new placement makes it runtime-visible.

## SimpleNIC observability and fanout

The current SimpleNIC bridge still contains many real counters and snapshots even
though the broad OCL attach list is behind `if (false)`. This means hiding
register attaches reduces OCL surface but does not remove all debug hardware.

The generated RTL count comparison still matters:

```text
old generated RTL debug_ count:    417
current generated RTL debug_ count: 34

old generated RTL blocked count:    774
current generated RTL blocked count: 15
```

So the current design is less observable than the old build in attach-visible
text, but still contains active counters and state in the SimpleNIC datapath.
This remains a possible contributor to placement/fanout, not a proven root cause.

## Useful non-bitstream checks

The following checks are currently useful and cheap:

```text
PASS sshport_repair_test
PASS shmemport_marker_test
PASS test_lib_pipe_ddr_stat_split
```

They cover the host-side packet repair path, shmem empty-marker clearing, and
the split DDR stat pipe module. They do not cover full FireSim CPU-managed stream
semantics, Vivado placement, or F2 runtime stability.

## What to do after the active builds

If a build produces an AGFI:

1. Commit the build checkpoint with AGFI/AFI, config, recipe, result path,
   timing status, key route warnings, and limitations.
2. Run the old-AGFI-equivalent remote `gdbserver` software-breakpoint smoke.
3. If it passes, checkpoint commit the pass and promote that AGFI.
4. If it fails, preserve UART, switchlog, workload output, GDB/expect logs, and
   live run evidence before terminating the F2 farm.

If all active builds fail route or produce unusable AGFIs:

1. Do not return to 8BP.
2. Use the routed reports to decide whether the next source change targets:
   RL_SHIM/PCIS placement, DDR reset/status constraints, or SimpleNIC
   observability/fanout.
3. Prefer a single high-information next build over another blind sequence of
   small timing tweaks.
