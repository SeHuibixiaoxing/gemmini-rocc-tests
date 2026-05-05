# 20260505T142049Z - host build and CPU artifact dry-run checkpoint

## Goal

While the `12p4c128sbus32cfg + optimized DMA + current NIC` F2 bitstream build
continues in `GoldenGateMain`, make the local pipeline-runtime binary buildable
on the x86 host and run a small CPU-backend artifact dry-run. This checkpoint is
intended to catch software/configuration regressions before the new AGFI is
available.

## Code changes

- `src/prt_dma.c`
  - adds `dma_cpu_fence_rw()`;
  - keeps the RISC-V `fence rw, rw` on RISC-V builds;
  - uses a compiler memory barrier on non-RISC-V host builds;
  - replaces the completion-flag acquire/release fences with the wrapper.
- `src/prt_runtime.c`
  - adds `runtime_cpu_fence_rw()`;
  - keeps the RISC-V `fence rw, rw` on RISC-V builds;
  - uses a compiler memory barrier on non-RISC-V host builds;
  - replaces the `prefault_and_lock_blob()` fence with the wrapper.

These changes are host-build hygiene only. They do not alter the RISC-V
instruction stream or the active bitstream build inputs.

## Verification

Host build:

```bash
cd generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime
make clean >/dev/null 2>&1 || true
make -j2 CFLAGS='-O0 -g -Wall -Wextra -Werror -Wno-error=unused-function -Wno-error=unused-variable -Wno-error=unused-but-set-variable -std=gnu11'
```

Result:

- linked `pipeline_runtime`;
- existing unused debug/probe warnings remain, but are explicitly downgraded for
  this host-only check.

Runtime init smoke:

```bash
./pipeline_runtime --hw-validate-only --backend cpu
```

Result:

```text
HW_VALIDATE_ONLY_PASS
```

CPU backend artifact dry-run:

```bash
timeout 180 ./pipeline_runtime \
  --backend cpu \
  --model-yaml ../rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml \
  --layer-mapping-yaml ../rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
  --pipeline-yaml ../rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml \
  --batch 1 \
  --num-cores 4 \
  --num-gemmini-mgrs 12 \
  --num-dma-mgrs 12 \
  --pair-manager-mode 1 \
  --gemmini-base-id 0 \
  --dma-base-id 0 \
  --spm-page-bytes 1024 \
  --pages-per-acc 1024 \
  --skip-model-bin-load \
  --skip-input-load \
  --skip-golden-check
```

Result:

- exit code 0 in about 5 seconds;
- no stdout/stderr diagnostics.

The command above matches the FireMarshal fixed profile's `PAGES_PER_ACC=1024`.
An earlier local dry-run used `--pages-per-acc 256`; that was only a weaker
host-side smoke and is not the acceptance configuration for this target.

## Configuration pitfall caught

The same CPU dry-run with `--spm-page-bytes 4096` fails before execution:

```text
prepare_stage_spm_windows: stage=0 slot=1 tensor=1000001 addr=1024 bytes=65536 end=66560 vpage=1 pages=64 span=193 window_bytes=790528 slot_bytes=[4096,266240)
runtime_run: prepare_stage_spm_windows failed rc=parse_error(-10)
runtime_run failed: parse_error (-10)
```

This is expected for the current mapper artifacts. Their
`localSpmTensorAddrList` and `localSpmFirstVPageList` are expressed with a
1024-byte SPM page granularity. The runtime fail-fast check is therefore useful:
it catches a workflow/config mismatch before DMA/Gemmini issue.

## Build status at this checkpoint

F2 build session:

- tmux: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- pane log:
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-cfg32-nic-mainline-20260505T132956Z.pane.log`
- manager log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-05--13-29-57-buildbitstream-C1F6YJIUOD2RJEE0.log`

Observed state:

- no exitcode file yet;
- tmux still alive;
- still in local `GoldenGateMain`;
- no z1d/f2 EC2 build/run instance observed yet.

## Limitations

This does not validate FPGA DMA, NIC/gdbserver, or RISC-V Linux behavior. It only
proves that the current host build can link and that the bertmini artifacts pass
the runtime init/scheduling path under the CPU backend with the correct SPM page
granularity.
