# 20260505T180030Z CoupledDMA copy model microtest

## Context

The `cfg32_nic` F2 bitstreams are still building, so I ran a small software model of the current `GemminiCoupledDMA` copy datapath while waiting. The goal was to catch obvious static logic errors in the optimized direct/misaligned copy behavior before relying on a new bitstream.

This test does not instantiate Chisel or run FireSim. It models the Scala algorithm in:

- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`

Specifically it mirrors:

- `canWide = src_aligned && dst_aligned && remaining >= beatBytes`
- `xfer = beatBytes` for wide copies, otherwise `min(remaining, dstBytesLeftInBeat)`
- second read when `srcBeatOffset + xfer > beatBytes`
- byte selection from `{hi, lo} >> srcShiftBits`
- destination partial write mask covering `dstOffset .. dstOffset + xfer`

## Command

An inline Python model was run from the repo root. The first exhaustive attempt was too broad and was terminated to avoid stealing CPU from local GoldenGate. The final bounded run covered:

- beat widths: 16, 32, 64, 128 bytes;
- deterministic boundary offsets around 0, 1, half-beat and beat-end;
- lengths around 0, 1, beat-1, beat, beat+1, 2*beat and 4*beat;
- 5000 random non-overlapping source/destination cases per beat width.

## Result

Passed:

```text
coupleddma copy model boundary/random checks passed
```

## Interpretation

The modeled byte movement matches normal byte-level copy for the covered non-overlapping cases. This gives confidence that the current `GemminiCoupledDMA` beat-splitting / cross-beat / partial-put algorithm is not obviously wrong at the static model level.

This does not prove:

- TileLink response ordering or backpressure behavior;
- SPM PTW/xlate interactions;
- ReRoCC scope ordering;
- Linux PA/cache/completion-flag behavior;
- FPGA timing or placement;
- overlapping src/dst semantics, which are not required for the DMA copy contract.

## Active build status

At the time of this microtest:

- mainline cfg32 NIC build `pairdummy-cfg32-nic-mainline-20260505T132956Z` was still alive, no exitcode, no AGFI/AFI;
- noTrace cfg32 NIC build `pairdummy-cfg32-nic-notrace-20260505T171453Z` was still alive in GoldenGate, no `FireSim-generated.sv` marker audit yet.

## Next

- Keep monitoring the active bitstream builds.
- When noTrace generated SV appears, audit NIC/optimized-DMA/no-Trace markers.
- When a new AGFI appears, validate the DMA path with real F2 gdbserver and targeted forced-direct/misaligned workloads before relaxing bounce guardrails.
