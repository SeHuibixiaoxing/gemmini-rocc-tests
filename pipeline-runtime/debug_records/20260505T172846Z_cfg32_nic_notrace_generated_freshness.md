# 20260505T172846Z cfg32 NIC no-TraceIO generated-source freshness

## Goal

Check the generated sources for the parallel cfg32 NIC no-TraceIO bitstream while it is still in the
build pipeline. This verifies that the resource-reduced fallback actually elaborated the intended target
configuration before waiting for Vivado/AGFI.

## Build context

- Session: `pairdummy-cfg32-nic-notrace-20260505T171453Z`.
- Target config: `FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig`.
- Platform config: `FRFCFS16GBQuadRank_BaseF2Config`.
- Build strategy/frequency: `TIMING`, `20MHz`.
- Generated source directory:
  `sims/firesim-staging/generated-src/firechip.chip.FireSim.FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig`.

## Freshness evidence

Generated files were written at `2026-05-05 17:17:32` through `2026-05-05 17:18:00` UTC. Key files include:

- `.dts`
- `.memmap.json`
- `.anno.json`
- `.sfc.fir`
- `.fir`
- `.appended.anno.json`

## Static checks

Device tree / memmap:

```text
DTS_ice_nic=1
DTS_1bp=4
DTS_rerocc_mgr=12
MEM_ice_nic=1
```

Interpretation:

- The target has `ice-nic@10016000`.
- All four Rocket cores advertise `hardware-exec-breakpoint-count = <1>`.
- The target has 12 `rerocc-mgr@...` MMIO nodes.
- The memmap also contains `ice-nic@10016000`.

Annotation checks:

```text
NICBridge=2
SimpleNICBridgeModule=1
SimpleNIC=1
IceNIC=4
TracerVBridge=0
TracePort=0
TraceIO=0
```

Interpretation:

- NIC bridge annotations are present.
- `SimpleNICBridgeModule` is present.
- No TracerV/TraceIO annotation is present in the target annotation list.

FIRRTL checks:

```text
FIR_bytes_written_per_beat=960
FIR_write_shift=192
FIR_TracerVBridge=0
FIR_TraceIO=0
FIR_TracePort=0
```

Interpretation:

- The optimized DMA write-packet marker `bytes_written_per_beat` is present.
- The optimized DMA alignment marker `write_shift` is present.
- TracerV/TraceIO strings are absent from the `.sfc.fir` file.

## Conclusion

The no-TraceIO variant elaborated the intended `4 core / 12 pair-manager / cfg32 / NIC / optimized DMA / 1BP`
target and removed TraceIO/TracerV from the generated target collateral. This is still only a generated-source
freshness checkpoint; it does not prove GoldenGate, Vivado synthesis/place/route, AGFI creation, or remote
`gdbserver` behavior.

## Next gate

When `FireSim-generated.sv` appears under the no-TraceIO FireSim generated-src or remote CL directory, repeat
the same marker checks on the actual post-GoldenGate RTL before accepting the bitstream as a valid run target.
