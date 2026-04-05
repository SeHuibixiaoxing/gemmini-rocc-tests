# Pair-Wrapper Manager Plan (2026-04-05)

## 1. 范围

本文档只记录下一阶段 `pair-wrapper manager` 方案的实施计划、测试计划和交接 prompt。

它的目标是：

- 明确当前已经**放弃**的旧路线是什么
- 给出接下来要做的真正 `pair-wrapper manager` 路线
- 指定下一位 AI 应该先读哪些文档、先改哪些代码、先跑哪些测试

本文档**不是** `pipeline-runtime` Linux/F2 `bertmini` 主线状态文档。那条主线仍以：

- `README.md`
- `docs/CURRENT_STATUS.md`
- `NEXT_SESSION_PROMPT.md`

为准。

## 2. 当前结论

截至 `2026-04-05`，当前结论是：

- 之前的“pre-NoC grouped attachment / aggregatePairedManagers”路线已经被放弃。
- 该路线对应的活动代码已经从当前工作树中回退，不应继续基于它叠加实现。
- 接下来的正确方向是：实现真正的 `pair-wrapper manager`，让每个 `Gemmini i + CoupledDMA i` 作为一个物理 ReRoCC manager / 一个 NoC attachment 单元出现。

同时保留了两类**不属于废弃路线本身**的改动：

- `GemminiLearningReRoCCCoupledDMAConfigHelpers.scala`
  - 继续作为一般性的 NoC 布局辅助工具使用
  - 保留 `useCompactPairedManagerLayout`
- `GemminiLearningReRoCCCoupledDMAConfigs.scala` 中的大 dummy-Gemmini / sbus 变体配置
  - 这些配置主要服务 buildbitstream 资源实验
  - 不等价于旧聚合路线本身

另外，`sims/firesim` 子树里仍保留与 build host / swap / local-only build 相关的改动；这些改动属于 FireSim buildbitstream 工作，不属于本次回退目标。

## 3. 已放弃路线的定义

已放弃路线指的是：

- `aggregatePairedManagers`
- `ReRoCCAttachmentGrouping`
- `NamedTLSourceShrinker`
- 在 `Integration.scala` 里把两个 manager 先聚成一个 attachment/group 再接入 NoC 的做法
- 以及围绕该路线追加的 grouped attachment / grouped manager edge 代码

放弃原因：

- 它减少的是 attachment 数量，不是真正的“一个 pair 就是一个 manager”
- 软件可见上仍然是两个 manager id
- ReRoCC / TL / DCache / source-id 边界被复杂化
- 即使继续修，也不是最终想要的架构终点

因此，下一个 AI 不应再尝试恢复：

- `aggregatePairedManagers = true`
- `AttachmentGrouping.scala`
- `NamedTLSourceShrinker.scala`
- grouped attachment 相关 `Integration.scala` 逻辑

如果需要参考这条路线的历史分析，只把它当作**归档材料**，不要把它当成当前实现基线。

## 4. 现有文档如何组合使用

建议阅读顺序如下。

### 4.1 先读这些

1. `/home/ubuntu/chipyard/AGENTS.md`
2. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/README.md`
3. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/PAPER_HARDWARE_ARCHITECTURE.md`
4. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pair_wrapper_manager_plan_20260405.md`
5. `/home/ubuntu/chipyard/tmp/firesim-aws-f2/HANDOFF_dummy_gemmini_buildbitstream_20260403.md`

### 4.2 各文档职责

- `README.md`
  - 文档入口和总索引
- `PAPER_HARDWARE_ARCHITECTURE.md`
  - 当前仓库硬件事实
  - 以及 2026-04-04/05 那轮旧路线实验的归档记录
- `pair_wrapper_manager_plan_20260405.md`
  - 当前 authoritative 的 pair-wrapper 计划与交接材料
- `HANDOFF_dummy_gemmini_buildbitstream_20260403.md`
  - 大 dummy-Gemmini bitstream 资源/内存背景
  - 与 pair-wrapper 的最终 FPGA 目标直接相关

## 5. 目标架构定义

目标不是“两个 manager 共用一个 attachment”。

目标是：

- 一个 `(Gemmini i, CoupledDMA i)` pair 对外表现为**一个物理 ReRoCC manager**
- 这个 manager 内部同时承接：
  - `custom3` Gemmini 指令
  - `custom2` CoupledDMA 指令
- 同一个 acquired cfg / manager 作用域下，可以向同一对硬件发两类 opcode
- shared-spad 本地直连仍沿用 `gemmini_id` 绑定语义

保留的语义约束：

- `custom3` 仍然表示 Gemmini 指令
- `custom2` 仍然表示 DMA 指令
- 不要把两类 opcode 硬改成一个 opcode

理想效果是：

- manager 粒度按 pair 原子化
- NoC attachment 按 pair 缩减
- 不是“两个 manager 绑在一起”，而是“一个 manager 内含两个子单元”

## 6. 硬件实施计划

建议分 5 个阶段推进。

### 6.1 阶段 A：只做硬件 wrapper，不碰大软件栈

新增一个 `LazyRoCC` wrapper，建议放在：

- `generators/gemmini/src/main/scala/gemmini/`

该 wrapper 负责：

- 实例化一个 Gemmini child
  - `OpcodeSet.custom3`
- 实例化一个 CoupledDMA child
  - `OpcodeSet.custom2`
- 二者共享同一个 `gemmini_id`

wrapper 对外要完成：

- 汇总 child `tlNode/atlNode/stlNode`
- 对 `io.cmd` 按 incoming opcode 分发
- 聚合 `io.resp`
- `io.busy := gemminiBusy || dmaBusy`
- 聚合 `io.ptw`
- `io.mem` 通过 `HellaCacheArbiter` 统一仲裁

### 6.2 阶段 B：扩展 ReRoCC manager 语义

当前最大结构性阻塞在：

- `generators/rerocc/src/main/scala/manager/Manager.scala`

现在的 ReRoCC manager 仍假设：

- 一个 manager 只有一个 native opcode
- 会把远端收到的 opcode 重写成固定 `roccOpcode`

pair-wrapper 需要改成：

- 允许一个 manager 对应多个合法 opcode
- 支持“保留 incoming opcode”
- 只做合法性检查，不强制全部改写成同一个 opcode

建议增加类似参数：

- `preserveIncomingOpcode: Boolean`

或者等价机制，但目标必须明确：

- pair-wrapper manager 下，`custom2/custom3` 都能保留下去

### 6.3 阶段 C：新增独立 config/fragment

不要复用被废弃的 grouped-attachment 路径。

应该新增独立 fragment，例如：

- `WithReRoCCGemminiCoupledDMAPairManagers`

原则：

- 一个 pair 生成一个 `BuildRoCC` 项
- 不再是“Gemmini managers + DMA managers”分开 append

先做小配置：

- `2 CPU`
- `2 pair`

先把 elaboration / compile 跑通，再考虑大配置。

### 6.4 阶段 D：小配置 baremetal 快速验证

先验证硬件，不先动 `pipeline runtime` 软件栈。

测试目标：

- pair-wrapper manager 的 acquire / dispatch / release 基本通路
- Gemmini 和 DMA 两类 opcode 在同一 pair manager 下都能正确工作
- shared-spad 本地路径没有被破坏

### 6.5 阶段 E：只在必要时再评估软件改动

如果已有 baremetal 难以表达“同一 manager、双 opcode”的控制模型，再评估软件。

优先顺序：

1. 先确认硬件 wrapper 正常
2. 再判断 baremetal 是否只需极小适配
3. `pipeline runtime` 只在硬件已稳定后再评估

## 7. 测试计划

### 7.1 原则

- 只选代表性、快速跑完的 baremetal
- 尽量减少 DMA 传输字节数
- 长循环迭代数尽量压到 `1`
- 不要先跑 Linux / FireSim / 完整 `pipeline runtime`

### 7.2 第一阶段测试

1. elaboration / compile smoke
   - 目标：wrapper + ReRoCC manager 语义先通过编译

2. `mt_hello-baremetal`
   - 仅在需要确认小配置 boot/load 路径时使用

3. `rerocc_lc_matrix_baremetal_coupleddma`
   - 目标：最基本的 Gemmini + DMA 成对控制路径
   - 建议参数：
     - 最小 manager 数
     - `--matrix single`
     - `--bytes 512` 或更小

4. explicit interleaved / shared-spad focused baremetal
   - 目标：shared scratchpad 显式交错访问路径

5. nonblocking overlap baremetal
   - 目标：Gemmini / DMA overlap 基本功能
   - 建议参数：
     - `--bytes 512`
     - 各类迭代数 `1`

### 7.3 暂不做的事

- 暂不先改 `pipeline-runtime/src/prt_rerocc.c`
- 暂不先改 Linux/F2 workload
- 暂不先把大 `12 pair` dummy bitstream 当作第一验证点

## 8. 代码阅读清单

下一位 AI 在真正实现前，建议优先阅读：

- `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/manager/Manager.scala`
- `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/manager/Parameters.scala`
- `/home/ubuntu/chipyard/generators/rerocc/src/main/scala/Configs.scala`
- `/home/ubuntu/chipyard/generators/chipyard/src/main/scala/config/fragments/ReRoCCLooseCoupledFragments.scala`
- `/home/ubuntu/chipyard/generators/chipyard/src/main/scala/config/fragments/ReRoCCCoupledDMAManagerFragments.scala`
- `/home/ubuntu/chipyard/generators/rocket-chip/src/main/scala/tile/LazyRoCC.scala`
- `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/Controller.scala`
- `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`
- `/home/ubuntu/chipyard/generators/gemmini/src/main/scala/gemmini/CoupledSharedSpadRegistry.scala`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_matrix_baremetal_coupleddma.c`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c`

## 9. 给下一个 AI 的 Prompt

```text
你现在在 /home/ubuntu/chipyard 仓库里继续接手 Gemmini + CoupledDMA 的 pair-wrapper manager 工作。

先按以下顺序阅读：

1. /home/ubuntu/chipyard/AGENTS.md
2. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/README.md
3. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/PAPER_HARDWARE_ARCHITECTURE.md
4. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pair_wrapper_manager_plan_20260405.md
5. /home/ubuntu/chipyard/tmp/firesim-aws-f2/HANDOFF_dummy_gemmini_buildbitstream_20260403.md

当前冻结结论：

- 旧的 pre-NoC grouped attachment / aggregatePairedManagers 路线已经放弃。
- 该路线的活动代码已经从当前工作树回退，不要再基于它继续叠加实现。
- 当前应该实现的是真正的 pair-wrapper manager：
  - 一个 `(Gemmini i + CoupledDMA i)` 对外是一个物理 ReRoCC manager
  - `custom3` 仍给 Gemmini
  - `custom2` 仍给 DMA
  - 同一个 acquired cfg 需要能绑定并保留这两个 opcode

重要约束：

- 不要恢复 aggregatePairedManagers / AttachmentGrouping / NamedTLSourceShrinker 路线
- 不要先改大软件栈
- 先做小配置
- 先做硬件 wrapper + ReRoCC manager 语义扩展

建议实施顺序：

1. 在 generators/gemmini/src/main/scala/gemmini/ 下新增 pair-wrapper LazyRoCC 模块
2. 扩展 generators/rerocc/src/main/scala/manager/Manager.scala，使 pair-wrapper manager 可以保留 incoming opcode，而不是强制重写成单一 opcode
3. 新增独立 config/fragment，不要复用已废弃的 grouped-attachment 路径
4. 先让 2CPU/2pair 小配置 elaboration 通过
5. 用已有 baremetal 做快速验证：
   - rerocc_lc_matrix_baremetal_coupleddma
   - explicit interleaved / shared-spad focused baremetal
   - nonblocking overlap baremetal
6. DMA bytes 和迭代数尽量压小，例如 bytes=512、iters=1
7. 只有当 baremetal 无法表达“同一 manager、双 opcode”时，再评估软件适配

测试纪律：

- 先 compile/elaboration，再 baremetal
- 暂不先跑 Linux / FireSim / pipeline runtime
- 暂不先碰 12-pair FPGA bitstream

当前保留但不属于旧路线本身的改动：

- /home/ubuntu/chipyard/generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigHelpers.scala
  - 这是通用布局辅助工具，保留 useCompactPairedManagerLayout
- /home/ubuntu/chipyard/generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigs.scala
  - 保留了大 dummy-Gemmini / sbus 变体配置
- /home/ubuntu/chipyard/sims/firesim 子树
  - 仍有 build host / swap / local-only build 相关改动，属于 buildbitstream 工作，不是 pair-wrapper 路线

如果你需要查看旧路线的历史分析，只把 PAPER_HARDWARE_ARCHITECTURE.md 里 2026-04-04/05 的相关部分当作归档材料，不要把它们当成当前实现基线。
```
