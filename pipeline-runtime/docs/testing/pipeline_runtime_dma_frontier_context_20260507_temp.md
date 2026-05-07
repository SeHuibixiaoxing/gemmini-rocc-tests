# Pipeline Runtime DMA frontier context, 2026-05-07

## Current finding

The current dummy8x8/sbus64/cfg32 NIC noTrace bitstream can boot Linux, start
networked `gdbserver`, and let host GDB break in pipeline-runtime. The first
pipeline-runtime card point reproduced so far is not a startup or mapping parse
issue. It is in the segment 0 export DMA path.

The reliable GDB frontier is:

```text
dma_blocking_wait()
token=546
segment=0 global_stage=0 local_stage=0 subbatch=1
tensor=2 manager=0 page=31
src=0x40702c00 dst_pa=0x103dcb000 done_pa=0x46d5bf000 bytes=1024
```

Call path:

```text
stage_worker_main
sync_stage_export_aliases
copy_tensor_pages_to_model_aliases
copy_tensor_pages_to_model_alias_target
prt_dma_copy_spm_pages_to_dram_prefix
dma_copy_spm_pages_to_host_linux
dma_submit_wait_annotated_scoped
prt_dma_wait
dma_blocking_wait
```

At the breakpoint:

- worker thread is at `dma_blocking_wait()` entry;
- main thread is sleeping in `prt_runtime_run()` while waiting for segment
  progress;
- `tok->hw_done_flag == 0`;
- `tok->rr_scope_valid == 1`;
- `tok->rr_scope_external == 1`.

After continuing from this breakpoint, Ctrl-C through gdbserver did not regain
control within 240 seconds. This makes the exact later instruction unresolved,
but places the failure immediately after entry to the token 546 wait path or in a
subsequent uninterruptible hardware wait reached shortly after.

## Mapping reconstruction

The matching pipeline target is:

```text
rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64
```

Stage context:

- segment: `0`
- local stage: `0`
- global stage: `0`
- layer: `0`
- layer type: `conv`
- split: `oc`
- acc utilization: `8`
- assigned virtual accelerators: `[0,1,2,3,4,5,6,7]`
- tensors in stage: `[1000000, 1000001, 0, 2]`
- entry tensor: `0`
- export tensor: `2`
- fixed tensors: `1000000`, `1000001`

Tensor 2 context:

- role: export
- tensor index in stage: `3`
- local SPM base: `132096`
- first local vpage: `129`
- page count: `64`
- tensor bytes: `65536`
- frontier page: `31`
- frontier page byte offset: `31744`
- frontier page local SPM address: `163840`
- frontier local vpage: `160`
- frontier transfer bytes: `1024`

Layer 0 model context:

- type: `conv`
- inputs: `[0]`
- outputs: `[2]`
- tensor IDs: `[1000000, 1000001, 0, 2]`
- tensor 2 size: `65536`
- layer mapping first source candidate: `40`
- mapping tile: `[1, 128, 64, 256, 1, 1, 1, 1]`
- DRAM bypass: `[0, 0, 0, 0]`
- SPM bypass: `[1, 1, 1, 1]`

## Why the old breadcrumb tail is misleading

After GDB timed out, the file-backed breadcrumb in the guest image still decoded
to:

```text
last_kind=dma
last_phase=dma_program_post_src
subbatch=1 tensor=2 token=534 page=19
```

This is older than the GDB breakpoint at token 546. Treat it as a stale persisted
file view, not as the final hardware PC. The GDB transcript is authoritative for
the token 546 entry.

## Current uncertainty

The unresolved split is inside `dma_blocking_wait()` after the entry breakpoint:

1. `dma_completion_flag_refresh(tok)`
2. breadcrumb/log before the wait
3. optional done-flag polling controlled by
   `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1`
4. `hw_dma_fence()` only if done-flag polling was not used
5. completion refresh and token cleanup
6. external ReRoCC scope fence/release

Because Ctrl-C did not regain control after a long continue, the next run should
not continue past the breakpoint blindly. It should step or set narrower
breakpoints inside this function.

## Evidence

Main artifact directory:

```text
pipeline-runtime/debug_records/artifacts/20260507T103951Z_dummy8x8_sbus64_dma_frontier_gdb_timeout/
```

Most important files:

- GDB transcript:
  `local/pairdummy-cfg32-dma-frontier-20260507T103951Z-192_168_1_150-172_16_0_2/pairdummy-cfg32-dma-frontier.expect.log`
- GDB stdout:
  `local/pairdummy-cfg32-dma-frontier-20260507T103951Z-192_168_1_150-172_16_0_2/expect-driver.stdout`
- decoded stale breadcrumb after timeout:
  `local/20260507T105837Z_192_168_1_150_after_gdb_timeout/breadcrumb.decoded.txt`

