# Pipeline Runtime P0 Completion Status

更新时间：`2026-05-07 07:25 UTC`

本文按
[`pipeline_runtime_optimization_actions_20260505_draft.md`](pipeline_runtime_optimization_actions_20260505_draft.md)
的 P0 要求记录当前完成状态。

## 结论

P0 当前已完成到“可用 gdbserver + 默认 fail-fast 合同 + 页数/allocator 域修正”的状态。
这不等于 pipeline-runtime 计算已经通过；它表示后续卡死调试已有可 attach 的 F2
现场和更早的运行前合同检查。

## P0-1：先保证可调试

已完成：

- 当前可用 AGFI：`agfi-077451484fe3b63c3`
- AFI：`afi-07989ce9ce725a690`
- target：
  `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`
- 结果：remote `gdbserver` expect triage `PASS`
- 覆盖：`target remote`、线程/栈/寄存器/反汇编、变量读写、内存读写、
  `next`、多个 software breakpoint、Ctrl-C 抢回控制、`detach`

证据：

- [`20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass.md)
- [`20260507T061559Z_dummy8x8_sbus64_gdbserver_extended_matrix_pass.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260507T061559Z_dummy8x8_sbus64_gdbserver_extended_matrix_pass.md)

限制：

- 这条已通过路线是 `dummy8x8 / sbus64 / no TraceIO`，不是更大的
  `dummy16x16 / sbus128` 主线。
- 本轮验证只声明 gdbserver 链路可用，不声明 pipeline-runtime workload 完成。

## P0-2：运行前 fail-fast 校验

已完成并默认启用：

- `pages_per_acc * page_size_bytes == shared_spad_local_size_bytes` 静态审计。
- `accUtil`、`vAccIdxList`、`pAccIdxList` 的数量和边界审计。
- stage local SPM tensor `addr + bytes` 不越过 stage window 或 slot vpage window。
- stage `[execBaseVPage, execBaseVPage + localSpmPageSpan)` 不越过 segment alias window。
- 同一 segment 内 stage SPM alias window 不允许重叠；运行时也会在
  `runtime_prepare_stage_spm_windows()` fail-fast。
- model layer `address` / `address2` + `tensorSize` 不越过 model top-level address span。
- buffer binding `pages_per_slot * page_size` 必须覆盖对应 tensor 的最大 local bytes。
- ring/transport effective bytes、ring binding pages、RR cfg stage budget 等 artifact 合同审计。
- DMA manager ownership 运行时断言：`prt_dma_submit` / copy path 必须使用当前 stage
  绑定的 DMA manager，pair-manager 模式下允许对应 Gemmini manager。

仍然留给 P1/P2 的内容：

- action 级 shared-scope overlap 语义；
- 稳定 SPM binding；
- 更细粒度的 per-manager DMA status/idle/error CSR；
- no-DMA compute 二分。

## P0-3：页数和 allocator 模型

已完成：

- `prt_cfg_spm_manager_count(cfg)` 独立于 CPU core 数，优先使用 `num_gemmini_mgrs`。
- `prt_cfg_spm_total_pages(cfg)` 使用 `spm_manager_count * pages_per_acc`。
- page allocator、idle check、SPM PTE chunk sizing、默认 xlate range 和 allocation order
  使用 SPM manager domain。
- init log 包含 `spm_mgrs`，下一次 FPGA workload 初始化仍要确认：

```text
cores=4 gemmini=12 dma=12 spm_mgrs=12 pages_per_acc=1024 page_bytes=1024
```

## 本轮验证

构建：

```bash
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime all
```

正向检查：

```bash
for method in ours2 gemini2 tangram2; do
  python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py \
    --pipeline-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.${method}.yaml \
    --hardware-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/hardware_target.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml \
    --model-yaml conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml \
    --expect-target-key rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128 \
    --page-size-bytes 1024
done
```

结果：

```text
P0_POSITIVE_PASS method=ours2
P0_POSITIVE_PASS method=gemini2
P0_POSITIVE_PASS method=tangram2
P0_DRYRUN_PASS method=ours2
P0_DRYRUN_PASS method=gemini2
P0_DRYRUN_PASS method=tangram2
```

负向检查：

```text
P0_NEGATIVE_PASS page_size
P0_NEGATIVE_PASS model_range
P0_NEGATIVE_PASS stage_window_overlap
P0_NEGATIVE_PASS runtime_stage_window_overlap
```

这些负向检查分别证明：错误 SPM page size、model tensor 地址越界、artifact stage
SPM window 重叠、运行时 stage SPM window 重叠都会 fail-fast。
