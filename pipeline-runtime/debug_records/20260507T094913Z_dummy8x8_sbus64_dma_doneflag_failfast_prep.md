# 20260507T094913Z dummy8x8/sbus64 DMA doneflag fail-fast preparation

## Scope

Prepared the next remote-gdbserver pipeline-runtime run on:

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware type: dummy8x8, `4c12p12`, `sbus64`, `cfg32`, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

## Why This Change

The previous no-business-breakpoint stack sampling run showed that remote GDB
could attach and interrupt early, but later failed to regain control.  The last
durable breadcrumb was:

```text
last_kind=dma last_phase=dma_wait_before_fence
seg=0 gstage=0 lstage=0 sb=1 tensor=2 mgr=0 page=31 tok=546
src=0x40702c00 dst=0x103362000 done_pa=0x103217000
```

That places the frontier immediately before or inside `hw_dma_fence()` in
`pipeline-runtime/src/prt_dma.c`.

## Software Changes Prepared

- Added an env-gated diagnostic mode:
  `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE`.
- When this flag is enabled and `--export-dma-timeout-ms` is nonzero,
  `dma_blocking_wait()` polls the DMA completion flag before entering
  `hw_dma_fence()`.
- If the completion flag does not become visible before the timeout, the wait
  returns `PRT_ERR_TIMEOUT` and records a breadcrumb instead of entering the
  harder-to-interrupt fence.
- Added breadcrumb phases:
  - `dma_wait_doneflag_poll_begin`
  - `dma_wait_doneflag_poll_done`
  - `dma_wait_doneflag_poll_timeout`
- Updated guest env rendering, workflow `show`/`debug-preflight`, local/remote
  image freshness visibility, and triage scripts so this diagnostic bit is
  visible in artifacts.
- Added `explain_prt_frontier_context.py` to reconstruct model/mapping context
  from a breadcrumb or manually supplied frontier coordinates.

This is a diagnostic/fail-fast mode, not yet a production semantic change:
when enabled, completion-flag visibility is treated as enough to skip the local
DMA idle fence.

## Frontier Context Reconstruction

Command:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/explain_prt_frontier_context.py \
  --model-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
  --pipeline-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.ours2.yaml \
  --layer-mapping-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml \
  --kind dma --phase dma_wait_before_fence \
  --segment 0 --global-stage 0 --local-stage 0 --subbatch 1 \
  --tensor 2 --token 546 --manager 0 --page 31 \
  --src 0x40702c00 --dst 0x103362000 --aux0 0x103217000 --aux1 5000000000
```

Key result:

- `segment=0`, `global_stage=0`, `local_stage=0`, `subbatch=1`
- Stage maps to layer `0`, op `conv`, split kind `oc`
- `tensor=2` is the stage export tensor
- Tensor page count is `64`, tensor bytes is `65536`
- `page=31` corresponds to byte offset `31744`
- Local SPM page context:
  - tensor local SPM base: `132096`
  - page local SPM address: `163840`
  - tensor first vpage: `129`
  - page local vpage: `160`
  - page transfer bytes: `1024`

This is now enough to build a smaller reproduction around layer0/stage0
export of tensor2, and to scope trigger windows to page31/token546 if the next
run confirms this frontier.

## Verification

Syntax and Python checks:

```bash
bash -n generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
bash -n generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh
bash -n generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh
python3 -m py_compile \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/triage_prt_capture.py \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/explain_prt_frontier_context.py \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/decode_prt_breadcrumb.py
```

All passed.

Cross build:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests \
  -f /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile \
  abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests \
  src_dir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests \
  XLEN=64 \
  CC_LINUX=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc \
  rerocc_pipeline_runtime-linux -j1
```

Result: passed.

Workflow visibility:

```text
profile_id=pairdummy-sbus64-dummy8x8-fixed-v2
dma_force_direct_enable=1
dma_blocking_wait_poll_timeout_enable=1
export_dma_timeout_ms=5000
debug_preflight_status=pass
```

Manual rendered guest env confirmed:

```text
export PIPELINE_RUNTIME_PROFILE_ID='pairdummy-sbus64-dummy8x8-fixed-v2'
export EXPORT_DMA_TIMEOUT_MS='5000'
export PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE='1'
export PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE='1'
```

## Next Test

Refresh the workload image without using a directory-clean command unless
explicitly approved, run FireSim/infrasetup with the current AGFI, attach GDB
as the first TCP client, and sample again.

Expected discriminators:

- Breadcrumb `dma_wait_doneflag_poll_timeout`: DMA completion flag never became
  visible; focus on programmed DMA src/dst/done PA and hardware completion.
- Breadcrumb `dma_wait_doneflag_poll_done` followed by progress: original
  blocker is likely the local DMA fence/idle semantics.
- Progress reaches `dma_wait_before_shared_fence` or later: move the frontier
  to shared RR fence/release cleanup.

