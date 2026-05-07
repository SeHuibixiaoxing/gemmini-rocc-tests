# 2026-05-07 dummy8x8 sbus64 DMA frontier GDB timeout

## Scope

This record captures the first GDB-first-client pipeline-runtime frontier run on
the current 4c12p12 dummy8x8 sbus64 cfg32 NIC noTrace hardware after enabling
the mapping cache.

Hardware:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`

Run host:

- Instance: `i-09daec9aac4b12c2f`
- Private IP: `192.168.1.150`
- Terminated after artifact capture.

## Commands

Image/run setup used the sbus64 workflow with mapping cache enabled:

```bash
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh \
  run 192.168.1.150
```

The GDB first-client frontier helper was:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_POST_HIT_SECONDS=8 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  192.168.1.150 172.16.0.2:2345 32345
```

The breakpoint was:

```gdb
break dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 546
```

GDB reported this as a host-evaluated condition, so every false
`dma_blocking_wait()` hit briefly trapped through GDB and slowed the run.

## Result

GDB successfully connected as the first TCP client to `gdbserver --once`,
installed the breakpoint, and hit the target frontier:

```text
Thread 2 hit Breakpoint 1, dma_blocking_wait(...)
tok->id = 546
tok->stage_idx = 0
tok->tensor_id = 2
tok->rr_manager_id = 0
tok->rr_scope_valid = 1
tok->rr_scope_external = 1
tok->hw_done_flag = 0
tok->debug_src_addr = 0x40702c00
tok->debug_dst_addr = 0x103dcb000
tok->debug_done_flag_pa = 0x46d5bf000
tok->debug_bytes = 1024
```

The stack at the breakpoint was:

```text
dma_blocking_wait
prt_dma_wait
dma_submit_wait_annotated_scoped
dma_copy_spm_pages_to_host_linux
prt_dma_copy_spm_pages_to_dram_prefix
copy_tensor_pages_to_model_alias_target
copy_tensor_pages_to_model_aliases
sync_stage_export_aliases
stage_worker_main
```

The main thread was in `clock_nanosleep()` inside `prt_runtime_run()` while the
worker thread was at the DMA wait frontier.

After the hit, the helper disabled the breakpoint, continued for 8 seconds, and
sent Ctrl-C. GDB did not regain control within 240 seconds:

```text
timeout waiting for Ctrl-C/SIGINT stop
expect_rc=13
```

This is the current card point: after entering the target DMA wait path, the
inferior becomes hard to interrupt through remote gdbserver. The last reliable
PC is the entry of `dma_blocking_wait()` for token 546. The later exact line is
not yet proven.

## Frontier context

Static reconstruction with `explain_prt_frontier_context.py` maps token 546 to:

- Segment: `0`
- Global stage: `0`
- Local stage: `0`
- Subbatch: `1`
- Layer: `0`
- Layer type: `conv`
- Split kind: `oc`
- Tensor: `2`
- Tensor role: export
- Export target: `address2` / `target_seq=1`
- Tensor bytes: `65536`
- Tensor pages: `64`
- Page: `31`
- Page byte offset: `31744`
- Transfer bytes: `1024`
- Local SPM base: `132096`
- Local SPM page address: `163840`
- Local vpage: `160`
- Source SPM address: `0x40702c00`
- Destination PA: `0x103dcb000`
- Completion flag PA: `0x46d5bf000`

The matching pipeline target is:

```text
rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64
```

## Important caveat

The post-timeout breadcrumb copied from the image still showed the last persisted
slot as `dma_program_post_src` for token 534/page19. That is older than the GDB
hit at token 546 and is not authoritative for the final PC. The likely reason is
guest file/page-cache persistence: the process was under gdbserver and then lost
the remote session without a clean runtime exit or sync.

For the next run, do not rely on the image breadcrumb alone after a GDB timeout.
The breakpoint stack and token fields are the reliable evidence from this run.

## Artifacts

Artifacts were saved under:

```text
pipeline-runtime/debug_records/artifacts/20260507T103951Z_dummy8x8_sbus64_dma_frontier_gdb_timeout/
```

Key files:

- `local/pairdummy-cfg32-dma-frontier-20260507T103951Z-192_168_1_150-172_16_0_2/pairdummy-cfg32-dma-frontier.expect.log`
- `local/pairdummy-cfg32-dma-frontier-20260507T103951Z-192_168_1_150-172_16_0_2/expect-driver.stdout`
- `local/20260507T105550Z_192_168_1_150/breadcrumb.decoded.txt`
- `local/20260507T105837Z_192_168_1_150_after_gdb_timeout/breadcrumb.decoded.txt`
- `guest/prt-debug-files.tgz`
- `manager/2026-05-07--10-31-17-runworkload-4U8DO9QUSTDRYUQ6.log`

## Next

The next GDB run should stop at token 546 and then step or break inside
`dma_blocking_wait()` before doing a long continue. The goal is to split:

- before/inside done-flag polling,
- after done-flag polling but before any hardware fence,
- inside `hw_dma_fence()`,
- after wait but inside shared ReRoCC scope release.

