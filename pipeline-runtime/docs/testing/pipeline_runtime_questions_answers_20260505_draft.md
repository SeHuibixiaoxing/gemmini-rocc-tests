# `问题.md` 解答草案

更新时间：`2026-05-05 13:20 UTC`

本文逐条回答 [`问题.md`](问题.md) 中的问题。结论分为三类：

- `当前实现`：代码现在大致如何工作。
- `需求判断`：从 pipeline-runtime/HybridMapper 协作目标看应该怎样。
- `风险/动作`：需要修或验证的点。

## 1. action 是否应长期持有 manager

当前实现：action 会在 `prt_action_alloc_acc` 中给每个 stage 分配 Gemmini/DMA manager，
再在 `prt_action_bind_topology` 中写入 `exec->stage_acc_ids`、`exec->stage_dma_ids` 和
`exec->stage_mgr_ids`。这说明 runtime 已经有 action/stage 级资源分配模型。

但指令提交层仍有大量 per-instruction scope 操作：Gemmini 路径在 conv/resadd helper 中
`prt_rr_acquire_scope -> issue -> fence -> release`；DMA 路径在 submit/wait 中 acquire/release
ReRoCC scope。也就是说，当前代码在“调度语义”上像 action 持有 manager，在“硬件 scope 使用”
上仍像每条指令临时持有。

需求判断：action 应长期持有 manager。per-instruction acquire/release 可以保留为底层硬件协议封装，
但不应改变上层所有权。stage worker 只能使用 action 已分配的 manager；不同 action 不能在同一时刻
使用同一 manager，除非 HybridMapper 显式声明共享。

风险/动作：需要梳理 DMA 与 Gemmini 同 stage 使用同一 manager 的路径。如果两者各自 acquire/release，
可能出现一个 helper release 后另一个 helper 仍以为 manager 属于自己。短期应加 runtime 校验：
每条 DMA/Gemmini 指令的 manager 必须在当前 action/stage 的分配集合内；长期应把 scope 生命周期提升到
stage/action 或显式 batch scope。

## 2. 阻塞/非阻塞指令与 fence 时序

当前实现里存在两类 Gemmini 路径：

- sync/blocking 路径：issue 后立即 `prt_rr_fence_scope` + `gemmini_fence` + drain，然后 release。
- async/overlap 路径：stage worker 先 `prt_gemm_conv_run`，在 overlap mode 下可执行
  `stage_overlap_prefetch_entries`，之后再 `prt_gemm_fence`。

DMA 也有 blocking fence 与 poll progress thread 两种 backend。blocking 路径在 wait 中调用
`hw_dma_fence`，再根据 token scope 做 shared fence/release。poll 路径通过 pending token 和 completion
flag 轮询推进。

需求判断：issue/fence 分离只有在明确 overlap 边界时才成立。它要求：

- issue 之后到 fence 之前，不得复用同一 manager 给其他互斥操作；
- prefetch/export DMA 不得覆盖 Gemmini 尚未读完/写完的 SPM 区间；
- stage worker 进入下一个 subbatch 前必须完成本 subbatch 的必要 fence；
- ReRoCC release 后的 CSR readback workaround 仍要保留，不能把 release 当作天然同步点。

风险/动作：当前代码已有不少 fence，但缺少统一时序表。建议给每个 op kind 标注：
是否会 issue 非阻塞指令、谁负责 fence、是否允许 DMA overlap、overlap 访问的 buffer 集合是什么。
没有这张表前，不应扩大 async/overlap 默认使用范围。

## 3. 加速器分配和 SPM 分配是否与 MudnacSim/HybridMapper 对齐

当前实现：HybridMapper target yaml 给出硬件容量，例如 12 Gemmini/12 DMA/每 manager 1MiB SPM。
runtime 的 `prt_action_alloc_acc` 根据 stage 的 mapping 分配 manager；
`prt_action_alloc_spm` 根据 segment buffer binding 分配页、alias window 和 xlate context。
`PostProcess.resetLayerGroupSPMTensorAddr` 会按 layer group tensor lifetime 重新计算 SPM tensor address。

需求判断：方向一致，但还没完全对齐。HybridMapper 侧已经在按 lifetime 和 SPM utilization 规划 tensor
地址；runtime 侧应把这些地址作为 action 内稳定布局，而不是每轮 stage 重新解释成新的物理页绑定。

风险/动作：

- `pages_per_acc` 必须来自硬件 target，而不是默认值。
- SPM 页数应按 `num_gemmini_mgrs`/pair manager domain 建模，而不是只按 CPU core 数。
- 需要加 tensor address+size 不越界校验。
- 需要把 HybridMapper 的 `spmTensorAddr/spmTensorUtil/lgSpmUtil` 与 runtime 的 buffer binding
  建立一一对应校验。

## 4. stage 并行与单 stage 多 Gemmini 并行

当前实现：runtime 对每个 segment 创建多个 stage worker thread。每个 worker 根据自己的 entry/export
pipe/ring buffer 状态推进。单 stage 多 Gemmini 通过 `task.tile_count`、`task.manager_ids[]` 和 split
类型传给 Gemmini adapter；fence 时 `fence_task_managers` 会遍历 unique manager。

需求判断：

- stage 并行依赖 pipe/ring buffer 的 full/ready 语义；
- 单 stage 多 Gemmini 依赖 split 结果和 manager 列表；
- 两者都必须以 action 的 manager/SPM 绑定为静态约束。

风险/动作：当前重点风险不是“没有线程并行”，而是并行线程是否会通过 DMA/Gemmini 访问同一 manager
或同一 SPM 页。需要在 action bind 后生成一张冲突表：stage -> manager set、stage -> SPM page set、
buffer -> producer/consumer。运行前若存在未声明共享的交集，应 fail-fast。

## 5. “issue 后立即 fence”和“issue/fence 分离”的区别

“issue 后立即 fence”是保守模式。它牺牲 overlap，但容易证明正确：提交 Gemmini/DMA 指令后马上等待
该 manager 完成，再释放 scope。

“issue/fence 分离”是 overlap 模式。它允许在 Gemmini 计算期间做下一步 DMA 或其他准备，但需要额外
证明：

- manager 未被其他互斥操作抢占；
- 输入 SPM 页在 fence 前不会被覆盖；
- 输出 SPM 页在 fence 前不会被消费者读取；
- DMA completion/fence 与 Gemmini fence 的顺序满足数据依赖。

当前需求下，默认应以保守模式作为正确性基线；overlap 模式只在通过冲突校验和 gdbserver/trace 验证后启用。

## 6. `gemm_issue_conv_task` 切分是否需要校验

当前实现有 task split、tile count 和 manager list，并有一些 stride/pointwise fallback 校验。但从现有代码形态看，
runtime 仍较大程度信任 mapping/artifact 给出的 split 和 tensor 地址。`split_rects_even` 等 helper 能生成
矩形切分，但缺少完整的 artifact-vs-runtime 校验闭环。

需求判断：不能只信 mapping 数值。runtime 至少要校验：

- split 数量等于 stage 使用的 manager 数；
- 每个 split 的 output tile 范围不重叠、不越界；
- input/weight/output/bias 地址加 size 不越过对应 tensor SPM/host buffer；
- groups/stride/padding/channel 等参数与 Gemmini helper 支持范围一致；
- 多 manager 输出写入不同地址或显式 reduce，不允许隐式重叠。

## 7. CPU fence 的作用

CPU fence 只保证 CPU 视角的内存访问和指令顺序。例如 `asm volatile("fence rw, rw")` 可用于确保 CPU
对 page table、completion flag、host buffer 的写入在后续设备操作前可见。

它不等价于：

- DMA 完成；
- Gemmini 完成；
- ReRoCC manager queue 排空；
- SPM xlate 硬件已经加载/使用新页表；
- Linux 虚拟地址已经变成硬件可用物理地址。

因此 CPU fence 只能作为“软件发布状态”的一部分。DMA/Gemmini/SPM xlate 仍需要各自的硬件 fence、
CSR readback、completion 或 idle 状态确认。

## 8. 单 action 虚拟地址区间与碎片风险

当前实现会为 action 分配 alias window，并在 action release 时 `munmap`/unbind/release。页分配通过
`alloc_key_cursor` 跟踪多个 buffer slot，action release 时释放所有 alloc key。

这对“单个 segment/action 串行执行”有释放机制，但仍有两类风险：

- 若 action 内动态为 stage 分配/释放 vpage，可能出现 action 内碎片或重绑定错误；
- `runtime_assert_page_allocator_idle` 当前按 `num_cores * pages_per_acc` 统计总页，若 `num_cores`
  小于实际 manager 数，会漏检或错检。

需求判断：单 action 的虚拟地址区间应在 action 开始前一次性规划；运行中只分配 tensor 到已规划 slot；
action 结束后整体释放。不同 action 串行时应回到空 allocator 状态，不应累积碎片。

## 9. SPM page 绑定是否应每轮 stage 改变

需求判断：不应每轮 stage 改变物理页绑定。double buffer、ring buffer、lazy fixed tensor 都应在 action
开始前预留对应 slot。后续改变的是 DMA/Gemmini 指令上的虚拟地址或 slot index，而不是重新绑定物理页。

当前实现已经有 action-level `alias_base_va`、`alias_vpage_start`、`alias_page_count` 和 private xlate
context，这是正确方向；但需要继续检查 `runtime_prepare_stage_spm_windows`、pipe/ring/fixed lazy 路径是否仍有
按 stage 反复重绑定的行为。

## 10. `rerocc_coupleddma_set_dst` completion flag 能否去掉

当前软件会给 DMA token 分配 completion flag，转换成物理地址后传给硬件 `set_dst`，等待路径会刷新该 flag，
同时也会调用 `hw_dma_fence`。

completion flag 的问题是：Linux 下必须确保 flag 所在虚拟页已经 prefault/locked，并且传给硬件的是物理地址。
如果 v2p、cache 一致性或页迁移出错，就会出现“硬件完成了但软件看不到”或“软件等错地址”的风险。

需求判断：理想上可以用 DMA idle/fence 状态替代 memory flag 作为主完成条件，但前提是硬件能提供
per-token 或至少 per-manager 的明确完成/错误状态。只等“模块 idle”在并发 DMA 时不够精确，除非 runtime
保证同一 DMA manager 同时只有一个 outstanding token。

短期建议：保留 completion flag，同时把 `hw_dma_fence`/monitor 状态作为交叉验证。若要去掉 flag，先实现：

- 单 manager outstanding 数量限制或 token id；
- DMA idle/error/status CSR；
- wait path 只依赖硬件状态的 baremetal/metasim/F2 回归；
- Linux v2p 和 mlock 风险消除后再删除旧 flag path。

## 11. prefault + mlock 时机

当前 `load_model_blob_file` 读入 model/input/golden blob 后调用 `prefault_and_lock_blob`：
按 host page 写保留触碰每页，然后 `fence rw,rw`，再 `mlock`。SPM page table pool 也有 hugetlb/mmap
相关逻辑。

需求判断：所有会传给硬件的 host buffer、completion flag、SPM page table，都应在获取物理地址前完成：

- 分配；
- prefault；
- mlock 或 hugetlb pinning；
- v2p；
- CPU fence；
- 记录 VA/PA/size。

如果 buffer 在 DMA submit 后才 prefault/lock，就已经太晚。

## 12. `pages_per_acc * page_bytes != 1MiB` 风险

该风险成立。当前 P12 Sbus128 目标每 manager SPM 是 1MiB；如果 `page_bytes=1024`，则
`pages_per_acc` 应为 1024。若错误使用 256，manager 间 PPN window 会错位。

风险/动作：初始化时强制校验：

```text
pages_per_acc * page_size_bytes == shared_spad_local_size_bytes
```

并且 page allocator、idle check、SPM paddr 计算都要使用 manager 数，而不是 CPU core 数。

## 13. tensor 地址 + size 越界校验

需要加。建议位置：

- artifact load 后：校验每个 tensor 的 declared byte size；
- action alloc/bind 后：校验 buffer binding 的 `pages_per_slot * page_bytes` 覆盖 tensor 最大访问；
- stage task build 后：校验 conv/resadd 的 input/weight/bias/output 地址和 shape 推导 size；
- DMA submit 前：校验 `src/dst/bytes` 落在已注册 host buffer 或 SPM page range 内。

越界应 fail-fast，不要等 Gemmini/DMA hang。

## 14. 去掉 DMA 的计算试验

有价值。目的不是替代最终路径，而是二分问题：

- no-DMA compute 能跑，说明 Gemmini/SPM xlate/manager 基本健康，问题偏 DMA/completion/bounce/direct；
- no-DMA compute 也卡，说明问题可能在 Gemmini、SPM xlate、manager ownership 或 stage 调度。

建议先做最小 segment/stage，固定 input/weight 已在 SPM，禁用 export DMA，仅验证 Gemmini fence 和结果可读。

## 15. DMA manager id 如何决定

当前 pair manager 模式下 `prt_cfg_dma_manager_id` 返回 `gemmini_mgr_base_id + local_idx`，
即 DMA manager 与 Gemmini manager 使用同一 local index/pair id。`assign_stage_manager_slot`
中 `dm_local = gm_local`，所以 stage 的 DMA manager 与对应 Gemmini manager 成对绑定。

这符合 pair manager 设计，但也意味着同一 stage 内 DMA 与 Gemmini 更容易争同一个 manager scope。
因此并发时必须有显式互斥或共享 scope 策略。

## 16. SPM xlate 指令为什么需要单独 slot

SPM xlate 配置从语义上是 manager 地址翻译状态，不是普通 Gemmini compute。它需要明确作用域，避免和
Gemmini/DMA 指令在同一 manager 上乱序。当前如果 xlate slot 与 Gemmini slot 重叠，必须保证 xlate
发生在 action 执行前，并且后续 action 内不再变动。

需求判断：长期设计应把 xlate 配置提升到 action prepare 阶段，对 action 使用的所有 manager 安装同一
稳定 page table/context。stage 执行期间不再发改变绑定的 xlate 指令。

## 17. DMA 从 Tile A 到 Tile B 是否经过 Tile C

按当前理解，若 DMA manager 在 Tile C，搬运 Tile A 到 Tile B 会形成 A->C 和 C->B 两段 NoC 事务，
数据经过 DMA 内部缓冲，不进入 shared scratchpad。它不是真正的 A->B 直接 NoC copy。

性能含义：DMA manager 放置会影响 NoC 流量和延迟。HybridMapper 如果要优化性能，应把 DMA manager
位置纳入 cost model，而不是只看逻辑带宽。

## 18. DMA 和 Gemmini 并行需要同一 manager 的风险

这是当前最重要的未解决风险之一。pair manager 模式下 DMA/Gemmini local id 对齐，同一 stage 内如果
DMA prefetch/export 和 Gemmini compute overlap，可能需要同一 manager。若两个路径各自 acquire/release/fence，
就可能破坏彼此的顺序。

建议短期默认禁止同一 manager 上 DMA/Gemmini overlap；只有在实现 shared scope 或明确串行化后再打开。

## 19. 页数量应始终用 `num_gemmini_mgrs`

同意。尤其在 4 CPU core、12 Gemmini manager 的目标上，按 `num_cores` 计算总页是错误模型。
应使用 manager/SPM domain 数，pair manager 下通常是 `num_gemmini_mgrs`。

当前代码状态：已在 `2026-05-05` 的 checkpoint 中修正为 `prt_cfg_spm_manager_count()` /
`prt_cfg_spm_total_pages()`，并把 SPM page table sizing、`page_used` 分配、preferred/fallback page
allocation order、默认 xlate range size 和 `runtime_assert_page_allocator_idle` 都切到 SPM manager
domain。`num_cores` 不再为了覆盖 12 个 SPM manager 被强行提升到 12。

限制：这还只是软件侧静态/编译语义修复，尚未在新的 `cfg32_nic` AGFI 上跑过 pipeline-runtime workload。
后续 gdbserver run 需要确认初始化日志里 `cores=4`、`gemmini=12`、`spm_mgrs=12`、总页为 `12 * 1024`。

## 20. bounce/direct path 与 optimized DMA

现有 bounce 是软件 path，不是硬件里一条叫 bounce 的通道。它通过 host bounce buffer 修正 mod64
相位风险。optimized DMA 硬件的目标是让 misaligned direct path 能在不额外 copy 的前提下正确处理
对齐读、内部重组、掩码写。

动作顺序应是：

1. baremetal/metasim 单元测试 misaligned direct；
2. 构建含 optimized DMA 的 F2 bitstream；
3. 用 forced direct path 验证；
4. 再决定是否减少或删除软件 bounce。

在 F2 结果出来前，不应把 bounce 删除。
