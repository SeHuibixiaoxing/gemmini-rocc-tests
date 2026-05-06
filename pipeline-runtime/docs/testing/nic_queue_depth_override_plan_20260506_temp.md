# SimpleNIC Queue-Depth Override Contingency - 2026-05-06

## Context

This is a contingency plan only. It is not applied to the active TIMING builds:

- `pairdummy8x8sbus64cfg32nicntbf`
- `pairdummy16x16c4p8sbus128cfg32nicntbf`

The reason to prepare this plan now is the recurring Vivado high-fanout marker
under:

```text
CPUManagedStreamEngine_0/
  SIMPLENICBRIDGEMODULE_0_from_cpu_stream_incomingQueueIO_q/
    enq_ptr_value_reg[...]
```

Formal utilization also shows that `CPUManagedStreamEngine_0` is a fixed
pressure block across Gemmini mesh/sbus reductions, while
`SimpleNICBridgeModule_0` itself is small.

## Current Mechanism

`SimpleNICBridgeModule` mixes in `StreamToHostCPU` and `StreamFromHostCPU`.
Those traits convert bridge-local queue-depth values into
`StreamSourceParameters` and `StreamSinkParameters`:

```scala
def fromHostCPUQueueDepth: Int
def toHostCPUQueueDepth: Int
```

Current SimpleNIC values are hard-coded:

```scala
object TokenQueueConsts {
  val TOKEN_QUEUE_DEPTH = 3072
}

class SimpleNICBridgeModule ... {
  val fromHostCPUQueueDepth = TOKEN_QUEUE_DEPTH
  val toHostCPUQueueDepth   = TOKEN_QUEUE_DEPTH
}
```

`CPUManagedStreamEngine` then instantiates the actual CPU-managed FIFOs with:

```scala
FireSimQueueHelper.makeIO(
  UInt(BridgeStreamConstants.streamWidthBits.W),
  chParams.fpgaBufferDepth,
  isFireSim     = true,
  overrideStyle = Some(xdc.RAMStyles.ULTRA),
)
```

So reducing `TOKEN_QUEUE_DEPTH` directly reduces both SimpleNIC CPU-managed
stream FIFO depths.

## Low-Risk Implementation Shape

If both active builds fail route and the route diagnostics continue to implicate
the SimpleNIC CPU-managed stream queues, add a config-level field rather than
editing the global constant:

```scala
case object SimpleNICTokenQueueDepth extends Field[Int](TokenQueueConsts.TOKEN_QUEUE_DEPTH)
```

Then change only the two bridge values:

```scala
val fromHostCPUQueueDepth = p(SimpleNICTokenQueueDepth)
val toHostCPUQueueDepth   = p(SimpleNICTokenQueueDepth)
```

Add a debug-only config fragment in `generators/firechip/chip/src/main/scala/TargetConfigs.scala`:

```scala
class WithSimpleNICDebugTokenQueueDepth(depth: Int) extends Config((site, here, up) => {
  case firechip.goldengateimplementations.SimpleNICTokenQueueDepth => depth
})
```

Then compose only the debug build targets with this fragment. Do not change
`WithNIC`, `FireSimRocketNICNoTraceConfig`, or broad NIC defaults until a
reduced-depth bitstream passes the full gdbserver smoke matrix.

## Candidate Depths

| Depth | Expected resource effect | Functional risk |
|---:|---|---|
| `2048` | Moderate reduction, keeps a large burst cushion. | Low to medium. Best first experiment if route fails narrowly. |
| `1024` | Stronger reduction in queue storage/control fanout. | Medium. More likely to expose host/target burst backpressure. |
| `512` | High reduction. | Higher. Only use if route diagnostics strongly implicate the stream queue and 1024 is insufficient. |

Do not go below `512` for a first debug bitstream. GDB traffic is not high
bandwidth, but Linux boot, NIC handshake, and gdbserver RSP bursts still need
some buffering margin.

## Must-Preserve Functional Fixes

This queue-depth experiment must not revert the current 1bp stream fixes:

- `readRequestActive = grant && axi4.ar.valid`
- `StreamWidthAdapter` wide-to-narrow `wide_data` / `wide_valid`
- `NICTokenToBigTokenAdapter` `pcieOutQ`
- channelized SimpleNIC host IO
- from-host current/pending payload buffering
- dropping empty host-to-target tokens before they bury real payload tokens
- suppressing empty target-to-host tokens from the CPU-managed stream

Changing queue depth and reverting any of those fixes in the same build would
make the result ambiguous.

## Validation Matrix

A reduced-depth SimpleNIC bitstream is only useful if it still passes the old
1bp gdbserver capability matrix:

- `target remote`
- multiple software breakpoints
- `continue`
- `next`
- `info threads`
- `thread apply all bt`
- register reads
- disassembly
- variable/memory read and write
- thread switching
- Ctrl-C control reclaim
- `detach`

The historical passing baseline for comparison remains:

- AGFI: `agfi-0079cbbca617eca4e`
- AFI: `afi-0ee7774f829acd4de`
- Build result:
  `sims/firesim/deploy/results-build/2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz/`
- Config: `FireSimRocketNICNoTraceConfig + BaseF2Config`

## Decision Rule

Do not start this experiment while either current TIMING build is still making
progress. Start it only if:

1. Both active builds fail route, or the 12p build fails and the 8p build emits
   the same CPU-managed stream queue congestion signature.
2. The failure markers include route congestion, failed nets, node overlaps, or
   repeated high fanout rooted in `CPUManagedStreamEngine_0`.
3. The next build can be launched with one queue-depth change and no unrelated
   hardware edits.
