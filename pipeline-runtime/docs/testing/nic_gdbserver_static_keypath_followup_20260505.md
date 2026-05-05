# NIC gdbserver static key-path follow-up - 2026-05-05

This is a live triage note for the 1BP, non-8BP, single-core Rocket + NIC +
no TraceIO + 30 MHz recovery effort. It records the static and in-flight
Vivado evidence gathered while the 2026-05-05 bitstream builds were still
running.

## Current fixed target

- Target config: `FireSimRocketNICNoTraceConfig + BaseF2Config`
- Excluded config: `FireSimRocketNICNoTrace8BPConfig`
- Desired capability: the same remote `gdbserver` software-breakpoint smoke
  matrix passed by old AGFI `agfi-0079cbbca617eca4e`
- Current software control: old AGFI plus recovered driver has already passed
  the smoke matrix, so the active unknown is new hardware / implementation /
  route quality rather than the GDB expect harness

## Build state at 2026-05-05 08:45 UTC

Three build hosts were active, with no F2 run farm instance left running:

- `rocket-singlecore-nic-timingholdfixpcisreg1bp-build-20260505-0639`
  - host `i-069ea3337cb1e7d55`, private IP `192.168.3.128`
  - strategy `TIMING_HOLDFIX`
  - structure: single PCIS register slice
  - route status: high-effort hold fixing, no `Route 35-514` observed yet
  - latest route evidence: `WNS=-2.051`, `TNS=-2766.714`,
    `WHS=-1.022`, `THS=-205.284`
- `rocket-singlecore-nic-timingholdfixpcis2slr1bp-build-20260505-0758`
  - host `i-0b6c74e8e317c86d0`, private IP `192.168.3.88`
  - strategy `TIMING_HOLDFIX`
  - structure: two PCIS register slices, one constrained into SLR2 and one
    constrained into SLR1
  - status: post-placement physical optimization
  - post-place first timing path was still a PCIS SLR crossing into
    `AXI4_REG_SLC_PCIS_SLR2`
- `rocket-singlecore-nic-timingpcis2slrddrstat1bp-build-20260505-0812`
  - host `i-0eef76a36516ac895`, private IP `192.168.1.63`
  - strategy `TIMING`
  - structure: PCIS2SLR plus DDR stat split pipes
  - status: detail placement
  - post-opt first timing path moved to DDR reset recovery, not PCIS setup

Interpretation: PCIS2SLR and DDR-stat split are not cosmetic. They are moving
some top paths, but they do not remove all route pressure. The deciding result
is still the routed checkpoint and real F2 GDB smoke, not only the pre-route
report.

## Static key-path chain

### 1. SimpleNIC observability is still mostly real hardware

The current `SimpleNICBridge.scala` low-observability state suppresses the
large CSR attach surface by placing many `attach(...)` calls inside
`if (false)`. That does not remove most of the hardware that feeds those
signals:

- `BigTokenToNICTokenAdapter` still has debug counters, snapshots, and debug
  output ports.
- `NICTokenToBigTokenAdapter` still has debug counters, last-word snapshots,
  output-queue counters, and debug output ports.
- `SimpleNICBridgeModule` still has many 32-bit and 64-bit progress/blockage
  counters and snapshot registers.
- Only a smaller subset is attached to OCL now, but the producers remain in
  the design.

This is probably not the first functional suspect because the old AGFI proves
the software path and current adapter repairs are plausible. It is, however,
still a timing/area suspect if the active builds fail routing.

### 2. CPUManagedStreamEngine queue pointer fanout remains a major pressure

All active builds still show very-high-fanout messages for:

`CPUManagedStreamEngine_0/SIMPLENICBRIDGEMODULE_0_from_cpu_stream_incomingQueueIO_q/enq_ptr_value_reg[...]`

Counts from the current build logs:

- 0639 single PCIS-slice build: 167 matching high-fanout log lines
- 0758 PCIS2SLR build: 97 matching high-fanout log lines
- 0812 PCIS2SLR+DDRSTAT build: 97 matching high-fanout log lines

The source is the SimpleNIC CPU stream queue shape:

- `TokenQueueConsts.TOKEN_QUEUE_DEPTH = 3072`
- `fromHostCPUQueueDepth = TOKEN_QUEUE_DEPTH`
- `toHostCPUQueueDepth = TOKEN_QUEUE_DEPTH`
- the generated CPUManagedStreamEngine queue becomes a deep 512-bit queue
  implemented with many URAMs
- queue pointer bits fan out into many RAM write-address loads

Vivado repeatedly says these nets are not considered for normal very-high
fanout optimization because timing constraints reduce the effective candidate
fanout. It suggests `FORCE_MAX_FANOUT`, but using that as the next RTL/XDC
change should wait for the routed verdict because it is a physical workaround,
not a logical fix.

### 3. PCIS2SLR moved but did not eliminate PCIS pressure

The 0758 post-place first path was:

- source: hidden static-side register on `clk_main_a0`
- destination:
  `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/inst/ar.ar_pipe/m_payload_i_reg[80]/D`
- path type: setup
- slack: `-3.380ns`
- route share of data delay: about `95%`
- crossing: SLR1 to SLR2

This means the SLR2 register slice is real and named correctly, but the static
to SLR2 segment is still a hard path before post-place optimization. Later
physopt is replicating `S_READY` and related PCIS nets, so the final verdict
must come from route.

### 4. DDR reset / async recovery remains prominent

The 0812 `TIMING+PCIS2SLR+DDRSTAT` post-opt first path was:

- source: `WRAPPER/CL/SH_DDR/SYNC_RST/pipe_reg[3][0]/C`
- destination: hidden FDCE recovery check on `mmcm_clkout0`
- path group: `**async_default**`
- slack: `-1.284ns`
- net `WRAPPER/CL/SH_DDR/SYNC_RST/rst_pipe_n` had fanout around 445 in the
  unplaced report

The 0639 post-phys-opt first path was the inverse DDR reset recovery direction:

- source:
  `WRAPPER/CL/SH_DDR/genblk1.IS_DDR_PRESENT.DDR4_0/inst/div_clk_rst_r1_reg_replica/C`
- destination: hidden FDCE recovery check against `clk_main_a0`
- path group: `**async_default**`
- slack: `-1.911ns`

Old passing AGFI reports also had violations, so this evidence is not enough
to reject a build. It does show that if all current candidates fail, the next
logic/XDC question should include DDR reset constraints and reset-fanout
structure, not only PCIS datapath slicing.

## What this says about the next experiment

Do not go back to 8BP. The active issue is unrelated to hardware-breakpoint
count.

If one current build produces a plausible AGFI, run the old-AGFI GDB smoke
matrix against it before making another RTL change. Real F2 behavior is the
only useful final arbiter because old AGFI passed despite a violated route
checkpoint.

If all current builds fail timing badly or fail GDB on F2, the next build
should reduce actual hardware pressure instead of only hiding OCL registers.
Most likely candidates:

1. Remove or gate off SimpleNIC debug/snapshot hardware at elaboration time,
   while preserving the minimal protocol repairs that old-AGFI software
   validation needs.
2. Consider a controlled CPU stream queue-depth experiment only after checking
   the driver latency assumptions, because `TOKEN_QUEUE_DEPTH=3072` is tied to
   link latency buffering and changing it may alter runtime behavior.
3. Consider targeted `FORCE_MAX_FANOUT` only if routed reports still show the
   same CPUManagedStreamEngine queue pointer nets as timing or route blockers.
4. Review DDR reset false-path / async recovery constraints against the AWS F2
   reference constraints before changing RTL reset structure.

## Small local tests that remain useful

The tests already run in this checkpoint series still matter:

- `sshport_repair_test` passed and covers local switch repair behavior.
- `test_lib_pipe_ddr_stat_split` passed under Vivado xsim and covers the DDR
  stat split pipe latency/validity contract.

Additional local testing worth adding only if builds keep failing:

- a focused SimpleNIC adapter token packing/unpacking test, preferably at the
  Chisel module level rather than by duplicating C++ helper logic
- a C++ unit around shmem empty-marker handling and checksum repair if any new
  software-side change is made
- a generated-RTL lint/count script to assert that a future "min observability"
  build actually removes debug producer hardware, not only CSR attaches
