# DMA manager contract check checkpoint

Timestamp: `2026-05-05T15:20:12Z`

## Context

The active cfg32 NIC bitstream build `pairdummy-cfg32-nic-mainline-20260505T132956Z`
is still in local GoldenGate/MidasTransforms. While waiting, I added a software
runtime guard for one of the current design risks: in pair-manager mode, DMA and
Gemmini share the same local manager id, so a wrong DMA `manager_id` can look
like a hardware fence hang instead of a clear software contract violation.

## Change

`src/prt_dma.c` now validates DMA manager ownership before:

- `dma_batch_scope_acquire`
- direct `prt_dma_submit`
- host `prt_dma_copy_spm_va` fast path

The check uses the active action exec table:

- `stage_idx` must be in range.
- `manager_id` must match `stage_dma_ids[stage_idx]`.
- In pair-manager mode, any manager in `stage_mgr_ids[stage_idx][0..stage_tile_count)`
  is also accepted because DMA and Gemmini manager ids are intentionally paired.
- In non-pair mode, only the assigned DMA manager is accepted.

This guard should turn accidental manager misuse into a direct `PRT_ERR_STATE`
or `PRT_ERR_INVAL` instead of letting the run proceed into a low-information
DMA wait/fence hang.

## Verification

Build:

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j2 \
  CFLAGS='-O0 -g -Wall -Wextra -Werror -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=unused-but-set-variable -std=gnu11'
```

Result: exit 0. Existing warnings remain downgraded by the explicit
`-Wno-error` flags.

Basic runtime validation:

```bash
./pipeline_runtime --hw-validate-only --backend cpu
```

Result: `HW_VALIDATE_ONLY_PASS`.

CPU dry-run:

```bash
DIR=/home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
./pipeline_runtime \
  --backend cpu \
  --model-yaml "$DIR/model.layers.yaml" \
  --layer-mapping-yaml "$DIR/gemmini_layer_mapping.$TARGET.yaml" \
  --skip-model-bin-load \
  --pipeline-yaml "$DIR/pipeline_mapping.$TARGET.ours2.yaml" \
  --skip-input-load \
  --skip-golden-check \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 12 \
  --gemmini-base-id 0 \
  --dma-base-id 0 \
  --pair-manager-mode 1 \
  --pages-per-acc 1024 \
  --spm-page-bytes 1024 \
  --batch 8 \
  --watchdog-ms 300000
```

Result: exit 0.

## Limitations

This is still a software-only checkpoint. It does not prove the new cfg32 NIC
AGFI, DMA hardware, or remote gdbserver on the pairdummy target. It only ensures
that one class of manager ownership bugs fails before hardware DMA wait/fence
debugging becomes ambiguous.
