# 2026-05-08 08:55Z dummy8x8 sbus64 GDB page15 marker missed; fixed-load page63 frontier

## Scope

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Run host: `i-0ff4f274d766b8903`, private IP `192.168.1.5`
- Target marker env:
  `dma-export-page-submit-begin`, segment `1`, global stage `1`, local stage `0`,
  subbatch `7`, manager `0`, tensor `3`, page `15`
- Artifact directory:
  `pipeline-runtime/debug_records/artifacts/20260508T085501Z_dummy8x8_sbus64_gdb_page15_missed_fixedload63/`

## What changed before this run

The host-side helper
`scripts/run_pairdummy_cfg32_gdbserver_export_page_frontier.sh` was corrected
before attach so its later post-marker breakpoints target the actual current
`prt_dma.c` call-site lines:

- before `hw_dma_fence()`: `prt_dma.c:3695`
- after `hw_dma_fence()`: `prt_dma.c:3699`
- before external shared RR fence: `prt_dma.c:3774`

This change is host-side GDB command generation only. It does not change the
guest binary or hardware behavior.

## GDB result

GDB was the first TCP client to `gdbserver --once`; the guest port was not probed
with `nc`, `curl`, or `telnet`.

Command:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_EXPORT_STAGE=0 \
PRT_GDB_EXPORT_MANAGER=0 \
PRT_GDB_EXPORT_TENSOR=3 \
PRT_GDB_EXPORT_PAGE=15 \
PRT_GDB_MARKER_TIMEOUT=1800 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_export_page_frontier.sh \
  192.168.1.5 172.16.0.2:2345 32345
```

The helper connected and installed `break prt_gdb_marker_stop`, but the requested
page15 export marker did not hit. After live logs showed the run was still in
segment 0, I sent `SIGINT` to GDB to try to obtain a stack. In this batch-mode
helper that caused:

```text
Error in sourced command file:
Disconnected from target.
```

No stack was recovered from that interrupt. Treat this as evidence that this
specific helper is not suitable for post-facto stack sampling when the first
marker is too late.

## Runtime frontier

The target never reached segment 1. Live sparse log stopped in segment 0:

```text
[prt-progress] stage-fixed-load-sparse phase=begin segment=0 stage=0 slot=1 tensor=1000001 pages=64 bytes=65536 lazy=0 reuse=0 dma=0
[prt-progress] rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1
```

Live breadcrumb was more precise than the sparse log. It showed all fixed-load
pages for the tensor had completed through page `63`, then the final external
RR scope release completed:

```text
last_kind=rr last_phase=rr_release_end
last_slot seg=0 gstage=0 lstage=0 sb=0 tensor=any mgr=0 rc=0 line=404
...
tensor=0 page=63 tok=64 phase=dma_submitwait_after_cleanup rc=0 flags=0x7
tensor=0 page=63 phase=dma_page_end rc=0
rr_release_end mgr=0 cfg=0 line=404
```

Interpretation:

- This run did not test the intended segment1 tensor3 page15 frontier.
- It regressed or reproduced an earlier early fixed-load frontier.
- The currently visible window is after the tensor `1000001` fixed-load DMA page
  loop has completed and immediately after `dma_batch_scope_release()` /
  `prt_rr_release_scope()` for manager 0 opcode 2.
- The next missing boundary is the caller-side return from
  `prt_dma_copy_dram_to_spm_pages()` back to `stage_prepare_exec_views()`,
  followed by `stage-fixed-load-sparse phase=end`.

## Constraints checked

- Doneflag polling was not used as completion evidence. The run config kept
  `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0`; the blocking path
  remains `hw_dma_fence()` plus RR scope fencing.
- The GDB marker miss is not evidence that the new segment/global/subbatch marker
  context is broken, because execution never reached the targeted segment/page.
- The helper line-number fix remains valid and should be committed with this
  failed milestone.

## Next probe

Use a fresh `gdbserver --once` run and stop earlier, before the segment1 page15
marker:

1. Start with a marker at `dma-fixed-load-submitwait-begin` or
   `dma-fixed-load-submitwait-end` for segment `0`, stage `0`, subbatch `0`,
   manager `0`, tensor `1000001`, page `63`.
2. After the marker hits, keep one GDB session and dynamically set breakpoints at:
   `dma_submit_wait_annotated_scoped`, `dma_blocking_wait`,
   `dma_gdb_marker_wait_return`, `prt_dma_copy_dram_to_spm_pages`,
   `stage_prepare_exec_views`, and the `stage-fixed-load-sparse phase=end`
   source line.
3. Avoid using the late export-page helper for stack sampling; if a marker is
   speculative, use the stack-sampling helper or an earlier source marker so
   `Ctrl-C` is part of the scripted flow instead of an external signal.
