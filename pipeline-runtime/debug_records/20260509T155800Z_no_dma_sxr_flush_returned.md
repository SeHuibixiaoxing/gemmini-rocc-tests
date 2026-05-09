# 20260509T155800Z - no-DMA SPM xlate release/restore/flush returned under GDB

## Goal

Continue the requested no-DMA compute bisection from the prior frontier:

```text
segment=1 local_stage=0 subbatch=3
spm-xlate-release mgr=6 cfg=31 phase=release-begin prev_opc3=0x1
```

The previous run had already proved `rr_release(cfg31)` and the same-cfg
`rr_read_csr(CSR_RRCFG31)` readback returned. This run used the extended GDB
ladder to test the remaining SPM xlate release/restore window:

```text
release-end
rr_restore_opcode_binding(3, prev_binding)
prt_gemmini_spm_xlate_flush return
```

## Configuration

- Date: 2026-05-09 UTC
- AGFI/AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Run host: `i-04a749215080c3e4f`, private IP `192.168.1.220`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- RISC-V runtime SHA256:
  `ed592ca0530050c8bec12c4dbecf47879989f63a91286cb6ae92f336a3479850`
- Guest env SHA256:
  `7e075601c39f90dc6ea4ed599e3cd83461e8cf268a9cb0807aeeeaa79dfb9cea`

Important guest env:

```text
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
```

## Commands

The existing run farm was used after `infrasetup` and freshness checks. The
workload was launched with:

```bash
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1 \
PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task \
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1 \
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0 \
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3 \
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0 \
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run 192.168.1.220
```

After UART showed `[gdbserver] phase=listening`, GDB was the first TCP client:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_no_dma_sxr_release.sh \
  192.168.1.220 172.16.0.2:2345 32345
```

No `nc`, `curl`, `telnet`, or port scan touched the guest `172.16.0.2:2345`
endpoint.

## Evidence

Artifacts were copied to:

```text
pipeline-runtime/debug_records/artifacts/20260509T1558_no_dma_sxr_flush_passed_gdb/
```

Key local files:

- `pairdummy-cfg32-marker-stop.gdb.log`
- `pairdummy-cfg32-marker-stop.gdb`
- `uartlog.live.txt`
- `guest-debugfs-status.txt`
- `runworkload.pane.log`

The marker hit the intended no-DMA window:

```text
site_id = 23
segment_idx = 1
global_stage_id = 1
local_stage_id = 0
subbatch_id = 3
```

The target SPM xlate release scope was:

```text
valid = 1
cfg_id = 31
stage_id = 4294967295
manager_id = 6
opcode_id = 3
```

The GDB ladder then reached these boundaries:

```text
--- rr_release(cfg31) write returned ---
--- about to read CSR_RRCFG31 after release ---
--- release readback returned; cfg31 release CSR readback is not the stuck instruction in this run ---
--- prt_rr_release_scope returned to prt_spm_xlate_release_scope ---
--- opcode restore returned ---
--- prt_gemmini_spm_xlate_flush returned from release/restore path ---
```

At the opcode restore stop, the PC was after the `rr_swap_csr` used by
`rr_restore_opcode_binding(3U, prev_binding)`:

```text
0x2a528 <prt_spm_xlate_release_scope+428>: jal 0x294bc <rr_swap_csr>
=> 0x2a52c <prt_spm_xlate_release_scope+432>: ...
```

That proves the `RROPC3` restore write returned in this run. The final flush
return stop was:

```text
Thread 2 hit Temporary breakpoint 9,
prt_gemmini_spm_xlate_flush(manager_id=7) at prt_rerocc.c:531
```

For segment 1 local stage 0 the artifact has `splitKind: oc` and
`vAccIdxList: [0,1,2,3,4,5,6,7]`, so manager 7 is the last stage manager in
the flush loop. This run therefore rules out the stage-level SPM xlate flush
loop as the no-DMA frontier for this window.

## Interpretation

The current no-DMA card is no longer:

- artifact/YAML/rootfs reading;
- fixed-load DMA or export DMA;
- `rr_release(cfg31)`;
- post-release `RRCFG31` readback;
- `RROPC3` restore;
- `prt_gemmini_spm_xlate_flush()` for the target stage managers.

The next no-DMA frontier is after task construction and stage SPM xlate flush,
at the compute path:

```text
stage_worker_main()
  prt_gemm_conv_run()
    gemm_issue_task()
      run_conv_oc_split()
        conv_call_for_manager_sync_strided() or conv_call_for_manager_sync()
```

For segment 1 local stage 0, static artifact inspection shows `splitKind: oc`
with 8 virtual accelerators. The next GDB round should move the marker from
`worker-before-build-stage-task` to `worker-gemm-run` and split:

```text
prt_gemm_conv_run entry
gemm_issue_task
run_conv_oc_split
conv_call_for_manager_* acquire
Gemmini issue
rr_fence / gemmini_fence / drain / release
worker-export-sync
```

## Limitations

- This run does not prove no-DMA completes. It only proves the previous SPM
  xlate release/restore/flush suspect window returns.
- The GDB source line for "before opcode restore" mapped to the line after
  `rr_restore_opcode_binding()`. The useful evidence is still positive: the PC
  was after the CSR write helper, so the restore write returned.
- After GDB detached at `prt_gemmini_spm_xlate_flush()` return, UART did not
  grow. That is not by itself a compute frontier because this profile disables
  ordinary runtime guest/UART progress logs. Future rounds should rely on GDB
  markers, not on post-detach UART growth.

`doneflag` was not used as DMA completion or pass/fail evidence.

## Cleanup

The F2 run farm was terminated with:

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
cd deploy
firesim terminaterunfarm --forceterminate \
  -c config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml \
  -a config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml \
  -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml
```

The terminated instance was:

```text
i-04a749215080c3e4f
```

A follow-up AWS query showed no active `f2.*` instances.
