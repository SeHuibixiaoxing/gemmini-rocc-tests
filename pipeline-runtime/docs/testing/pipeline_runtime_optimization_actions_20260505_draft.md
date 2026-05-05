# Pipeline Runtime 优化与修复措施草案

更新时间：`2026-05-05 13:20 UTC`

本文把 [`问题.md`](问题.md) 中的优化点和风险点转成可执行措施。优先级按“正确性先于性能、可调试性先于复杂 overlap”排序。

## P0：先保证可调试

1. 完成 `12p4c128sbus32cfg + optimized DMA + current NIC` bitstream 构建，并用 remote gdbserver 验证 attach。
2. gdbserver attach 成功后，把 pipeline-runtime 卡死现场分成：
   - pipe/ring wait；
   - DMA submit/wait/fence；
   - ReRoCC acquire/fence/release；
   - Gemmini tiled conv/pointwise；
   - thread join/stop/fatal error。
3. 每次关键测试 commit，commit message 写清 AGFI/AFI、配置、命令、结果、证据目录和限制。

## P0：运行前 fail-fast 校验

新增或强化以下校验：

- `pages_per_acc * page_size_bytes == shared_spad_local_size_bytes`。
- page allocator 总页数使用 SPM manager 数，pair manager 目标使用 `num_gemmini_mgrs`。
- 每个 stage 的 `acc_util == manager_ids` 数量，且 manager id 不超过硬件 manager 数。
- 每个 tensor 的 `base + byte_size` 不越过对应 host buffer、SPM slot 或 alias window。
- 每个 DMA request 的 `src/dst/bytes` 落在已登记范围内。
- 每个 Gemmini conv/resadd 的 input/weight/bias/output 推导范围落在已登记范围内。
- 每个 stage 的 manager set 与 SPM page set 与其他并行 stage 的交集必须被 artifact 显式声明为共享。

建议先把这些校验做成默认开启；性能稳定后再考虑 release build 降低日志。

当前状态：`2026-05-05T140859Z` 已补第一版 SPM tensor bounds 校验。
`audit_pipeline_runtime_artifact.py` 会静态检查 `localSpmTensorAddr + bytes`
是否落在对应 `localSpmFirstVPage/localSpmPageCount` 和 stage window 内；
`runtime_prepare_stage_spm_windows` 会在运行前做同样校验，并检查 stage
`exec_base_vpage + local_spm_page_span` 不越过 action alias window。当前
`bertmini/ours2/pairdummy-sbus128` artifact 通过该审计。

`2026-05-05T142049Z` 本地 CPU backend 干跑进一步确认：这批 mapper artifact 的
SPM page 粒度必须按 `1024` 字节解释。用 `--spm-page-bytes 4096` 会在
`runtime_prepare_stage_spm_windows` 阶段立刻报出 stage 0 / tensor `1000001`
的 `localSpmTensorAddr=1024` 不在 slot 1 window 内；改为
`--spm-page-bytes 1024` 后，同一组 `bertmini/ours2/pairdummy-sbus128`
YAML、`--backend cpu`、`--batch 1`、跳过 model/input/golden 的干跑退出 0。
因此该 fail-fast 对 workflow 配置错误有效，后续 FireSim wrapper 需要保持
默认 `PRT_PAGE_SIZE_BYTES=1024`，不要把 `--spm-page-bytes` 覆盖成 4096。

## P0：修正页数和 allocator 模型

当前状态：`2026-05-05` 已完成第一版软件修复。之前代码通过把 `num_cores` 提升到
`num_gemmini_mgrs` 来让页池覆盖 12 个 manager，这会污染 CPU core 语义。现在页池、PTE sizing、
allocation order、默认 xlate range 和 idle check 都改为使用独立的 SPM manager domain count。

措施：

- 已引入 `prt_cfg_spm_manager_count(cfg)` 和 `prt_cfg_spm_total_pages(cfg)`。
- SPM manager count 优先使用 `num_gemmini_mgrs`，仅在其为 0 时回退到 `num_cores`。
- `page_used` 总页、idle check、SPM PTE chunk sizing、默认 xlate range 和 page allocation order 已走 helper。
- 初始化日志已增加 `spm_mgrs`。

已完成的验证：helper 小测试确认 4 core / 12 manager / `pages_per_acc=1024` 得到 `spm_mgrs=12`
和总页 `12288`。

未完成的验证：新的 `cfg32_nic` AGFI 生成后，需要在 FPGA workload 初始化日志中确认
`cores=4 gemmini=12 spm_mgrs=12`，并观察 action release 后 allocator idle check 不再漏检。

## P1：action 级资源所有权

措施：

- action 开始前一次性确定 stage -> Gemmini manager set、stage -> DMA manager set。
- 指令提交前校验 manager 属于当前 stage。
- 默认禁止同一 manager 上 DMA/Gemmini overlap。
- 如果需要 overlap，先实现 shared scope 或 action/stage 级 scope，不允许两个 helper 独立抢同一 manager。
- action release 前统一 drain/fence 所有 manager，再释放资源。

验收：日志中能看到 action manager 表；任何越权 manager 使用都 fail-fast。

## P1：SPM 稳定绑定

措施：

- action 开始前为所有 weight、pipe slot、ring slot、lazy tensor 分配 SPM pages。
- action 开始前分配 alias VA window 和 private xlate context。
- stage/subbatch 运行时只选择虚拟 slot/offset，不重新绑定同一 tensor 的物理页。
- fixed/lazy tensor 第一次加载后保持绑定，除非 action release。
- ring/double buffer 通过 slot index 轮转，不通过重新映射物理页轮转。

验收：同一 action 内同一 buffer slot 的 vpage->ppn 绑定不变；action release 后 allocator idle。

## P1：DMA completion 策略

短期：

- 保留 completion flag。
- completion flag 分配后立即 prefault/mlock/v2p，并打印 VA/PA/slot。
- wait path 同时记录 `hw_dma_fence` 返回、completion flag 和 DMA monitor。
- 若 flag 未置位但 DMA monitor/idle 显示完成，应记录为一致性疑点。

中期：

- 增加 per-manager DMA status/idle/error CSR。
- 限制同一 DMA manager outstanding token 数，或引入 token id。
- 支持 wait-by-idle 的实验 backend。

长期：

- 若 F2/baremetal/metasim 均证明 idle/status 足够精确，再考虑删除 completion flag。

## P1：no-DMA compute 二分测试

目的：把卡死从 DMA/completion/direct/bounce 与 Gemmini/SPM/manager 中拆开。

措施：

- 构造最小 stage，input/weight/output 预置在 SPM。
- 禁用 fixed-load DMA 和 export DMA。
- 只执行 Gemmini issue/fence，并读取 output。
- 分别在 baremetal/metasim/F2 上运行。

验收：

- no-DMA 通过：优先查 DMA/completion/host buffer/direct/bounce。
- no-DMA 失败：优先查 Gemmini/SPM xlate/ReRoCC manager ownership。

## P2：HybridMapper artifact 合同化

措施：

- HybridMapper 输出 target key 和硬件容量，runtime 初始化必须匹配。
- 每个 segment 输出 action-level SPM plan，而不是只输出 stage-local hint。
- 每个 buffer 输出 slot count、pages_per_slot、alias group、producer/consumer、lifetime。
- 每个 stage 输出 virtual manager ids，并明确 split 与 tile 覆盖范围。
- 输出 artifact checksum/version，runtime 记录到日志。

验收：runtime 可以在不执行任何 DMA/Gemmini 的情况下完成 artifact 静态验证，并输出 manager/SPM 冲突表。

## P2：性能优化

在正确性稳定后再做：

- DMA manager 位置纳入 HybridMapper cost model，避免不必要的 A->C->B NoC 绕路。
- 对可安全 direct 的 host/SPM copy，减少 bounce。
- 对 misaligned direct 依赖 optimized DMA 硬件能力，并用 forced direct 回归验证。
- stage overlap 只在冲突表证明安全时开启。
- 对固定 weight 支持跨 action 缓存，但必须由 artifact 显式声明 lifetime 和共享关系。

## 建议执行顺序

1. 先完成 gdbserver bitstream 和 attach 验证。
2. 修 P0 校验和页数模型。
3. 用 gdbserver 定位当前卡死点。
4. 做 no-DMA compute 二分。
5. 收敛 action 级 ownership 和 SPM 稳定绑定。
6. 再打开 DMA/Gemmini overlap 和 direct path 性能优化。
