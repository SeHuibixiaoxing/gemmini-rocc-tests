# Small reproducer plan for the segment0 tensor2 DMA card point

## Reproducer target

The smallest currently justified target is still the same dummy8x8/sbus64/cfg32
NIC hardware, but with batch reduced from 8 to 2:

- subbatch 0 reaches and passes the same export path;
- subbatch 1 reaches the observed token 546/page31 frontier;
- later subbatches are not needed for this card point.

The workflow default remains batch 8. A new explicit override was added for the
sbus64 dummy8x8 fixed env:

```bash
PAIRDUMMY_SBUS64_TARGET_BATCH=2
```

The default check remains:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh show
# target_batch=8
```

The reduced check is:

```bash
PAIRDUMMY_SBUS64_TARGET_BATCH=2 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh show
# target_batch=2
```

Changing this value changes the rendered guest environment, so the image must be
rebuilt/reinstalled and remote freshness must pass before trusting a batch2 run.

## Proposed batch2 run

Use the existing hardware:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Runtime config:
  `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

Suggested environment:

```bash
PAIRDUMMY_SBUS64_TARGET_BATCH=2
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1
PIPELINE_RUNTIME_BREADCRUMB_ENABLE=1
PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
```

Expected historical frontier should still be:

```text
segment=0 global_stage=0 local_stage=0 subbatch=1 tensor=2
page=31 manager=0 src=0x40702c00 bytes=1024
```

Token number is expected to remain near 546 because it is driven by the fixed
subbatch0/subbatch1 stage0 DMA sequence. Still, use a condition with a small
window rather than relying only on exact equality:

```gdb
break dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 540 && tok->id <= 560
```

Update from the first batch2 GDB path-trace run: the window condition above
does catch the batch2 export sequence, but it stops at token 540 first. Token
540 cleanly completed done-flag poll, shared ReRoCC fence, token completion, and
`dma_blocking_wait()` return. For the historical card point, the next run should
use exact token 546:

```gdb
break dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546
```

Use `PRT_GDB_PATH_TRACE_MAX_STOPS=10` so the trace stops after one token's
return path instead of rolling into token 547.

## Inconclusive batch8 exact-token attempt

The `20260507T120514Z` exact-token run should not be used as evidence about the
batch2 reproducer. Captured `runworkload` state shows the guest launched
`--batch 8`, not `--batch 2`, and the FireSim host watchdog terminated the F2
run after 1065 seconds of guest-visible file idle time. GDB connected and set:

```gdb
break dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == 546
```

but no `dma_blocking_wait()` hit was observed before `Remote connection
closed`. The watchdog capture's breadcrumb only reached
`runtime_init_done`, and sparse log tail was still in YAML pipeline parsing
around segment 8. There was no token-546 context dump.

Before the next reduced reproducer, verify both:

```bash
PAIRDUMMY_SBUS64_TARGET_BATCH=2 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh show
```

and the generated `runworkload` command or guest sparse log. The command must
show `TARGET_BATCH=2` or the guest must report `runner-enter batch=2`.

## Next GDB strategy

The previous helper continued for 8 seconds after the hit and then could not
interrupt. The next helper/run should stop at token 546 and avoid a long
continue.

Recommended sequence:

1. Break at `dma_blocking_wait` with the token window above.
2. Print `tok` fields and all threads.
3. Set a breakpoint at `dma_blocking_wait_poll_doneflag`.
4. Continue only to the poll function.
5. If poll is reached, print timeout, done flag VA/PA, and then use `finish` or
   a short Ctrl-C window to see whether it returns.
6. If poll returns `PRT_OK`, break at the lines after the poll and before the
   external ReRoCC scope release.
7. If GDB cannot reach the poll function, the card point is before or at the
   first memory/log/breadcrumb operations in `dma_blocking_wait()`, which would
   be surprising and should trigger a source-level line stepping run.

Do not use a broad host-evaluated conditional breakpoint for long runs unless
needed. GDB reported the condition as `host evals`, which makes every false
`dma_blocking_wait()` call round-trip through GDB.

## If batch2 still takes too long

A stronger software-only minimization would be to add a temporary runtime option
that stops after segment 0 or after global stage 0. That option does not exist in
the current CLI. Until it exists, batch2 is the least invasive reproducer because
the card point occurs inside segment 0 before any later segment can matter.

## Success criteria

The small reproducer is useful if it can answer one of these:

- `dma_blocking_wait_poll_doneflag()` is reached and times out: completion flag
  is not becoming visible for the problematic export DMA.
- done-flag poll returns but execution later becomes uninterruptible:
  focus shifts to token cleanup or external ReRoCC scope release.
- poll is not reached: inspect the very first lines of `dma_blocking_wait()` and
  any side effects from logs/breadcrumb/completion flag refresh.
