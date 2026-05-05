# Pipeline Runtime 问题答复与优化措施草案

更新时间：`2026-05-05 17:55 UTC`

本文逐条回答 [`问题.md`](问题.md) 中的设计/风险/优化问题。结论基于当前仓库静态代码阅读、2026-05-05 已通过的 host build / artifact audit / CPU dry-run，以及仍在构建中的 `cfg32_nic` F2 bitstream。没有 F2 live GDB 现场前，本文只把“代码当前事实”和“建议改造方向”分开写。

## 1. Action 是否应长期持有 manager

结论：需求上应该长期持有；当前代码已经在 action 层分配和绑定 manager，但底层指令 helper 仍会做短 scope acquire/release。

当前事实：

- `prt_runtime_run()` 每个 segment 生成一个 action，顺序是 `prt_action_generate -> prt_action_alloc_acc -> prt_action_alloc_spm -> runtime_prepare_stage_spm_windows -> prt_action_bind_topology -> runtime_flush_spm_xlate`。
- `prt_action_alloc_acc()` 检查每个 stage 的 `acc_util`、`vAccIdxList`、显式物理 manager 列表，并拒绝同一 segment 内 manager over-subscribe。
- `prt_action_bind_topology()` 把每个 stage 的 Gemmini/DMA manager 写进 `exec->stage_mgr_ids[]` / `exec->stage_dma_ids[]`。
- DMA 路径已经有 `dma_validate_stage_manager()`，会在 `prt_dma_submit()` 和 `dma_batch_scope_acquire()` 前验证 manager 属于当前 stage/action。

设计建议：

- 上层调度语义按 action/stage 拥有 manager，不按每条指令重新决定 manager。
- ReRoCC scope acquire/release 仍可保留为底层路由协议，但它应消费 action/stage 的既定授权，而不是重新做调度。
- DMA 与 Gemmini 如果共用 pair manager，默认同一 stage 内串行；要 overlap 必须先有显式 shared scope 和冲突表。

## 2. 指令阻塞/非阻塞与 fence 关系

当前运行态在 `spm_xlate_enable=1` 时走保守同步路线：

- runtime 初始化会强制 `sync_mode = blocking_debug`、`dma_backend = blocking_fence`、`gemmini_mode = blocking_fence`。
- DMA submit 后进入 `prt_dma_wait()`，RISC-V 路径会调用 `hw_dma_fence()`；当前这个 fence 等的是 DMA manager idle，不是 per-token id。
- Gemmini blocking 路径是 `prt_gemm_conv_run()` 发任务，若 issue 路径没有自己 fence，再进入 `gemm_blocking_fence()`。
- split conv / split resadd 在 RISC-V 路径里部分 issue helper 自带 fence，因此 `task_issue_already_fenced()` 会避免重复 fence。
- ReRoCC release 已经在 `prt_rr_release_scope()` 后增加同 cfg 的 `RRCFG` readback，用于避开 release ack 窗口。

仍需注意：

- CPU `fence rw,rw` 只保证当前 hart 的内存/指令顺序，不等价于 DMA 完成、Gemmini 完成或 ReRoCC 队列排空。
- `rr_fence(cfg)` 保证该 ReRoCC cfg scope 上的路由命令完成。
- `gemmini_fence()` 保证 Gemmini 内部命令队列完成。
- `hw_dma_fence()` 当前保证 DMA manager idle；在 blocking path 下等价于这一个 outstanding token 完成，但不能支持同 manager 多 outstanding token。

## 3. 当前加速器分配和 SPM 分配是否对齐 MudnacSim

部分对齐，且 2026-05-05 已修掉一个关键域错误。

当前 manager 分配：

- 优先使用 artifact 的 `physical_acc_ids`；没有显式物理 id 时按 `rr_cursor` 顺序分配。
- `virtual_acc_ids` 必须存在且长度等于 `acc_util`，用于把 mapper 的虚拟槽映射到本 stage 内 slot。
- pair-manager 模式下 DMA local id 与 Gemmini local id 对齐，`prt_cfg_dma_mgr_count()` 返回 `num_gemmini_mgrs`。

当前 SPM 分配：

- `prt_action_alloc_spm()` 读取 segment 的 `bufferBinding*` 合同，为 WEIGHT / PIPE / RING 分配页。
- 分配页时按 all-bank interleave：`bank0:lp0, bank1:lp0, ..., bank0:lp1, ...`，这是为了匹配 MudnacSim / mapper 的 all-bank 语义。
- 旧代码曾把页池容量绑到 `num_cores`，现在已通过 `prt_cfg_spm_manager_count()` 改成优先使用 `num_gemmini_mgrs`，避免 4 core / 12 manager 目标只分到 4 个 SPM manager 的页。

当前已验证：

- `bertmini` 的 `ours2/gemini2/tangram2` artifact 在 `--page-size-bytes 1024` 下通过静态审计。
- 故意用 `4096` 会 fail-fast：`pages_per_acc * page_size != shared_spad_local_size_bytes`。
- CPU backend dry-run 能在 `spm_mgrs=12`、`pages_per_acc=1024` 配置下通过初始化和 artifact 合同检查。

## 4. Stage 并行与单 stage 多 manager 并行

当前 stage 并行粒度是 POSIX stage worker：

- `build_topology_from_pipeline()` 之后，每个 stage 有一个 worker 线程。
- stage 之间通过 pipe/ring buffer 的 ready/idle 状态同步。
- 当前顶层一次只推进一个 active action；还不是多个模型/多个 action 同时运行。

单 stage 多 manager 并行：

- `acc_util` 和 `split_kind` 决定一个 stage 使用多少 Gemmini manager。
- `prt_action_alloc_acc()` 把 stage 的多个 manager 写入 `assign->gemmini_mgr_ids[]`。
- `prt_gemmini_adapter.c` 根据 `split_kind` 走 `run_conv_oc_split`、`run_conv_spatial_split` 或 resadd split。

“issue 后立即 fence”和“issue/fence 分离”的区别：

- blocking 模式下，`gemm_blocking_conv_run()` 通常 issue 后立即 fence，便于调试和避免复杂 overlap。
- 部分 split helper 内部已经为了分片正确性做 fence，`task_issue_already_fenced()` 用来避免外层重复 fence。
- 真正的 issue/fence 分离适合 overlap，但必须有 manager/page 冲突证明；当前不应默认打开。

## 5. `gemm_issue_conv_task` 切分是否校验

当前有基础合法性校验，但还不是完整的 mapper split 覆盖证明。

已有校验：

- RISC-V 路径检查 `input/weights/output` 非空，batch/input/output/kernel/channel 维度为正。
- action 分配阶段检查 `acc_util` 是 1/2/4/8/16/32，且不超过 manager 数。
- artifact audit 检查 `pAccIdxList` 长度、重复、越界，检查 local SPM tensor address/page bounds。

缺口：

- 还需要把每个 split tile 的覆盖范围、输出区间、manager 数和 `tile_count` 做统一 fail-fast 报告。
- 对 conv/resadd 的 host/SPM 指针范围，运行时还需要覆盖所有 direct/bounce/SPM->host/host->SPM 入口。

建议：

- 在 `build_stage_conv_desc()` 后、`prt_gemm_conv_run()` 前加 `validate_gemmini_task_bounds()`。
- 报告 stage/layer/tensor/manager/split/tile 覆盖，发现 overlap 或越界直接 fail-fast。

## 6. CPU fence 的作用

CPU fence 是本 hart 的顺序屏障，用于避免软件在发 custom 指令前后把普通内存访问乱序到危险位置。

它不能替代：

- DMA completion：需要 `hw_dma_fence()` 或 completion/status。
- Gemmini completion：需要 `gemmini_fence()` 或相关 helper 的内部 fence。
- ReRoCC scope completion：需要 `rr_fence(cfg)`。
- SPM xlate 生效：需要对对应 manager 发 `CFG/RANGE/FLUSH`，并等待 scope release/fence 完成。

因此建议把文档和代码里的 “fence” 名字按层区分：`cpu_fence`、`rr_fence`、`gemmini_fence`、`dma_idle_fence`、`spm_xlate_flush`。

## 7. 单 action 的虚拟地址区间与碎片风险

当前已经是 action-private alias window：

- `prt_action_alloc_spm()` 用 `segment_spm_page_span` 计算 action 的 alias page count。
- Linux 路径通过 `mmap(PROT_NONE)` 预留一段 action-private VA window。
- action 内硬件 PTE index 按 `(vaddr - range_base)` 解释，所以现在 `alias_vpage_start = 0`。
- action release 会 `disable_action_spm_xlate()`、`prt_spm_unbind_vpages_ctx()`、释放 PTE context、释放 SPM 页和 `munmap()` alias window。

碎片风险：

- 对当前“单模型、不同 action/segment 串行执行”主线，action 结束释放所有页，`runtime_assert_page_allocator_idle()` 会在 segment 开始/结束检查页池，因此长期碎片风险较低。
- 运行中仍有短期碎片可能，因为 `prt_alloc_tensor_pages()` 会按 preferred manager 再 fallback 分配，页池是 bitmap，不做 compaction。但 action 生命周期短，释放后回到空池。

后续设计：

- 多模型并发时，不能只依赖“action 后清空”；需要全局 allocator、lifetime、共享 alias group 和冲突表。

## 8. SPM page 不应每轮 stage 重绑

需求判断：同意。长期设计应是 action prepare 阶段一次分配和绑定，运行阶段只移动指令上的虚拟地址/slot。

当前代码状态：

- `prt_action_alloc_spm()` 已经为 weight/pipe/ring slot 提前分配物理页。
- `configure_action_spm_xlate()` 已在 action bind 时对 action 用到的 Gemmini manager 安装 PTBR/range。
- 但 `stage_prepare_exec_views()` 仍会在运行期根据 stage/tensor 准备视图、fixed-load DMA，并可能触发 stage 级 xlate flush；还没达到 slot-stable 的最终需求。

建议分阶段：

- 阶段 A：只加观测，打印 action prepare 后 slot -> vpage -> ppn 表，以及运行期每次 stage 视图准备。
- 阶段 B：每个 ring/double/fixed slot 固定 VA 区间，仍保留 flush。
- 阶段 C：action prepare 一次写完 PTE，stage 期间禁止重绑，只允许选择 slot vaddr。

在新 `cfg32_nic` bitstream 还没跑通 GDB 前，不建议直接做阶段 C 大改。

## 9. `rerocc_coupleddma_set_dst` 和 completion flag

当前不能直接抛弃 completion flag。短期应保留，但把它作为交叉观测，而不是唯一完成语义。

硬件当前实现：

- `FUNCT_DEST_INFO` 记录 `dstAddrReg` 和 `completionAddrReg`。
- `FUNCT_SRC_INFO` 入队 `src/dst/len/completion`。
- copy FSM 读源、写目的；最后进入 `sIssueFlag/sWaitFlag`，向 `curCompletionAddr` 发 TileLink Put 写 1。
- `FUNCT_CHECK_COMPLETION` 在 `!dmaBusy` 且 response 可用时才 ready；软件 `hw_dma_fence()` 对应这个 funct。

软件当前实现：

- completion flag 不再是临时栈地址，而是 runtime 级 completion pool。
- pool 按 host page 对齐、prefault、`mlock()`，并逐 slot 通过 `/proc/self/pagemap` 转 PA。
- submit 前写 0，wait 前后刷新 flag。

关键判断：

- 当前 blocking path 一次只允许同 manager 一个 outstanding token，因此 `hw_dma_fence()` 可以作为主完成条件。
- completion flag 用来分流：如果 `hw_dma_fence()` 返回但 flag 为 0，优先查 flag PA/cache/TL Put；如果 fence 本身不返回，优先查 DMA FSM/TL/SPM xlate/ReRoCC scope。
- 若未来支持同 DMA manager 多 outstanding token，必须引入 token id/status，不能继续用 manager idle 当 per-token completion。

## 10. Prefault + mlock 时机

当前有三类锁页/固定 PA 处理：

- 早期 `main.c` 可执行 `mlockall()`，减少运行时 page fault，但这不是 DMA PA 合同本身。
- 模型 blob / synthetic buffer 通过 `prefault_and_lock_blob()` 逐页触碰后 `mlock()`。
- DMA completion pool 和 bounce buffer 通过 `dma_prefault_and_lock_buffer()` 逐页写触碰、`fence rw,rw`、`mlock()`，然后对 completion slot 做 VA->PA。

风险：

- `mlock()` 失败当前会报错或 warning，F2 workload 必须保留日志。
- `/proc/self/pagemap` PA 只对当前已驻留页可靠，所以 prefault 必须在 v2p 前完成。

## 11. `pages_per_acc * page_bytes` 的问题

AI 建议是有效的，并且已经变成 fail-fast。

当前 `12p4c128sbus32cfg` 的 mapper artifact 要按 `page_size_bytes=1024` 解释：

- `pages_per_acc=1024`
- `shared_spad_local_size_bytes=1048576`
- `1024 * 1024 = 1MiB`

如果误用 4096 字节页，软件 PPN 到硬件 manager local window 的映射会错位。当前 audit 和 runtime dry-run 都会提前报错。

## 12. Tensor 地址和 SPM 大小越界校验

第一版已经加上：

- 静态脚本 `audit_pipeline_runtime_artifact.py` 检查 `localSpmTensorAddr + bytes` 是否落在 `localSpmFirstVPage/localSpmPageCount` 和 stage window 内。
- 运行时 `runtime_prepare_stage_spm_windows()` 做同类检查，并检查 `execBaseVPage + localSpmPageSpan` 不越过 action alias window。

仍需补：

- DMA request src/dst range 校验要覆盖所有 copy 方向。
- Gemmini task input/weight/bias/output range 校验要覆盖 split 后的子任务。

## 13. 去掉 DMA 的 compute 二分

当前没有现成 `--no-dma-compute` 开关。建议先不要在 bitstream 构建等待期改主线语义。

低风险路径：

- 先用 host/CPU backend 做调度干跑，保留 artifact、action、SPM、manager 合同检查。
- 构造一个最小 baremetal/metasim compute-only 用例：预置 SPM，禁 fixed-load/export DMA，只做 Gemmini issue/fence。
- F2 上等新 AGFI 和 GDB attach 可用后，再跑正常路径 vs no-DMA compute 对比。

判据：

- no-DMA 通过，正常路径挂：优先查 DMA/completion/direct/bounce。
- no-DMA 也挂：优先查 Gemmini/SPM xlate/ReRoCC manager ownership。

## 14. 每次 DMA 的 manager id 如何决定

当前由 action/stage 分配决定：

- `prt_action_alloc_acc()` 给每个 stage 生成 `assign->dma_mgr_ids[]`。
- pair-manager 模式下 `dm_local = gm_local`，因此 DMA manager 与 Gemmini manager local id 对齐。
- `prt_action_bind_topology()` 把每个 stage 的主 DMA manager 写到 `exec->stage_dma_ids[stage]`；pipebuf 的 `cmd_acc[0/1]` 也设为该 stage DMA id。
- 多 tile stage 中，DMA guard 在 pair-manager 模式下允许本 stage 的 `stage_mgr_ids[]` 列表。

建议：

- 每个 DMA request 的 `src_acc/dst_acc` 应显式来自 `exec->stage_dma_ids` 或该 stage manager set。
- 日志/GDB 中第一时间看 `tok->rr_manager_id`、`req->src_acc`、`req->dst_acc` 和 stage 的 bound manager list。

## 15. SPM xlate 为什么需要单独 cfg slot

这不是需求层面的“SPM xlate 独占一个计算 manager slot”，而是当前 ReRoCC 路由实现需要一个稳定 cfg id 来把 opcode 3 临时绑定到指定 Gemmini manager。

当前事实：

- `prt_rerocc.c` 定义 `PRT_RR_SPM_XLATE_CFG_ID = RR_MAX_CFGS - 1`。
- `prt_gemmini_spm_xlate_program/flush/range/cfg()` 会通过 `prt_spm_xlate_acquire_scope()` 临时获取该 cfg，保存 opcode 3 原绑定，发 xlate 指令，然后 release 并恢复。
- `prt_action_bind_topology()` 会对 action 使用到的所有 Gemmini manager 逐个安装 PTBR/range。

长期建议：

- 需求上 xlate 配置应发生在 action prepare/bind 阶段，对所有 action manager 一次写完。
- stage 运行期不应反复占用 xlate cfg slot。
- 如果以后多个 action 并发，必须避免 xlate cfg 与计算 cfg/opcode 竞争，或为 xlate 增加更明确的管理通道。

## 16. DMA 从 Tile A 到 Tile B 是否经过 DMA 所在 Tile C

当前硬件语义是 DMA manager C 发 TileLink 读 A、写 B。

它不会把数据写进 C 的 shared scratchpad 再从 C 搬出去，但会经过 C 的 DMA 内部寄存器/数据窗口，因此 NoC/TL 事务形态是：

- C 发起对 A 的读；
- C 收到读数据；
- C 发起对 B 的写。

所以性能模型上可以近似看成 A->C 和 C->B 两段流量。HybridMapper 的 cost model 后续应把 DMA manager 位置纳入考虑，避免不必要的绕路。

## 17. DMA 和 Gemmini 并行同 manager 的风险

当前仍是未解决风险点。现有 guard 能防止“用错 stage manager”，但不能证明同一 stage 内 DMA 和 Gemmini overlap 一定安全。

建议默认策略：

- blocking debug 模式下，同一 stage 内 DMA fixed-load、Gemmini compute、export DMA 串行。
- 打开 overlap 前，必须给每个 stage 生成 manager set、SPM page set、buffer slot set 冲突表。
- 如果 DMA/Gemmini 共用 pair manager，必须明确 scope 顺序：acquire -> issue -> fence -> release，不能两个 helper 独立抢同一 manager。

## 18. Direct/bounce path 当前策略

当前 `bounce path` 是 Linux runtime 软件策略，不是硬件独立路径。

- 判定函数是 `bytes >= 64 && src_mod64 != dst_mod64`，除非设置 force-direct。
- bounce 用 stage-local host buffer 调整 64B 相对偏移，再交给硬件 DMA。
- 当前硬件 `GemminiCoupledDMA` 已支持非对齐 beat 的读/写重排和 partial put；但仍需要 baremetal/metasim/F2 回归确认所有 misaligned 组合。

建议：

- 在 gdbserver 可用前保留 bounce guardrail。
- 新 AGFI 跑通后，再做 forced-direct A/B 测试和 misaligned perf/coverage 测试。

## 19. 当前优先级

P0：

- 等 `cfg32_nic` 或 noTrace `cfg32_nic` bitstream 完成。
- 更新 HWDB 后先验证 remote gdbserver attach。
- 首轮 GDB 只判断卡点归类：DMA fence、completion flag、SPM xlate、Gemmini fence、pipe/ring wait、thread join。

P1：

- 补 Gemmini split/tile bounds 校验。
- 补 DMA request 全方向 range 校验。
- 输出 action manager/SPM/page 冲突表。

P2：

- 推进 slot-stable SPM 绑定。
- 构造 no-DMA compute 二分 profile。
- 在正确性稳定后再打开 DMA/Gemmini overlap、forced-direct 和跨 action weight cache。

## 20. Stage resource 静态微测试补充

2026-05-05 18:12 UTC 追加了一轮等待 bitstream 期间的软件/静态合同验证：

- `pipeline-runtime` clean rebuild 通过。
- `ours2/gemini2/tangram2` artifact audit 和 `--hw-validate-only` 全部通过。
- 对三组 `bertmini` mapping 逐 segment 汇总 `accUtil`，最大值均为 12，没有超过
  当前 12 Gemmini / 12 DMA pair-manager 目标。
- 没有发现 explicit `pAccIdxList` 重复；没有超过当前普通 stage RR cfg budget 15
  且 cfg 31 继续保留给 SPM xlate。
- 最大 segment SPM 页跨度：`ours2=2060`、`gemini2=1734`、`tangram2=2061`，都低于
  `12 * 1024` 页的全局目标容量。

解释：当前 artifact 层面没有明显的 segment-level manager over-subscription。若新 F2
bitstream 上 pipeline-runtime 仍卡住，优先用 gdbserver 判断具体停在 DMA fence、
completion flag、SPM xlate、Gemmini fence、pipe/ring wait 还是硬件/NIC，而不是先怀疑
mapper 给了超过 12 manager 的 segment。仍未解决的是动态 overlap 场景的同 manager/page
冲突证明；在 `spm_xlate_enable=1` 下，短期仍以 blocking debug 路线作为首个验证目标。
