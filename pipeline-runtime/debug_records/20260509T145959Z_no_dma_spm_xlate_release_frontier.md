# 20260509T145959Z - no-DMA compute reached SPM xlate release stall

## Goal

Run the requested no-DMA compute bisection on the current
`dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` F2 profile after fixing the
payload/image freshness issue, and determine whether the workload still stops
before artifact loading, in DMA, or in the Gemmini/SPM/ReRoCC path.

## Configuration

- Date: 2026-05-09 UTC
- AGFI: `agfi-077451484fe3b63c3`
- F2 instance: `i-0ed559828a2a736a6`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- Runworkload tmux:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260509-144353`
- Guest environment:
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=0`
  - `PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE=0`
- RISC-V payload SHA256:
  `ed592ca0530050c8bec12c4dbecf47879989f63a91286cb6ae92f336a3479850`
- Remote image SHA256:
  `6e5dac99b4ed9cc1bad1b7e4a08bc9c0cd5e6d7f4a6669f11bc7485f8d4e5d78`

## Evidence

Artifacts were copied to:

```text
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/20260509T1458_no_dma_sbus64_spm_xlate_release_stall/
```

Key files:

- `bertmini-batch8.log`
- `bertmini-batch8.status`
- `bertmini-batch8.runner.stage`
- `uartlog`
- `host-watchdog.log`
- `runworkload-pane.log`
- `SHA256SUMS`

The F2 instance was terminated after artifact collection. A later AWS query
showed the instance in `shutting-down`; no new F2 test should start until this
is rechecked as fully gone.

## Observations

The earlier "program does not enter runtime" symptom was not reproduced after
forcing a fresh RISC-V payload and FireMarshal image. UART reached:

```text
[prt-early] calling runtime_run
```

The no-DMA run skipped real DMA transfer paths as intended and progressed into
Gemmini/SPM compute:

```text
[prt-progress] runtime begin backend=0 batch=8 watchdog_ms=600000 no_dma_compute=1 ...
```

Segment 0 stage 0 completed all observed subbatches and exited the worker after
subbatch 8. Segment 1 stage 0 then progressed through subbatches 0, 1, and 2.
For segment 1 stage 0 subbatch 2, manager 6 completed pointwise compute and
fence:

```text
[prt-progress] conv-sync-strided stage=0 mgr=6 fence-end rc=0
[prt-progress] worker stage=0 subbatch=2 compute-done
[prt-progress] worker stage=0 subbatch=2 done
```

The next subbatch entered `stage_prepare_exec_views()` and successfully flushed
managers 0 through 5. The final frontier is:

```text
[prt-progress] no-dma-compute c1-skip stage=0 tensor=0 idx=0 subbatch=3 bytes=65536 ring=0
[prt-progress] stage-exec-views phase=begin segment=1 stage=0 layer=1 tensors=4
...
[prt-progress] spm-xlate-flush mgr=6 cfg=31 prev_opc3=0x1
[prt-progress] spm-xlate-flush-call mgr=6 cfg=31 phase=issue-begin prev_opc3=0x1
[prt-progress] spm-xlate-flush-call mgr=6 cfg=31 phase=issue-end prev_opc3=0x1
[prt-progress] spm-xlate-release mgr=6 cfg=31 phase=fence-begin prev_opc3=0x1
[prt-progress] spm-xlate-release mgr=6 cfg=31 phase=fence-end prev_opc3=0x1
[prt-progress] spm-xlate-release mgr=6 cfg=31 phase=release-begin prev_opc3=0x1
```

Thirty seconds later `bertmini-batch8.log` was still `314124` bytes, with no
new `release-end` line. Therefore the stuck window is inside
`prt_rr_release_scope(scope)` called from `prt_spm_xlate_release_scope()` for
`manager_id=6`, `cfg_id=31`, `opcode_id=3`.

## Static Interpretation

At this frontier the artifact read path, mapping cache, fixed-load DMA, export
DMA, and DMA completion path are no longer the primary explanation for this
run. The no-DMA bisection moved the active blocker to the ReRoCC/Gemmini SPM
xlate path.

The exact remaining static window is:

```c
rr_release(scope->cfg_id);
(void)rr_read_csr(CSR_RRCFG0 + scope->cfg_id);
```

in `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c`.
The existing progress log does not distinguish whether the hang is the release
CSR write itself or the same-cfg readback used as a release-ack retire point.

This is not a simple "cfg31 always fails" issue. The same log contains many
successful `cfg=31` xlate release cycles, including earlier manager 6 releases
with `prev_opc3=0x1`, and earlier records show cfg31 xlate flush/release can
complete on the same AGFI. The failing condition is later in repeated
stage-0 no-DMA compute execution, at segment 1 / stage 0 / subbatch 3.

The local docs are partly stale: `docs/software_rerocc_api_guide.md` and
`docs/software_spm_xlate_api_guide.md` still describe `RR_MAX_CFGS=16` and
`cfg15`. Current `rerocc_control.h` uses `RR_MAX_CFGS=32`, and the runtime
reserves `PRT_RR_SPM_XLATE_CFG_ID = 31`.

## Next Step

Do not add broad logging first. The next F2 round, if needed, should be a
gdbserver round with the same AGFI/config family and no-DMA enabled, with a
narrow ladder around:

```text
stage_prepare_exec_views
runtime_flush_stage_spm_xlate
prt_gemmini_spm_xlate_flush
prt_spm_xlate_release_scope
prt_rr_release_scope
```

Once stopped in `prt_rr_release_scope` for the target manager/cfg, use
line stepping or temporary breakpoints around the two CSR operations to separate
`rr_release(cfg31)` from the post-release `rr_read_csr(CSR_RRCFG31)` readback.

Before launching another F2 run, recheck:

- no active/stale `f2.*` instances;
- RISC-V payload SHA and remote image SHA freshness;
- guest no-DMA env is enabled;
- gdbserver binary path uses
  `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb`.
