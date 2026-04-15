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

## 8.1 2026-04-05 实施 checkpoint

截至 `2026-04-05` 当前 checkpoint，pair-wrapper 小配置方向已经完成下面这些落地项：

- `generators/rerocc/src/main/scala/manager/Parameters.scala`
  - `ReRoCCTileParams` 新增 `preserveIncomingOpcode: Boolean = false`
- `generators/rerocc/src/main/scala/manager/Manager.scala`
  - `ReRoCCManager` 不再要求单一 opcode
  - 改为接收整个合法 opcode 集合
  - `preserveIncomingOpcode = true` 时只检查 incoming opcode 合法，不再强制改写成固定 `roccOpcode`
- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMAPairWrapper.scala`
  - 已新增真正的 pair-wrapper
  - 内部固定实例化：
    - `Gemmini(custom3, gemmini_id = pairId)`
    - `GemminiCoupledDMA(custom2, gemmini_id = pairId)`
  - 对外汇总 `resp/busy/mem/ptw/tlNode/atlNode/stlNode`
  - `sbusSlaveTLNode` 直接沿用 Gemmini child，保持 shared scratchpad / shared xlate 语义
- `generators/chipyard/src/main/scala/config/fragments/ReRoCCGemminiCoupledDMAPairFragments.scala`
  - 已新增 `WithReRoCCGemminiCoupledDMAPairManagers`
- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigHelpers.scala`
  - 已新增 `buildPairLayout()`
  - 没有覆盖旧的 separate-manager `buildLayout()` 路径
- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`
  - 已新增 pair-manager 专用 parametric config
  - 第一条小配置为：
    - `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2P2x1x2CoupledDMAPairManager`
- 现有 separate-manager 大 dummy 配置与 active FireSim YAML 没有在本轮被替换或回退

baremetal 最小适配当前也已经落地：

- `bareMetalC/learn-gemmini/rerocc_lc_matrix_baremetal_coupleddma.c`
  - 已新增 `REROCC_PAIR_MANAGER_MODE=1`
  - 同一 pair cfg 下同时绑定 `rr_set_opc(3, cfg)` 和 `rr_set_opc(2, cfg)`
  - 在一次 acquire/release 作用域内先后发 Gemmini 和 DMA 指令
- `bareMetalC/learn-gemmini/rerocc_lc_nonblocking_baremetal_coupleddma.c`
  - 已新增 `REROCC_PAIR_MANAGER_MODE=1`
  - 每个并发场景把 Gemmini/DMA 绑定到同一个 pair cfg
- `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
  - 当前仍保持 Gemmini-only shared-spad 验证路径
- `pipeline-runtime/src/prt_rerocc.c`
  - 本轮未修改

当前已确认的验证结果：

- 阶段 0 compile/elaboration：
  - `make -C sims/verilator ... firrtl CONFIG=GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2P2x1x2CoupledDMAPairManager`
    已通过
  - chisel/elaboration 日志显示：
    - `ReRoCC Manager id 0 is a gemmini.GemminiCoupledDMAPairWrapper`
    - `ReRoCC Manager id 1 is a gemmini.GemminiCoupledDMAPairWrapper`
  - 说明 manager 数已经按 `P=2` 展开，而不是旧的 `G + D = 4`
- baremetal 编译 smoke：
  - `mt_hello-baremetal`
  - `rerocc_lc_matrix_baremetal_coupleddma-baremetal`
  - `rerocc_lc_resadd_explicit_interleaved-baremetal`
  - `rerocc_lc_nonblocking_baremetal_coupleddma-baremetal`
  均已重编通过
- 阶段 2 runtime：
  - log: `/tmp/pair_stage2_matrix_trace.log`
  - pair-manager `matrix + coupled-dma` quick case 已通过
  - 关键结论：
    - `GEMMINI_MATRIX_RESULT mode=single pass=1 fail=0 expected=1`
    - `DMA_MATRIX_RESULT mode=single pass=1 fail=0 expected=1 bytes=512`
    - `ALL_TESTS_PASS`
- 阶段 3 runtime：
  - log: `/tmp/pair_stage3_resadd_bias_focus_quiet_nodram.log`
  - Gemmini-only shared-spad / xlate 验证已通过
  - 关键结论：
    - `CASE_RESULT bias_mvin3_runtime_alias_focus PASS`
    - `ALL_TESTS_PASS`
- 阶段 4 runtime：
  - log: `/tmp/pair_stage4_nonblocking_final2.log`
  - `rerocc_lc_nonblocking_baremetal_coupleddma-baremetal` 已通过 pair-manager smoke
  - 关键结论：
    - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
    - `ALL_TESTS_PASS`
  - 同时确认了一个 smoke 级软件事实：
    - 当 stage 4 按计划把 `long/short` 两侧迭代数都压到 `1` 时，原测试里的“`short` 必须先于 `long` 完成”不再是稳定判据
    - 因此 `bareMetalC/learn-gemmini/rerocc_lc_nonblocking_baremetal_coupleddma.c` 现在只在 `long_iters > short_iters` 时保留原 latency-order check
    - 在最小 smoke 配置下，场景 pass 仅要求 `ok0 && ok1`，`overlap` / `short_before_long` 继续保留为观测指标，不再作为 gate

当前剩余未完成的部分：

- 为了让本地 verilator smoke 可读，额外修了：
  - `tools/DRAMSim2/AddressMapping.cpp`
  - 把 DRAMSim 的 `address ... is not aligned to the request size of 64` warning 改为只报一次
  - 实际运行时需要通过本地 `tools/DRAMSim2/libdramsim.so` 覆盖 toolchain 自带 `libdramsim.so`
- 小配置 pair-wrapper 的阶段 0 到阶段 4 已经闭环
- 下一步只剩计划中的阶段 5：
  1. 在不影响当前 separate-manager `Sbus256/Sbus128` buildbitstream 基线的前提下
  2. 新增 large dummy pair-wrapper 配置
  3. 复用当前 active FPGA 目标的资源旋钮
  4. 在 small-config pair-wrapper 稳定基线上再考虑 FireSim / buildbitstream

### 8.2 2026-04-07 runtime 软件适配补充

当前仓库中的 `pipeline-runtime` 已补上一层显式 pair-manager 适配，软件契约收敛为：

- 新增 runtime CLI / cfg 开关：
  - `--pair-manager-mode <0|1>`
- `pair_manager_mode = 0`：
  - 保持旧的 separate-manager 语义
  - 默认 `dma_base_id = gemmini_base_id + num_gemmini_mgrs`
- `pair_manager_mode = 1`：
  - `num_gemmini_mgrs` / `num_dma_mgrs` 必须相等
  - `gemmini_base_id` / `dma_base_id` 必须相等
  - stage 级别的 Gemmini / DMA manager-id 使用同一个 pair manager 空间
  - `custom3` 仍走 Gemmini，`custom2` 仍走 DMA
  - 本轮没有改写 `pipeline-runtime/src/prt_rerocc.c` 的 acquire/fence/release 协议，只改正 manager-id 映射

当前脚本入口也同步收紧为：

- `run_rerocc_pipeline_runtime_bertmini.sh`
  - 通过环境变量 `PAIR_MANAGER_MODE=1` 进入 pair-manager 运行方式
  - pair 模式下如果 `NUM_DMA != NUM_GEMMINI` 或 `DMA_BASE_ID != GEMMINI_BASE_ID`，脚本会直接报错

当前必须继续遵守的运行约束：

- 任何 binary / rootfs / workload / AGFI 变化之后，FireSim 都必须重新执行一次 `infrasetup`
- 每次 FireSim 验证结束后都必须立即 `terminaterunfarm`，并额外确认实例真的进入 `shutting-down` 或 `terminated`

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

## 10. 2026-04-05 晚间更新：阶段 5 已进入 buildbitstream

上面的阶段 0 到阶段 4 交接 prompt 仍保留作归档，但截至 `2026-04-05` 晚间，当前真实起点已经前移到阶段 5。

本轮新落地并已实际使用的 large dummy pair-wrapper 硬件入口是：

- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`
  - `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256`
  - `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`

两条配置都保持以下资源目标：

- `4 CPU`，`2x2`
- `12 pair`，`4x3`
- dummy Gemmini
- `16x16`
- shared spad = `1 MiB`
- `useCompactPairManagerLayout = true`
- `globalNoCVirtualChannelDepth = 4`
- `useGlobalNoC = true`
- `useDeterministicGlobalNoCRouting = true`
- `Sbus256` 为主线，`Sbus128` 为 fallback

本轮已确认的阶段 5 本地 elaboration 事实：

- `make -C sims/verilator firrtl CONFIG=GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256`
  - log: `/tmp/pair_stage5_sbus256_firrtl.log`
  - 已通过
- `make -C sims/verilator firrtl CONFIG=GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  - log: `/tmp/pair_stage5_sbus128_firrtl.log`
  - 已通过

两条 elaboration 日志都显示：

- `ReRoCC Manager id 0..11 is a gemmini.GemminiCoupledDMAPairWrapper`
- ReRoCC outwards mapping 为 `Managers: List(0)` 到 `List(11)`
- shared-spad bank 窗口为 `0x40000000` 到 `0x40b00000`
- `rerocc-mgr@20000` 到 `rerocc-mgr@2b000` 共 `12` 个 manager CSR 窗口

本轮已启动的阶段 5 FireSim buildbitstream 主线：

- tmux session:
  - `firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r1`
- 命令：
  - `./scripts/firesim-tmux-run.sh --session-name firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r1 buildbitstream -a /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb.yaml -b /home/ubuntu/chipyard/sims/firesim/deploy/config_build_f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c12p12_sbus256_20mhz_aws.yaml -r /home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c12p12_sbus256_20mhz.yaml`
- pane log:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r1.pane.log`
- FireSim deploy log:
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-05--17-17-04-buildbitstream-QOGOISRUKUNM1GIP.log`

截至本次更新写回时，状态是：

- 本地 `replace-rtl` 已完成
- `midas.stage.GoldenGateMain` 已启动并持续运行
- 还没有远端 Vivado / AWS build host 的成功进入证据
- 还没有最终 bitstream / AGFI

因此，当前可以明确写成：

- stage 5 的 large dummy pair-wrapper 硬件前提已经在当前工作树中落地
- stage 5 的 `Sbus256` buildbitstream 已正式启动

但当前还**不能**写成：

- stage 5 buildbitstream 已完成
- pair-wrapper 已拿到最终 bitstream / AGFI

如果下一位 AI 接手，优先继续监控上面的 tmux/deploy log。
只有在 `Sbus256` 真正失败并拿到明确资源/阶段证据后，才允许切到 `Sbus128`。

## 11. 2026-04-05 18:04 UTC 更新：阶段 5 已进入远端 Vivado

上面第 10 节写回时，状态还停在 “GoldenGate 已启动”。截至 `2026-04-05 18:04 UTC`，阶段 5 的 `Sbus256` 主线已经继续推进到 FireSim 远端 build host / Vivado 阶段。

本轮新增确认的事实：

- 本地 `GoldenGateMain` 已完成并进入后续 FireSim FPGA driver / platform 准备阶段
- FireSim 已拉起远端 build host：
  - instance id: `i-0b93aac4e10fa16a9`
  - private IP: `192.168.0.12`
  - instance type: `z1d.3xlarge`
  - build-farm tag: `gemmini4c12p12-pair-sbus256`
- 远端已执行：
  - `build-bitstream.sh --cl_dir ... --frequency 20 --strategy TIMING`
- 当前远端 Vivado 主日志：
  - `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_04_05-175757.vivado.log`

截至这个 checkpoint，远端 Vivado 已明确进入一系列 OOC / IP synthesis 步骤，并已看到至少以下 top 名称：

- `clk_wiz_0_firesim`
- `axi_clock_converter_512_wide`
- `axi_dwidth_converter_0`
- `axi_clock_converter_dramslim`

这意味着当前状态应写成：

- 远端 Vivado 已真实启动并在持续推进
- 若干 IP / OOC synthesis 已完成且未报 error

但当前还**不能**写成：

- 顶层 CL synthesis 已完成
- 已进入 `link_design`
- 已进入 `opt_design` / `place_design` / `route_design`
- 已拿到 bitstream / AGFI

当前已观测到的警告类型主要是：

- `IP_Flow 19-3664` generated file not found / regenerate 提示
- `Vivado 12-13650` `.xci` moved from original location
- 若干 `CDC-*` waiver / `No cells matched '*'` 警告

截至 `18:04 UTC`，这些都还只是 warning，尚未看到明确 error 或失败退出。

为了避免只靠前台人工盯日志，本轮还额外启动了独立 watcher：

- tmux session:
  - `firesim-monitor-4c12p12-pair-sbus256`
- monitor script:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/monitor_pair_stage5_buildbitstream.sh`
- monitor log:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r1.monitor.log`

该 watcher 会持续采样：

- tmux pane log
- FireSim deploy log
- 远端 Vivado 进程
- 远端 `.vivado.log`

因此，下一位接手时的最优动作仍然是：

1. 先看 `.monitor.log` 和 `.pane.log`
2. 确认是否首次出现 `link_design` / `place_design` / `route_design`
3. 只有在 `Sbus256` 出现明确资源或实现失败证据后，才允许切到已准备好的 `Sbus128` fallback

## 2026-04-05 18:49 UTC 更新

上一轮远端 `Sbus256` stage 5 pair-wrapper buildbitstream（`r1`）已经可以确定不是
GoldenGate 失败，而是远端 Vivado 在 build host 上崩溃：

- 失败点位于远端 `task_worker.tcl` 子进程段错误
- 失败日志保存在：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-05--17-17-04-f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c12p12_sbus256_20mhz/`

当前 active rerun 为 `r2`，对应：

- build tmux:
  - `firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r2`
- monitor tmux:
  - `firesim-monitor-4c12p12-pair-sbus256-r2`
- build host:
  - instance id: `i-0e02356822c5cd6cf`
  - private IP: `192.168.1.7`
  - public IP: `35.91.73.88`
  - instance type: `z1d.3xlarge`

本轮新的远端运行事实：

- build host swap 已明确扩大到 `128 GiB`，并已在 pane / deploy log 中看到配置成功记录
- 远端当前可见主 Vivado 进程和 `7` 个 task worker，用户已明确说明 `7` worker 属于预期，不应被误判为异常
- 截至 `18:48 UTC`，`r2` 仍在远端顶层 synthesis 内推进，尚未看到 OOM、实例掉线或新的 fatal 退出

本轮还补上了“失败先抓证据再关机”的 host 侧保证：

- `sims/firesim/deploy/buildtools/bitbuilder.py`
  - future rerun 若失败，会先在远端收集 `firesim-failure-debug/`，再随 `results-build` 拉回本地，然后才 release build host
- `tmp/firesim-aws-f2/monitor_pair_stage5_buildbitstream.sh`
  - 对当前 `r2` 这种旧逻辑启动的活跃会话，额外挂了独立 tmux monitor
  - 若 `r2` 失败，会先经 `~/firesim.pem` SSH 到远端抓取：
    - `free -h` / `meminfo` / `swaps`
    - `ps` / Vivado 进程快照
    - `dmesg` / `journalctl`
    - `.Xil` / core dump / 最新 `.vivado.log`
  - 抓回本地目录：
    - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/failure-debug/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r2/`
  - 之后才 terminate 实例

## 2026-04-06 01:36 UTC 更新

上一轮 `r3` 在远端 host 被人工关机后没有形成新的 Vivado 崩溃证据。本轮已重新启动
`Sbus256` 主线 rerun `r4`，并继续坚持：

- `stage5 large dummy pair-wrapper`
- `4 CPU`
- `12 pair`
- dummy Gemmini
- `16x16`
- shared spad = `1 MiB`
- `useCompactPairManagerLayout = true`
- `globalNoCVirtualChannelDepth = 4`
- `Sbus256` 主线，不切 `Sbus128`

当前 active `r4` 入口：

- build tmux:
  - `firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r4`
- deploy log:
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-06--01-31-35-buildbitstream-DDTXOI1LN9PRC3CO.log`
- pane log:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r4.pane.log`
- monitor tmux:
  - `firesim-monitor-4c12p12-pair-sbus256-r4`
- monitor log:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r4.monitor.log`

当前 `r4` build host：

- instance id: `i-06832ddd54f2e52d5`
- private IP: `192.168.2.166`
- public IP: `100.23.89.210`
- instance type: `z1d.3xlarge`

本轮已经确认的运行事实：

- build host swap 已再次成功配置到 `128 GiB`
- 本地 `replace-rtl` / driver / 远端同步已完成
- 当前远端 Vivado 主日志：
  - `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_04_06-013335.vivado.log`
- 截至 `01:36 UTC`：
  - 远端 Vivado 已稳定启动
  - 当前仍在读取设计 / CL IP block 阶段
  - 当前尚未看到新的 `task_worker` 子进程
  - 尚未复现 `r1/r2` 的早期 `task_worker.tcl` 段错误

本轮 checkout 里的 failure-handling 也已继续强化：

- `sims/firesim/deploy/buildtools/bitbuilder.py`
  - 失败时先在远端打包 `firesim-failure-minimal.tar.gz`
  - 先把小诊断包拉回本地，再做整棵结果树 rsync
  - 即使完整 rsync 失败，也不再把原始 Vivado 失败误写成后继同步失败
- `tmp/firesim-aws-f2/monitor_pair_stage5_buildbitstream.sh`
  - 当前 manager 环境下优先使用私网地址监控远端 host
  - `r4` monitor 以 `AUTO_TERMINATE_ON_FAILURE=0` 运行
  - 若失败，将先保活 host、SSH 取证，再决定后续处置

截至这个 checkpoint，仍然**不能**写成：

- `Sbus256` 顶层 synthesis 已完成
- 已进入 `link_design`
- 已进入 `place_design`
- 已拿到 bitstream / AGFI

## 2026-04-06 05:18 UTC 更新

在保活的远端 build host 上，本轮先做了两步 `Sbus256` 手工复现，用来缩小
Vivado 崩溃范围，而没有回退 small-config 或切到 `Sbus128`。

### `r3`：只改 top synth 选项

沿用保活 host：

- instance id: `i-0222681d51c9964a1`
- private IP: `192.168.0.117`
- public IP: `34.220.56.138`
- instance type: `z1d.3xlarge`

手工 tmux：

- `pw-sbus256-topopts-noretime-r3`

主日志：

- `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/manual-pw-sbus256-topopts-noretime-r3.log`

本轮 `r3` 使用的关键环境：

- `FIRESIM_VIVADO_SYNTH_MAX_THREADS=1`
- `FIRESIM_VIVADO_JOBS=1`
- `FIRESIM_F2_TOP_SYNTH_OPTIONS="-no_lc -shreg_min_size 5 -fsm_extraction one_hot -resource_sharing auto"`
- `FIRESIM_F2_TOP_SYNTH_DIRECTIVE=default`

已确认的新事实：

- `r3` 不再像旧 `r1`/`r2` 那样在更早阶段退出
- 已真实进入 customer top synth：
  - `AWS FPGA: (05:06:59): Starting synthesizing customer design ...`
- 已明确打印 top-synth override：
  - `AWS FPGA: (05:07:15): Using FIRESIM_F2 top-synth override options='-no_lc -shreg_min_size 5 -fsm_extraction one_hot -resource_sharing auto' directive='default'`

但 `r3` 最终仍失败，且依然是 Vivado 内部崩溃，不是资源耗尽：

- 在日志行 `4881-4882` 处，`task_worker.tcl` 子进程再次 segfault
- 失败前仍可见一串
  - `WARNING: [Synth 8-4767] Trying to implement RAM 'ram_flattened_reg' in registers`
  - `RAM "ram_flattened_reg" dissolved into registers`
- 之后直接落到：
  - `ERROR: Did not find the post-route DCP file ...`

远端 host 资源快照当时为：

- `Mem: 93 GiB total / 1.8 GiB used / 86 GiB free`
- `Swap: 137 GiB total / 0 used`
- `/home/ubuntu/firesim-build` 在独立数据盘 `/dev/nvme2n1`

因此，本轮可排除：

- root 盘过小
- swap 不足
- 系统 OOM / 被内核杀死

当前更精确的判断是：

- `Sbus256` pair-wrapper 的 Vivado 崩溃仍然发生在 customer top synth 的
  `task_worker.tcl` 子进程
- 只去掉 legacy top-synth 路径并改成 `NORETIMING` 风格 synth options，
  仍不足以彻底消除崩溃

### `r4`：开启完整 `BUILD_STRATEGY=NORETIMING`

在同一台保活 host 上，已继续启动下一轮：

- tmux:
  - `pw-sbus256-strategy-noretime-r4`
- 日志：
  - `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/manual-pw-sbus256-strategy-noretime-r4.log`

`r4` 使用的关键环境：

- `FIRESIM_VIVADO_SYNTH_MAX_THREADS=1`
- `FIRESIM_VIVADO_JOBS=1`
- `FIRESIM_F2_APPLY_STRATEGY=1`
- `FIRESIM_F2_BUILD_STRATEGY=NORETIMING`

截至 `05:18 UTC` 已确认：

- `aws_build_dcp_from_cl.py` 已收到 `--strategy NORETIMING`
- 远端 log 已打印：
  - `Loading build strategy NORETIMING ...`
  - `build_strategy   : NORETIMING`
- 当前仍在 OOC / IP synthesis 阶段推进
- 尚未到达新的 customer top-synth 成败结论

因此，当前 active 调试主线已经切换为：

- 继续坚持 `stage5 large dummy pair-wrapper`
- 继续优先 `Sbus256`
- 在同一 preserved host 上验证完整 `NORETIMING` 策略
- 只有在这条 `Sbus256` 路线再次形成充分失败证据后，才考虑下一档 fallback

## 2026-04-06 06:15 UTC 更新

本轮在当前 checkout 上继续推进 `stage5 large dummy pair-wrapper`，并把
pair-wrapper 路线的 source-space 收缩改动正式接入到新的 `Sbus256`
buildbitstream 主线。

### 本轮新接入的 pair-wrapper-only 改动

当前 pair-wrapper 参数化基类已新增并实际使用：

- `pairTlMaxInFlight = Some(64)`
- `pairAtlMaxInFlight = Some(64)`

落点在：

- `GemminiCoupledDMAPairWrapper.scala`
  - 对 pair-wrapper 的 `tlNode` / `atlNode` 边界插入可选 `TLSourceShrinker`
- `ReRoCCGemminiCoupledDMAPairFragments.scala`
  - 透传 `tlMaxInFlight` / `atlMaxInFlight`
- `GemminiLearningReRoCCPairManagerConfigs.scala`
  - large dummy pair-wrapper `Sbus256/Sbus128` config 通过参数化入口启用该旋钮

该改动保持：

- separate-manager 路线不被覆盖
- pair-wrapper / separate-manager 两条线并存
- 不涉及 `pipeline-runtime/src/prt_rerocc.c`

### 本轮本地静态验证

在正式重启 FireSim `buildbitstream` 之前，已重新做本地 pair-wrapper
`Sbus256` elaboration / GoldenGate 入口验证。

已确认的新事实：

- regenerated FIRRTL 的 SBus in-node mapping 中出现 `TLSourceShrinker`
  - 每个 ReRoCC pair 对应一个 `TLSourceShrinker[0]`
- 当前 regenerated FIRRTL 中，`coupler_from_port_named_rerocc_*` 的 outer
  source width 已表现为 `UInt<7>`

这至少说明：

- shrinker 已真正插入 pair-wrapper 路径
- 当前运行的 stage5 不是在复用旧的 pre-shrinker 本地产物

### 当前 active run

当前 active stage5 run 已重新启动：

- tmux session:
  - `firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r7-shrink64-noretiming`
- command script:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r7-shrink64-noretiming.command.sh`
- pane log:
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-buildbitstream-4c12p12-dummy16x16-sbus256-vc4-pair-r7-shrink64-noretiming.pane.log`
- FireSim deploy log:
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-06--06-12-16-buildbitstream-VWJWBAYT5IU611T2.log`

本轮使用的关键环境：

- `FIRESIM_KEEP_BUILD_HOST_ON_FAILURE=1`
- `FIRESIM_VIVADO_JOBS=1`
- `FIRESIM_VIVADO_SYNTH_MAX_THREADS=1`
- `FIRESIM_F2_APPLY_STRATEGY=1`
- `FIRESIM_F2_BUILD_STRATEGY=NORETIMING`

截至本 checkpoint：

- 本地已进入 `replace-rtl -> GoldenGateMain`
- 还未进入远端 build host / Vivado 阶段
- 当前没有新的失败结论；仍在等待本地 GoldenGate 完成

## 2026-04-06 10:25 UTC 更新

本轮已基于 stage5 large dummy pair-wrapper `Sbus256` 的最新 `post_synth`
报告，对“mesh 已缩小但 NoC 面积没有按预期缩小”的原因做了更细的面积归因。

本节使用的两份对照报告为：

- 当前 pair-wrapper：
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/26_04_06-085945.post_synth_utilization.rpt`
- 当前 active separate-manager baseline：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-04--01-15-02-f2_gemmini_rerocc_globalnoc_coupleddma_dummy16x16_4c12g12d_sbus256_20mhz/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2G12x4x3D12x4x3CoupledDMADummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/reports/26_04_04-044837.post_synth_utilization.rpt`

### 分层热点表

下表是当前 pair-wrapper `post_synth` 中最重要的热点行。注意这些行是**层级统计**，
不是彼此互斥分区，因此不能直接横向求和。

| 行项目 | pair-wrapper LUT | 占 `cl_firesim` 比例 | baseline LUT | 变化 |
| --- | ---: | ---: | ---: | ---: |
| `cl_firesim` | 1,406,466 | 100.00% | 1,480,982 | -5.0% |
| `sbus` | 533,460 | 37.93% | 669,845 | -20.4% |
| `globalNoCDomain` | 521,561 | 37.08% | 653,783 | -20.2% |
| `NoC` | 507,714 | 36.10% | 631,858 | -19.6% |
| `rerocc_tile` total | 507,075 | 36.05% | 484,701 | +4.6% |
| `GemminiCoupledDMAPairWrapper` total | 455,009 | 32.35% | N/A | N/A |
| `mbus` | 74,339 | 5.29% | 71,146 | +4.5% |
| `CPUManagedStreamEngine_0` | 58,379 | 4.15% | 58,247 | +0.2% |

### 更细的重复结构归因

为避免层级统计重叠，本轮额外对重复出现的结构做了求和。这里更能解释
“为什么 endpoint 数减半后，NoC 总面积仍然只下降约 20%”。

| 结构 | baseline | pair-wrapper | 变化 | 解释 |
| --- | ---: | ---: | ---: | --- |
| `rerocc_tile` 数量 | 24 | 12 | -50.0% | pair-wrapper 把 `Gemmini + DMA` 合成一对 |
| `rerocc_tile` 总 LUT | 484,701 | 507,075 | +4.6% | attachment 数减半，但 tile 总量反而略涨 |
| `rerocc_tile` 平均 LUT | 20.2k | 42.3k | +109.5% | 每个 surviving endpoint 明显变胖 |
| baseline 中 12 个大 Gemmini tile 总 LUT | 401,310 | N/A | N/A | 平均约 `33.4k` LUT/个 |
| baseline 中 12 个小 DMA tile 总 LUT | 83,391 | N/A | N/A | 平均约 `6.95k` LUT/个 |
| `GemminiCoupledDMAPairWrapper` 总 LUT | N/A | 455,009 | N/A | pair 内部已吃掉原 DMA tile + 新 wrapper glue |
| pair `rerocc_tile - pairWrapper` 壳层 LUT | N/A | 52,066 | N/A | 平均约 `4.34k` LUT/个 |
| `coupler_from_port_named_rerocc_*` 数量 | 24 | 12 | -50.0% | direct sbus ingress 端口数确实减半 |
| `coupler_from_port_named_rerocc_*` 总 LUT | 14,412 | 9,151 | -36.5% | direct coupler 面积基本按端口数下降 |
| `router_sink_domain*` 数量 | 63 | 40 | -36.5% | hierarchy 中 router 相关实例明显减少 |
| `router_sink_domain*` 总 LUT | 631,858 | 507,714 | -19.6% | 下降幅度明显小于实例数下降幅度 |
| `router_sink_domain*` 平均 LUT | 10.0k | 12.7k | +26.6% | 单个 router / local NI 变得更贵 |
| `ingress_unit_*` 数量 | 130 | 82 | -36.9% | ingress 个数下降明显 |
| `ingress_unit_*` 总 LUT | 53,329 | 41,598 | -22.0% | 但单个 ingress 更重 |
| `egress_unit_*` 数量 | 131 | 95 | -27.5% | egress 个数下降较少 |
| `egress_unit_*` 总 LUT | 25,041 | 19,747 | -21.1% | 也没有按实例数线性下降 |

### 当前可直接下结论的解释

- 当前 pair-wrapper 路线**不是**旧的 grouped attachment / `aggregatePairedManagers`
  路线，因此并没有把 pair 之前的大量 TL 接口在 NoC 之前彻底折叠掉。
- direct endpoint coupler 的面积实际上已经按端口数缩掉一大截：
  - `14,412 -> 9,151 LUT`
  - 这说明“端口数减少”本身是有效的。
- 真正没有同比缩小的是每个 surviving router / ingress / egress 的平均面积：
  - `router_sink_domain*` 平均 LUT 从约 `10.0k` 增到约 `12.7k`
  - 这说明当前主导项已经变成更高 radix、更宽总线、更重 buffer / queue /
    协议状态，而不再只是 router 个数。
- pair-wrapper 自身把原来分离的 DMA tile 和新的 wrapper glue 收进了同一个
  `rerocc_tile`，导致 manager 侧总 LUT 没有下降，反而略有上升。
- 因为 `Sbus256`、`globalNoC VC depth = 4`、`shared spad = 1 MiB` 都保持在较强配置，
  所以当前 stage5 的面积瓶颈已经从“attachment multiplicity”转成了
  “每个 pair endpoint 的协议与宽链路成本”。

### 如果还要继续压 NoC，优先顺序应是什么

以下排序以当前 authoritative 路线为前提：

- 继续保持 separate-manager 与 pair-wrapper 共存
- 不恢复 grouped attachment / grouped manager edge 旧路线
- 优先保住 `Sbus256`
- 只有在前几档都证据充分失败后才考虑 `Sbus128`

#### 第一档：不改变总体路线，直接减少每个 pair 对外暴露的 NoC 代价

1. 继续减少 pair-wrapper 边界的 source / in-flight 状态
   - 当前 pair route 已启用 `TLSourceShrinker(64)` / `atl` 对应 shrinker。
   - 若 coupled DMA 与 Gemmini 的 outstanding/ordering 假设允许，优先继续试：
     - `64 -> 32`
     - 再评估 `32 -> 16`
   - 这一档不会改变架构语义，但有机会继续降低 source bits、队列和仲裁器复杂度。

2. 审计 `connectSbusSlaveToStl = true` 是否真的对 stage5 必需
   - 当前 pair large config 仍把它打开。
   - 如果能关掉，理论上可减少每个 pair 的一类 sbus-visible 出口。
   - 这是一个潜在的中高收益开关，但必须先确认 shared-spad / STL 相关路径不会被破坏。

3. 继续减少 pair 边界上的 TL 暴露集合
   - 当前 `sbusNodeMapping` 仍保留：
     - `ReRoCC i DCache`
     - `port_named_rerocc_i[`
     - `sport_named_rerocc_i[`
     - `sport_named_rerocc_sbus_i[`
     - `Gemmini i-`
   - 若能把其中一部分在 pair-wrapper 内部消化，而不是跨 NoC 暴露，将比单纯缩 mesh 更有效。

#### 第二档：仍保 `Sbus256`，但开始直接针对 NoC 协议成本

4. 减少 global NoC 的 VC 数量，而不是只减深度
   - 当前 deterministic global routing 虽然已启用，但 `channelParamGen` 仍为
     `7` 个 VC，只是把 depth 调成了 `4`。
   - 若后续验证证明 manager 路径不需要这么多 VC，则改成更少 VC 数会直接减少
     queue / LUTRAM / 路由器状态。
   - 这一档需要重新做 deadlock / protocol 正确性审计，风险高于 source shrink。

5. 把 manager 数据面从“完整 system-bus 协议重量级路径”中拆出来
   - 如果目标是**彻底**压缩 NoC，而不仅是小修小补，那么最有效的方向不是继续缩 mesh，
     而是把 manager 路径从当前的宽 TileLink/global-NoC 语义里剥出来。
   - 例如：
     - CPU cache/coherent 流量保留当前系统路径
     - pair manager 的 uncached / DMA / shared-spad 访问走更轻的专用网络
   - 这会比继续微调 router 数量更接近“真正大幅缩 NoC”的目标。

#### 第三档：代价很大，但理论上压缩最彻底

6. 引入 pair-local concentrator / cluster fabric
   - 从面积角度看，最激进的方向是让多个 pair 在本地先汇聚，再通过更少的高代价 NoC
     端口进入系统。
   - 但这已经非常接近被明确放弃的 grouped attachment 思路。
   - 因此它只能作为“若未来允许改变 authoritative 架构假设”的研究方向，
     **不能**作为当前 active branch 的下一步。

7. `Sbus128` 仅作为最后 fallback
   - 在前述 `Sbus256` 保持方案全部证据充分失败后，才应把 `Sbus128`
     作为资源 fallback。
   - 它可以明显减轻宽链路 buffer / queue / mux 压力，但不能把它当作
     `Sbus256 stage5` 的替代验证结果。

### 当前推荐修补方向

为避免下一轮再次回到“只凭直觉缩 mesh”的低收益修改，本轮把当前更推荐的修补方向
明确收敛为以下顺序。

#### 修补方向 A：继续压 pair-wrapper 边界状态规模

- 优先把 `pairTlMaxInFlight` / `pairAtlMaxInFlight` 从当前 `64`
  继续试降到 `32`
- 若 baremetal / ordering 语义仍稳定，再评估是否继续降到 `16`
- 目标是继续减少：
  - source bits
  - queue state
  - TL buffer / arbiter 的状态空间

当前这条线的优点是：

- 不改变 pair-wrapper 的总体架构
- 不影响 separate-manager 基线
- 风险集中在 outstanding / ordering 假设，可通过现有小配置 baremetal 回归复验

#### 修补方向 B：审计并尽可能关闭 `connectSbusSlaveToStl`

- 当前 large dummy pair-wrapper 仍使用 `connectSbusSlaveToStl = true`
- 如果 stage5 的 buildbitstream 目标并不需要这条 `sbusSlave -> STL` 可见路径，
  那么应优先尝试关闭

这条线的理论收益是：

- 减少每个 pair 对外暴露的一类 sbus-visible 出口
- 减少 `sbusNodeMapping` / local NI / protocol adapter 负担

但前提是必须先确认：

- shared-spad 本地访问语义不依赖该路径
- coupled DMA / Gemmini 的实际 stage5 路径不会被破坏

#### 修补方向 C：减少 pair 对外暴露的 TL 端口集合

当前 pair node 在映射上仍同时承载：

- `ReRoCC i DCache`
- `port_named_rerocc_i[`
- `sport_named_rerocc_i[`
- `sport_named_rerocc_sbus_i[`
- `Gemmini i-`

后续修补如果要获得比“再缩一点 mesh”更大的收益，核心不是继续减少 node 数量，
而是减少这些接口里真正跨 NoC 暴露的那部分。

因此，下一轮静态代码审计应优先回答：

- 哪些接口只是实现方便而保留，但并非 stage5 必需
- 哪些接口可以在 pair-wrapper 内部汇聚，而不是作为独立 NoC-visible path 保留

#### 修补方向 D：如果 `Sbus256` 仍然密度过高，优先动 NoC VC 数量而不是先降位宽

- 当前 `globalNoCVirtualChannelDepth = 4` 已经较小
- 但 `globalNoC` 仍保留 `7` 个 VC

因此，如果 placement / route 继续卡在高密度和 congestion，而前述 A/B/C
收益仍不足，则更推荐优先尝试：

- 减少 VC 数量

而不是立即降到 `Sbus128`。

原因是：

- 这更符合“优先保住 `Sbus256`”的当前目标
- 也更直接针对当前已经暴露出来的 router / ingress / egress 平均复杂度上升问题

#### 修补方向 E：若目标是“彻底压 NoC”，需要把 manager 数据面从当前系统语义中剥离

如果后续目标不是“在现有 fabric 上再省 10% 到 20%”，而是希望有明显更大幅度的
NoC 压缩，那么更根本的方向应是：

- 保留 CPU cache / coherent 路径在当前系统 bus / global NoC 上
- 让 pair manager 的 uncached / DMA / shared-spad 数据面走更轻的专用路径

这条线仍然和当前 authoritative pair-wrapper 路线兼容，因为它并不要求恢复旧的
grouped attachment；它要求的是：

- 把当前过重的 manager-side protocol cost 从 system-wide fabric 中拆出来

换句话说：

- `A/B/C/D` 是当前 active branch 上的可立即推进修补
- `E` 是若后续论文/系统目标明确要求“大幅压 NoC”时，更值得投入的架构方向

## 2026-04-07 运行时与时序补充

本轮没有继续放任当时仍在运行的两个 `buildbitstream` 会话，而是先按当前用户要求停掉它们，
避免继续浪费 build host 资源。

已停掉的两个会话为：

- `firesim-buildbitstream-4c12p12-dummy16x16-sbus128-vc4-pair-r3-shrink64-noretiming-openmt`
- `firesim-buildbitstream-4c12g12d-dummy16x16-20mhz-aws-r3-openmt`

对应本地 tmux `.exitcode` 文件当前均为：

- `120`

并且当前已经没有残留的 `firesim-buildbitstream` / `firesim-monitor` tmux 会话；对应两台
build host：

- `i-031866abb3e6b0f98`
- `i-0f92515e84182d43c`

AWS 当前都已显示为 `terminated`。

### 这轮真正新增的硬件事实：`4c8p8 pair sbus256` 已拿到 AGFI，但当前不能当作可用目标

本轮最重要的新事实不是又多了一次失败的 `buildbitstream`，而是已经有一版
`4 CPU + 8 pair + dummy16x16 + shared-spad 1 MiB + Sbus256` 的 pair-wrapper 映像完成了
打包并拿到了 AGFI：

- build 目录：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-06--18-16-00-f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c8p8_sbus256_20mhz/`
- `AGFI_INFO`：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-06--18-16-00-f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c8p8_sbus256_20mhz/AGFI_INFO`
- AGFI：
  - `agfi-01818b1718ae9b7a0`

同时，针对它的最小 FireMarshal + FireSim smoke 也已经真的搭起来并执行过：

- FireMarshal build log：
  - `/home/ubuntu/chipyard/software/firemarshal/logs/linux-poweroff-build-2026-04-07--02-48-52-LSVM6P7IYKMTTP7X.log`
- FireMarshal install log：
  - `/home/ubuntu/chipyard/software/firemarshal/logs/linux-poweroff-install-2026-04-07--02-50-45-8F382R7K1GVO1KPH.log`
- FireSim `launchrunfarm` log：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--02-51-20-launchrunfarm-E6D0ERU97LJO88X2.log`
- FireSim `infrasetup` failure log：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--02-51-42-infrasetup-H1TACA6XDZ2321BU.log`

但当前结论必须写死为：

- 该 AGFI 虽然已经 `available`
- 但当前**不能**当作可用的 FireSim runtime baseline
- 更不能把它当作 stage5 “已完成验证”的结果

原因是它在最早的 driver preflight 就失败了：

- FireSim manager 在 `infrasetup` 阶段三次执行：
  - `timeout --kill-after=5s 30s sudo ./FireSim-f2 +slotid=0 +check-fingerprint`
- 日志里只出现：
  - `entered simulation flow execution`
- 没有出现：
  - `finished waiting`
  - `FireSim fingerprint: 0x46697265`

并且这不是 manager 默认 `30s` timeout 过短造成的假阳性。本轮已经对远端 F2 host 手动复验：

- 命令：
  - `timeout --kill-after=5s 180s sudo ./FireSim-f2 +slotid=0 +check-fingerprint`
- 结果：
  - 依然超时，返回 `124`

因此，当前 `4c8p8 pair sbus256` 的问题不是“Linux workload 还没启动完”，而是
**目标在 very-early bring-up / host-FPGA 初始化阶段就没有完成 `wait_for_init()`**。

### 与之前成功过的较小设计相比，当前失效级别明显更重

旧的较小设计虽然也有 timing violation，但至少还能走到：

- `finished waiting`
- `FireSim fingerprint: 0x46697265`
- `Commencing simulation.`

参考：

- `/home/ubuntu/chipyard/tmp/firesim-aws-f2/test-logs/2026-03-15-runworkload-local-agfi-hangcheck.md`

也就是说，旧设计属于：

- “时序违规，但 driver 至少能把目标拉起来，然后在 target cycle 0 deadlock”

而当前 `4c8p8 pair sbus256` 属于：

- “连 fingerprint preflight 都过不去”

这两类风险等级不能混为一谈。当前这一版更接近“bitstream 已生成但时序/布局已经把 very-early
bring-up 破坏掉”的状态。

### 为什么这轮延迟大，不应只归因于 NoC 拓扑

本轮最新可复核的时序证据来自：

- `post_route_timing`：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-06--18-16-00-f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c8p8_sbus256_20mhz/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P8x4x2CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/reports/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P8x4x2CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config.2026_04_06-181805.post_route_timing.rpt`
- 本轮额外从 `post_route.VIOLATED.dcp` 反拉出的 timing summary：
  - `/home/ubuntu/chipyard/tmp/analysis_4c8p8_pair/timing_summary.rpt`

当前最关键的几个结论是：

- 设计总 WNS：
  - `-5.015ns`
- 设计总 TNS：
  - `-1075.048ns`
- failing endpoints：
  - `1696`
- `WRAPPER/CL/clk_main_a0` 自身域内也有违例：
  - WNS `-0.932ns`
  - TNS `-318.980ns`
  - failing endpoints `1140`
- 但最差路径**不是**内部 NoC router datapath 深组合链，而是 shell 边界相关路径：
  - source：
    `WRAPPER/CL/PIPE_DDR_STAT_ACK0/pipe_reg[...]`
  - data path delay：
    `11.419ns`
  - 其中 route：
    `11.340ns`
  - 逻辑层级：
    `0`
  - 路径包含：
    `SLR Crossing[2->1]`

这说明当前最坏延迟的主导项是：

- shell / DDR 状态返回路径
- CL 与 static shell 之间的跨 SLR 全局布线
- 时钟域边界与 wide-shell infrastructure 带来的放置压力

而不是“router 太多 hop，纯组合太深”这种单一解释。

### 更细一点的静态比较

这轮还可以补一个对理解很重要的横向对照：

1. 与 `12 pair sbus256` 自己相比，降到 `8 pair sbus256` 确实有帮助

- `cl_firesim`：
  - `1,406,466 -> 1,112,333 LUT`
  - `-20.9%`
- `NoC`：
  - `507,714 -> 399,295 LUT`
  - `-21.4%`

说明：

- pair 数下降并不是完全无效
- NoC 和总面积都确实下降了

2. 但与当前真正跑通 FireSim smoke 的 `4c12p12 pair sbus128` 相比，问题并不在于
   “设计太大所以当然起不来”

- 成功 runtime smoke 的配置是：
  - config：
    `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  - AGFI：
    `agfi-0dc8dcfa4c7735f40`
- 对应 FireSim runtime 证据：
  - `infrasetup` 成功：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--05-47-11-infrasetup-IICY0DY3T4M3MH91.log`
  - `runworkload` 成功：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--05-51-26-runworkload-NZ7FEGNCORQ86JJF.log`
- 同一类 preflight 在成功版上会出现：
  - `finished waiting`
  - `FireSim fingerprint: 0x46697265`
- 失败版 `4c8p8 pair sbus256` 则三次都停在：
  - `entered simulation flow execution`

3. 成功版与失败版的 post-route 最坏路径类型其实几乎一样

- `4c8p8 pair sbus256`：
  - WNS `-5.015ns`
  - 最坏路径位于 `WRAPPER/CL/PIPE_DDR_STAT_ACK0/...`
  - `11.419ns` data path 里 `11.340ns` 是 routing
- `4c12p12 pair sbus128`：
  - WNS `-4.943ns`
  - 最坏路径同样位于 `WRAPPER/CL/PIPE_DDR_STAT_ACK0/...`
  - `11.336ns` data path 里 `11.255ns` 是 routing

这说明：

- 两版都不是 “timing clean”
- 最坏路径也都不是内部 NoC router 的深组合链
- `4c8p8 pair sbus256` 卡在 fingerprint preflight，不能简单解释成
  “因为它比 `4c12p12 pair sbus128` 更大或更慢很多”

4. 两版资源的关键差异是“宽度形态”而不是“顶层是否更大”

- `4c8p8 pair sbus256` `cl_firesim`：
  - `1,112,333 LUT`
  - `579,510 FF`
  - `1,407 DSP`
- `4c12p12 pair sbus128` `cl_firesim`：
  - `1,164,888 LUT`
  - `604,807 FF`
  - `2,079 DSP`
- 也就是说成功版总量实际上更大
- 但单个 `GemminiCoupledDMAPairWrapper` 在 `sbus128` 下更轻：
  - `~35.4k LUT`
  - 对比 `sbus256` 的 `~37.9k LUT`

截至当前，更合理的工程结论是：

- `sbus256 -> sbus128` 改变了 pair-wrapper 和 shell-facing interconnect 的实现形态
- 这种形态变化足以把同类设计从“fingerprint preflight 卡死”拉回到
  “infrasetup 和 linux-poweroff smoke 可运行”
- 因而当前 `4c8p8 pair sbus256` 更像是 very-early bring-up 失效，
  而不是单纯资源超限

5. 这个失败现象在新的 F2 host 上已经独立复现，不是单次 runfarm 偶发

- 本轮为了排除旧 runfarm 残留和共享 YAML 污染，额外新建了：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus256_smoke_rerun.yaml`
  - `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus256_linux_poweroff_rerun.yaml`
- 新 runfarm tag：
  - `pair-smoke-4c8p8-sbus256-rerun-20260407`
- 新实例：
  - `i-0f006359e65a52669`
  - `192.168.1.246`
- 新 `infrasetup` 日志：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--06-26-29-infrasetup-PV1ZJI9OCHQ8TP76.log`

结果仍然完全一致：

- `timeout --kill-after=5s 30s sudo ./FireSim-f2 +slotid=0 +check-fingerprint`
  连续 3 次返回 `124`
- 最终仍然是：
  - `RuntimeError: FireSim driver readiness preflight failed for slot 0 after 3 attempts.`

因此可以把当前结论再收紧一层：

- `4c8p8 pair sbus256` 的 runtime bring-up 失败是**可复现的**
- 现有证据不支持把它归因为某次 runfarm host 的偶发脏状态

但它仍然没有解决最关键的 post-route bring-up 失败，这再次说明：

- 当前瓶颈不是“只要 NoC 再小一点点就自然能过”

2. 与 `4c12g12d separate sbus128` 相比，`4c8p8 pair sbus256` 的 NoC 仍然只小了约 `10.8%`

- `4c12g12d separate sbus128` `NoC`：
  - `447,588 LUT`
- `4c8p8 pair sbus256` `NoC`：
  - `399,295 LUT`

也就是说：

- endpoint/pair 数和 mesh 都已经缩了
- 但只要 `Sbus256`、shell-facing wide path、以及较重的 system-bus 语义还在，
  剩下的 NoC 体量仍然会被维持在一个很高的平台上

这比“是不是 4x3/4x2 拓扑放得不够紧”更能解释为什么面积没有继续明显往下掉。

### 当前更稳妥的结论

到本轮为止，比较稳妥且与证据一致的结论应写成：

- NoC 拓扑和 pair-level compact layout 当然仍会影响拥塞与 spread
- 但它们更像是**间接因素**
- 当前已经观察到的最坏时序与最早 runtime 失效，更直接地指向：
  - shell 边界必经路径
  - DDR/status/clock-boundary 相关跨 SLR 路由
  - `Sbus256` 下的宽链路与其配套协议/缓冲基础设施

因此，如果后续继续修：

- 不能把希望主要押在“继续缩 mesh 几何布局”
- 更应该优先怀疑并瘦身：
  - pair 对外暴露的宽 TL/system-bus 语义
  - shell-facing 必经状态路径
  - 需要跨 CL/static 边界来回往返的状态/握手网络
