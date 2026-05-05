# 20260505T182000Z - Hardware wait static blocker review

## Goal

Continue static triage while the cfg32 NIC bitstream builds are running. This
review focuses on why pipeline-runtime can still look "hung" even though it has
watchdog and condition-variable timeouts.

## Finding

The software wait paths and hardware wait paths have different timeout
properties:

- Pipe/ring condition-variable waits use short timed waits and retry in the
  worker loop.
- ReRoCC acquire has `PRT_RR_ACQUIRE_MAX_RETRIES=1000000`.
- DMA blocking wait, ReRoCC fence, and Gemmini fence have no software timeout
  around the actual custom instruction/fence path.

The important consequence is that the outer segment watchdog can set
`stop_requested` and `fatal_error`, but it can still hang in `pthread_join()` if
a worker is already stopped inside `hw_dma_fence()`, `rr_fence()`, or
`gemmini_fence()`.

## Evidence

Read paths:

- `src/prt_scheduler.c`
  - `cond_wait_pred()` supports `pthread_cond_timedwait()`.
- `src/prt_runtime.c`
  - stage worker clamps wait timeout to `10000000ns`;
  - segment watchdog sets `rt->fatal_error = PRT_ERR_TIMEOUT`;
  - then joins all worker threads.
- `src/prt_dma.c`
  - `dma_blocking_wait()` calls `hw_dma_fence()` directly; `timeout_ns` is only
    used for logs/breadcrumbs.
- `include/rerocc_coupleddma.h`
  - `rerocc_coupleddma_wait()` emits `FUNCT_CHECK_COMPLETION`.
- `GemminiCoupledDMA.scala`
  - `readyFence = isFence && canAcceptRespCmd && canCompleteFence`;
  - `canCompleteFence = !dmaBusy`.
- `src/prt_gemmini_adapter.c`
  - blocking fence eventually calls `prt_rr_fence_scope()` and `gemmini_fence()`.
- `rerocc-linux-tests/rerocc_control.h`
  - `rr_fence()` writes `CSR_RRBAR` and executes `asm volatile("fence")`.

## Actionable GDB split

Once a new cfg32 NIC AGFI is available and remote gdbserver attach works, first
run:

```gdb
info threads
thread apply all bt
thread apply all p/x $pc
thread apply all x/6i $pc
```

Interpretation:

- `pthread_cond_timedwait` / pipe/ring helpers: software scheduling or buffer
  state.
- main thread in `pthread_join` plus worker in hardware fence: watchdog fired
  but cannot reclaim a hardware-blocked worker.
- worker in `hw_dma_fence`: DMA FSM/TL/SPM xlate/ReRoCC scope first; completion
  flag PA/cache second.
- worker in `gemmini_fence` or `prt_rr_fence_scope`: Gemmini command drain,
  RRCFG/RROPC binding, or manager ownership first.

## Docs

Detailed note added:

- `docs/testing/pipeline_runtime_static_hardware_wait_blockers_20260505.md`

## Build state

No bitstream completed during this review. The mainline cfg32 NIC build is still
running in Vivado before AGFI/AFI generation. The noTrace cfg32 NIC build is
still in local GoldenGate and has not yet emitted `FireSim-generated.sv`.
