# 20260505T173000Z hw-validate-only pair-manager check

## Goal

Run a small host-side validation while the cfg32 NIC F2 bitstream is still building.
This does not replace FPGA testing; it only checks that the current runtime init contract rejects an invalid
pair-manager configuration and accepts the intended 4 core / 12 Gemmini / 12 DMA layout.

## Commands and results

Valid cfg32-style pair-manager layout with SPM xlate enabled:

```bash
./generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --hw-validate-only \
  --backend cpu \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 12 \
  --pair-manager-mode 1 \
  --pages-per-acc 1024 \
  --spm-page-bytes 1024 \
  --spm-xlate-enable 1 \
  --watchdog-ms 300000
```

Result:

```text
HW_VALIDATE_ONLY_PASS
```

Valid cfg32-style pair-manager layout with SPM xlate disabled:

```bash
./generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --hw-validate-only \
  --backend cpu \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 12 \
  --pair-manager-mode 1 \
  --pages-per-acc 1024 \
  --spm-page-bytes 1024 \
  --spm-xlate-enable 0 \
  --watchdog-ms 300000
```

Result:

```text
HW_VALIDATE_ONLY_PASS
```

Negative test: pair-manager mode with mismatched DMA manager count:

```bash
./generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --hw-validate-only \
  --backend cpu \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 11 \
  --pair-manager-mode 1 \
  --pages-per-acc 1024 \
  --spm-page-bytes 1024 \
  --spm-xlate-enable 1 \
  --watchdog-ms 300000
```

Result:

```text
runtime_init: pair_manager_mode requires num_dma_mgrs (11) to match num_gemmini_mgrs (12)
runtime_init failed: invalid (-2)
```

Exit status: `1`, as expected.

## Conclusion

The current runtime still accepts the intended cfg32 pair-manager layout and rejects a mismatched
DMA/Gemmini manager count before any workload or hardware command is issued.

This is a narrow host-side contract test. It does not prove SPM xlate programming, DMA movement,
Gemmini execution, NIC attach, or FPGA `gdbserver` behavior.
