# Pipeline Runtime 与 HybridMapper 协作需求草案

更新时间：`2026-05-05 19:20 UTC`

本文从需求角度对齐 `pipeline-runtime` 与 `HybridMapper` 的协作关系。它不是最终设计定稿；
当前目标是把多模型协同计算、SPM/DMA/Gemmini 资源使用和 runtime 行为边界说清楚，便于后续
审阅后收敛成最终改进方案。

## 1. 总目标

`HybridMapper` 负责离线决定“怎么把一个或多个模型映射到目标硬件上”；`pipeline-runtime`
负责在线把这些决定安全、可调试、可复现地落到 `Gemmini + ReRoCC + CoupledDMA + shared SPM`
执行路径上。

这两部分共同解决的问题是：

- 多模型、多 stage、多 batch/subbatch 的并行执行编排；
- 每个 stage 使用多少 Gemmini/DMA manager，以及这些 manager 的逻辑身份和硬件身份如何对应；
- 中间 tensor、固定 weight、ring/double buffer、lazy fetch tensor 在 SPM 中如何布局；
- SPM 虚拟地址、物理页、manager local SPM window 之间如何稳定绑定；
- host DRAM、SPM、Gemmini 计算、CoupledDMA 搬运之间的依赖、同步和冲突避免；
- 在 Linux/F2 上出现卡死时，如何通过 gdbserver、breadcrumb、trigger log 和少量硬件观测定位。

当前 runtime 已经能读取 model/pipeline/mapping artifact，并按 segment/action 运行；但从需求看，
它还没有完全达到“多模型长期稳定协同”的边界，尤其是 action 生命周期、SPM 预留/绑定、DMA/Gemmini
并发冲突和 completion 语义仍需要收敛。

## 2. 职责边界

### HybridMapper 应输出什么

HybridMapper 应输出一个稳定的执行合同，而不只是单层 mapping 数值。合同至少应包含：

- target 描述：核心数、Gemmini manager 数、DMA manager 数、每 manager SPM 容量、SPM page size、
  shared SPM global base、sbus 宽度、默认 batch/subbatch。
- model 图：layer、tensor、输入输出、tensor shape/byte size、weight/input/output/bias 地址语义。
- pipeline 结构：segment、stage、stage 依赖、ring/double buffer、subbatch 粒度、跨 stage tensor 生命周期。
- stage 资源：每个 stage 的 `acc_util`、`virtual_acc_ids`、split 类型、期望绑定的 manager 集合。
- SPM 布局：每个 tensor 在 action 作用域中的虚拟地址区间、页数、buffer slot、alias group、
  是否固定 weight、是否 lazy fetch、是否 ring/double buffer。
- 校验信息：每个 tensor 的最大访问范围、每个 stage 的输入/输出范围、每个 action 的总 SPM 需求、
  每个 manager 的页数是否不超过硬件 local SPM 容量。

其中 `conference/HybridMapper/config/pipeline_runtime_hardware_targets.yaml` 已经给出了当前目标硬件
的核心参数。例如 `rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128`
描述的是 4 core、12 Gemmini、12 DMA、每 manager 1MiB SPM、`pages_per_acc=1024`、`sbus_width_bits=128`。

### Pipeline Runtime 应保证什么

pipeline-runtime 应把 HybridMapper 的合同落实为运行时不变量：

- action 开始前完成资源分配：manager、SPM 页、alias VA window、SPM xlate context。
- action 生命周期内资源稳定：同一个 action 内 manager 归属稳定，SPM 虚拟页到物理页的绑定稳定。
- stage 运行时只移动“指令上的虚拟地址/slot 指针”，不反复改变同一 tensor 的物理页绑定。
- 每条 DMA/Gemmini 指令提交前，能证明源/目的地址和长度不越过已分配 SPM 或 host buffer。
- 每个 stage 的 DMA 和 Gemmini 使用同一 manager 时，不允许形成 release/fence/acquire 次序错乱。
- action 结束后释放所有临时资源，并能用 allocator idle 检查证明无页泄漏。
- 发生 timeout 或 hang 时，能定位到 stage、subbatch、manager、tensor、DMA token 或 Gemmini fence。

当前代码已经有 action 级分配框架：`prt_action_generate`、`prt_action_alloc_acc`、
`prt_action_alloc_spm`、`prt_action_bind_topology`、`runtime_prepare_stage_spm_windows`、
`runtime_flush_spm_xlate`。但从需求看，仍需把这些机制从“能跑当前 artifact”强化成
“严格执行 HybridMapper 合同”。

## 3. 多模型协同计算模型

多模型协同不应理解为简单地把多个模型串行跑完，而应支持以下几类并行：

- 模型间 pipeline：模型 A 的某些 stage 和模型 B 的某些 stage 可以同时存在于系统中，只要依赖和资源不冲突。
- 模型内 pipeline：同一模型的不同 stage 可通过 pipe/ring buffer 在不同 subbatch 上并行推进。
- stage 内多 Gemmini：单个 stage 可以按 split 结果使用多个 manager 并行计算同一层的不同 tile/区域。
- DMA/compute overlap：某个 stage 在计算当前 subbatch 时，可以预取下一 subbatch 或导出上一 subbatch，
  但 overlap 必须以 manager 和 SPM 页不冲突为前提。

建议把运行时调度单位分三层：

- model instance：一次模型推理请求或一个模型流，持有模型级参数和输入输出。
- action：一个或多个 segment 的资源作用域，持有 manager、SPM 页、alias VA 和 xlate context。
- stage worker：action 内执行具体 stage/subbatch 的线程，按 buffer 状态推进 DMA 和 Gemmini。

当前代码中 action 每个 segment 创建一次，stage worker 按 `pthread_create` 并行运行。后续多模型协同时，
应避免每个模型各自盲目抢 manager，而应有全局 action scheduler。scheduler 需要保证：

- 同一 manager 在同一时刻只属于一个 action 或一个明确的共享域；
- action 之间可以时间复用 manager，但必须在 release/fence 完成后才能交给下一个 action；
- SPM 虚拟地址空间可以 action-private，但硬件物理页和 manager window 必须全局唯一；
- 不同模型共享固定 weight 或中间 tensor 时，需要明确的 alias group 和 lifetime，而不是隐式复用。

## 4. SPM 与地址需求

需求侧建议把 SPM 分成“预留布局”和“运行访问”两层：

- 预留布局在 action 开始前完成：根据 HybridMapper 的 buffer/tensor lifetime，为 weight、pipe、ring、
  double buffer 和 lazy tensor 预留页。
- 运行访问只改变虚拟地址：stage/subbatch 的 DMA 和 Gemmini 指令使用不同虚拟地址或 slot offset，
  但不重新给同一 tensor 换物理页。

这和当前每轮 stage 都可能重新准备/绑定 SPM page 的实现存在偏差。后续改进方向应是：

- action 生成时计算完整 alias VA window；
- `pages_per_acc * page_bytes` 必须等于硬件每 manager local SPM 容量；
- tensor 的 `vaddr + size` 必须落在已分配 slot/window 内；
- ring/double buffer 只轮转 slot 的虚拟地址或 slot index；
- fixed/lazy tensor 在 action 生命周期内保持稳定绑定；
- action release 时统一 unbind xlate、释放 SPM 页和 alias VA window。

当前 `PostProcess.resetLayerGroupSPMTensorAddr` 体现了 HybridMapper 已有按 lifetime 做 SPM
地址规划的思路：长生命周期 tensor 优先放置，并把 layer group 内 tensor 地址写回 mapping。
runtime 侧应承接这个语义，而不是把 mapping 当成可随 stage 任意重绑定的提示。

## 5. Manager 与同步需求

从需求看，一个 action 一旦确定了需要哪些 Gemmini/DMA manager，就应长期持有这些 manager。
每条指令临时 acquire/release 的实现可以作为底层 ReRoCC 协议封装，但对上层调度语义来说，
manager 的所有权应属于 action 或 stage，而不是属于单条指令。

建议的约束：

- `prt_action_alloc_acc` 决定 action 内 stage 到 manager 的映射。
- `prt_action_bind_topology` 将 stage 的 Gemmini/DMA manager 写入执行态。
- stage worker 发指令时只能使用已分配给本 stage 的 manager。
- DMA 和 Gemmini 如果使用同一个 pair manager，必须明确串行段或共享 scope，不允许两个线程独立 acquire/release。
- fence 的语义必须分层：CPU fence 只保证 CPU 内存/指令可见性；ReRoCC fence 保证 manager 指令队列排空；
  Gemmini fence 保证 Gemmini 内部命令完成；DMA fence 或 completion 只证明 DMA 模块完成指定搬运。

当前代码中 Gemmini 和 DMA 都会在提交/等待路径上 acquire ReRoCC scope，并在 fence 后 release。
这能工作，但容易把“硬件 scope 使用”与“调度所有权”混在一起。长期设计应让 action/stage 拥有资源，
指令级 helper 只消费已授权 scope。

## 6. gdbserver 对需求的作用

gdbserver 在本项目里的价值不是替代日志，而是定位“卡死时 CPU 线程停在哪里”：

- stage worker 是否卡在 pipe/ring wait；
- 是否卡在 `prt_dma_wait` / `hw_dma_fence`；
- 是否卡在 `prt_rr_acquire_scope` / `prt_rr_fence_scope` / release 后 CSR readback；
- 是否卡在 Gemmini tiled conv/pointwise fallback；
- 是否发生线程竞争、死锁、某个 worker 不退出；
- user-space 的 tensor 地址、DMA token、manager id 是否符合预期。

因此，新 `cfg32_nic` bitstream 的首要用途是让 remote GDB 能 attach 到 pipeline-runtime。
一旦 attach 稳定，后续卡死应优先用 GDB 的线程/栈/变量现场判断软件语义，再决定是否需要新的硬件观测点。

## 7. 需要用户审阅的需求选择

以下是后续定稿前需要确认的设计取向：

- action 是否等同于一个 segment，还是允许一个 action 覆盖多个 segment/多个模型片段；
- 多模型协同时，manager 是否只能 action 独占，还是允许显式 shared manager；
- fixed weight 是否允许跨 action 常驻 SPM，还是 action 结束必须释放；
- ring/double buffer 的地址模型采用固定虚拟 slot，还是每轮动态 vaddr；
- DMA completion 是否保留 memory flag，还是转向硬件 idle/fence 状态作为主完成条件；
- 多模型共享 tensor 是否由 HybridMapper 显式导出 alias group，还是 runtime 自动识别。

建议默认选择是：action 内资源独占且稳定；跨 action 共享必须由 HybridMapper 显式声明；runtime 不做隐式共享和隐式重绑定。

## 8. 当前建议冻结的需求基线

在用户进一步反馈前，建议先按以下基线推进实现和调试：

- action 是资源所有权边界。当前可以先让一个 action 覆盖一个 segment；后续若要跨 segment/action
  合并，必须由 HybridMapper 明确输出生命周期和共享规则。
- manager 默认 action 独占。不同 action 之间不隐式共享同一 Gemmini/DMA manager；若要共享，artifact
  必须声明 shared scope、允许的操作类型和 fence 顺序。
- SPM 物理页默认 action 内稳定。weight、pipe slot、ring slot、double buffer slot、lazy tensor 的
  物理页在 action prepare 阶段确定，action release 时统一释放。
- 虚拟地址模型向 slot-stable 收敛。短期可以保留当前运行期 PTE 重绑以便先完成 F2/gdbserver bring-up；
  中期应改成每个 slot 有稳定虚拟区间，stage/subbatch 只在指令上选择不同虚拟地址。
- DMA/Gemmini overlap 默认关闭。只有在冲突表证明 manager set 与 SPM page set 不冲突，或者实现了
  显式 shared scope 后，才允许同一 action 内开启 overlap。
- DMA completion flag 短期保留。当前 blocking path 以 `hw_dma_fence()` 为主完成条件，completion flag
  用作交叉观测；若以后恢复 poll-progress backend，再重新审查 Linux PA/lock/cache 一致性。
- artifact 必须成为可验证合同。runtime 应能在不执行硬件指令时先完成 target、manager、SPM、tensor
  bounds 和 stage 冲突校验。

该基线的工程目标是先把“能调试”和“不会 silent wrong/hang”做好，再谈高性能 overlap 和跨 action
缓存。它也方便 gdbserver 首轮结果分流：如果保守同步路径仍挂，就优先查固定 DMA、SPM xlate、Gemmini
fence 或硬件；如果保守路径通过，再逐项打开 overlap、direct/bounce 优化和跨 action 复用。

## 9. 与当前代码的差距清单

当前代码已经具备的部分：

- action 分配 stage 到 Gemmini/DMA manager 的映射；
- action 分配 SPM page list、alias window 和 private xlate context；
- `configure_action_spm_xlate()` 在 action bind 阶段对 action 使用的 Gemmini managers 安装 PTBR/range；
- `spm_xlate_enable=1` 时强制 blocking debug，避免 page-granular DMA 与 async token retire 混用；
- 2026-05-05 已补 artifact/SPM bounds、manager 数量域和 DMA manager ownership 的第一版校验。

仍需改造的部分：

- `stage_prepare_exec_views()` 仍可能在运行期反复 `bind_vpages + flush`，还没达到 slot-stable vaddr 设计；
- Gemmini task manager list 还需要与 `exec->stage_mgr_ids[]` 做完整 fail-fast 校验；
- DMA request 的 host/SPM range 校验需要覆盖 direct/bounce、SPM->host、host->SPM 和 SPM->SPM 所有入口；
- 并行 stage 的 manager/page 冲突表还没有形成统一的可打印报告；
- 多模型全局 scheduler 尚未实现，当前更像单模型/单 action 顺序执行主线；
- no-DMA compute 二分测试还需要构造，用来把 DMA/completion/direct 与 Gemmini/SPM xlate 分开。

## 10. 本轮建议审阅结论

为了让后续实现可以收敛，建议先把以下结论作为用户审阅入口：

- `HybridMapper` 输出的是执行合同，不只是 mapping hint。runtime 不应在执行期重新推导资源布局。
- action 是资源所有权边界。短期一个 action 可以对应一个 segment；跨 segment/action 合并必须由
  artifact 明确声明 lifetime 与共享规则。
- manager 默认 action/stage 独占。ReRoCC acquire/release 是底层路由协议，不应被上层理解成
  “每条指令重新调度 manager”。
- SPM 页和 alias window 应向 action-prepare 一次绑定收敛。当前运行期重绑只作为 bring-up 过渡。
- DMA/Gemmini overlap 暂时不是默认目标。没有 manager/page 冲突证明前，保守 blocking path 是首个验证目标。
- gdbserver 的首要任务是把卡死现场归类到 DMA fence、SPM xlate、Gemmini fence、pipe/ring wait 或软件调度，
  再决定是否需要下一轮硬件观测。

如果这些结论被确认，后续改进方案可以按三步走：

1. 先补 fail-fast 校验和 GDB 可观察性，确保错误不会 silent hang。
2. 再把 SPM 绑定从 stage 运行期迁移到 action prepare。
3. 最后才逐项打开 overlap、forced-direct、跨 action weight cache 和多模型全局 scheduler。
