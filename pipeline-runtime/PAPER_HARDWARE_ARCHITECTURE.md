# 基于 Chipyard + Gemmini + ReRoCC + FireSim 的原型硬件系统架构

## 1. 文档边界

本文档用于整理当前原型系统的硬件实现事实，面向后续论文写作。本文档**以当前仓库源码为唯一主参照**，不以历史协作文档、阶段性 README 或口头约定作为最终证据。

当前代码直接能确认的硬件增量主要落在以下几个子树：

- `generators/chipyard/src/main/scala/config/`
- `generators/chipyard/src/main/scala/config/fragments/`
- `generators/gemmini/src/main/scala/gemmini/`
- `generators/rerocc/src/main/scala/`
- `generators/gemmini/software/gemmini-rocc-tests/include/`

如果后续论文需要写“相对原始 Chipyard/Gemmini 的改动”，建议以本文档中的“代码可直接识别的增量模块”为准，而不是以历史记忆回溯。

## 2. 代码事实来源

本文档主要依据以下源码整理：

- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigs.scala`
- `generators/chipyard/src/main/scala/config/fragments/ReRoCCLooseCoupledFragments.scala`
- `generators/chipyard/src/main/scala/config/fragments/ReRoCCCoupledDMAManagerFragments.scala`
- `generators/rerocc/src/main/scala/Configs.scala`
- `generators/rerocc/src/main/scala/client/Client.scala`
- `generators/gemmini/src/main/scala/gemmini/GemminiISA.scala`
- `generators/gemmini/src/main/scala/gemmini/SharedScratchpad.scala`
- `generators/gemmini/src/main/scala/gemmini/SharedSpadXlate.scala`
- `generators/gemmini/src/main/scala/gemmini/CoupledSharedSpadRegistry.scala`
- `generators/gemmini/src/main/scala/gemmini/SpmPageTableWalker.scala`
- `generators/gemmini/src/main/scala/gemmini/Controller.scala`
- `generators/gemmini/src/main/scala/gemmini/FrontendTLB.scala`
- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`
- `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_gemmini_spm_xlate.h`
- `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_coupleddma.h`

## 3. 系统基线与实例化方式

### 3.1 系统基线

当前原型系统并不是从零构造的新 SoC，而是在现有 Chipyard 生态上组合并扩展如下基线能力：

- Rocket Chip 提供 Tile、RoCC、PTW、TileLink、CSR 等基础设施。
- Chipyard 提供 SoC 配置组合、NoC 和 FireSim 集成框架。
- Gemmini 提供矩阵/卷积加速器主体。
- ReRoCC 提供“client -> manager”的远程 RoCC 资源管理与转发机制。
- FireSim 提供最终的 FPGA 执行环境。

从当前源码能直接看到的结论是：本系统的核心增量并不体现在大规模改写 Rocket Chip CPU 流水线，而是体现在 **Gemmini 本地存储体系、ReRoCC 管理路径、Coupled DMA 以及软件可编程的 shared-spad 地址翻译链路**。

### 3.2 SoC 级参数化配置

主配置入口是 `GemminiLearningConfigSpadReRoCCNoCCoupledDMAParametric`。它统一参数化以下资源：

- CPU core 数量与二维布局
- Gemmini manager 数量与二维布局
- DMA manager 数量与二维布局
- system bus 宽度
- memory channel 数量
- 频率
- meshRows / meshColumns
- 是否启用 global NoC
- 是否启用 deterministic global routing
- 是否使用 dummy Gemmini
- DMA manager 可见性过滤和 `sbusSlave -> STL` 连接策略

这说明当前系统不是“单个 Gemmini + 单个 DMA”的固定原型，而是面向多 manager 扩展设计的参数化 SoC。

### 3.3 ReRoCC 与 BuildRoCC 实例化方式

当前配置链条中最关键的硬件拼装顺序为：

1. `new rerocc.WithReRoCCNoC(reroccNoCParams)`
2. `new rerocc.WithReRoCC(reRoCCManagerParams = ...)`
3. `new chipyard.config.WithReRoCCCoupledDMAManagers(...)`
4. `gemminiManagerConfig`

其中：

- `WithReRoCC` 在 `BuildRoCC` 中插入一个 `ReRoCCClient`。
- `WithReRoCCGemminiManagers` 在 `BuildRoCC` 中批量实例化多个 `Gemmini` manager，且统一使用 `OpcodeSet.custom3`。
- `WithReRoCCCoupledDMAManagers` 在 `BuildRoCC` 中批量实例化多个 `GemminiCoupledDMA` manager，且统一使用 `OpcodeSet.custom2`。

因此，当前系统在硬件级已经把“计算 manager”和“搬运 manager”显式拆成两类 RoCC 设备：

- `custom3` 负责 Gemmini 计算与 shared-spad xlate 控制指令
- `custom2` 负责 Coupled DMA 数据搬运

### 3.4 资源映射与 NoC 拓扑

`GemminiLearningReRoCCCoupledDMAConfigs.scala` 中不仅实例化了 manager，还显式构造了：

- `cpuNodes`
- `gemminiNodes`
- `dmaNodes`
- `managerNodes`
- `sbusNodeMapping`
- `reroccTileClientMapping`
- `reroccManagerMapping`

这意味着当前系统的任务编排不仅依赖“有多少 manager”，还依赖“这些 manager 在 NoC 上如何编号与布置”。尤其重要的是：

- ReRoCC tile client mapping 以 CPU tile 为起点
- manager mapping 把 Gemmini 与 DMA 统一纳入同一个 manager 编号空间
- Gemmini 与 DMA 采用相同的局部索引习惯，便于软件按 stage 对齐一对计算/搬运 manager

从论文写作角度，可以把这一层理解为：**任务编排在软件层操作的是逻辑加速器集合，而底层硬件已经提供了可被 ReRoCC 映射和分配的物理 manager 空间**。

## 4. ReRoCC 管理平面与指令路由

### 4.1 ReRoCC Client 的角色

`ReRoCCClient` 是当前系统中 CPU 侧的控制入口。它不是一个具体的计算单元，而是负责：

- 维护 cfg 上下文
- 管理 opcode 到 cfg 的绑定
- 向远端 manager 发送指令 beat
- 处理 acquire / release / status / ptbr 更新等控制消息

`ReRoCCClientParams` 当前默认支持：

- `nCfgs = 16`
- `4` 个 opcode remap CSR

这意味着硬件控制面为多 manager 调度预留了比“单一 Gemmini”更宽的上下文空间。

### 4.2 InstructionSender 的数据发送方式

`InstructionSender` 将一条 RoCC 指令拆成三段发送：

- instruction word
- `rs1`
- `rs2`

状态机为：

- `s_inst`
- `s_rs1`
- `s_rs2`

因此，ReRoCC 并不是把 RoCC 命令整包透明转发，而是把它变成一个协议化消息流。这个细节很重要，因为后续所有 Gemmini 指令、shared-spad xlate 指令和 DMA 指令都经过这条发送路径。

### 4.3 cfg 与 opcode 的绑定语义

`ReRoCCClient` 内部维护：

- `csr_opc[4]`
- `csr_cfg[16]`
- `cfg_credits`

这说明：

- 软件真正驱动的不是“直接给某个 Gemmini 发指令”
- 而是“先 acquire 某个 cfg，把 cfg 绑定到某个 manager，再把 opcode 重定向到该 cfg”

当前软件路径中，Gemmini 使用 `custom3`，DMA 使用 `custom2`。因此就当前稳定软件栈而言，单 hart 的活跃控制通路更接近：

- 1 条 Gemmini 指令路由 lane
- 1 条 DMA 指令路由 lane

这也解释了为什么论文里要把“多 manager 硬件存在”和“多指令流真正并发发射”区分开写。

### 4.4 代码中已经修复的一个协议缺陷

当前 `Client.scala` 已修复 `InstructionSender` 在 `s_inst` 状态下对 `xs2` 的判断错误。这个修补虽然不是 shared-spad 主功能本身，但它体现出：

- ReRoCC 指令打包路径已经被实机使用和审计
- 系统并不是停留在抽象设计，而是已经进入协议细节修补阶段

## 5. Shared Scratchpad 的硬件结构

### 5.1 SharedScratchpadConfig 的核心参数

`SharedScratchpadConfig` 定义了 shared-spad 的核心硬件属性：

- `enable`
- `global_base_addr`
- `local_size_bytes`
- `local_banks`
- `local_bank_interleaved_bytes`
- `local_bank_beat_bytes`
- `use_page_table_xlate`
- `share_xlate_with_coupled_dma`

在当前原型配置中，这些参数被设置为：

- `enable = true`
- `global_base_addr = 0x40000000`
- `local_size_bytes = 1 MiB`
- `local_banks = 1`
- `local_bank_interleaved_bytes = max(gemminiBeatBytes, 64)`
- `local_bank_beat_bytes = gemminiBeatBytes`
- `use_page_table_xlate = true`
- `share_xlate_with_coupled_dma = true`

这说明当前系统不是把 shared-spad 当作固定地址的旁路存储，而是把它设计成：

- 有全局地址空间入口
- 有每个 Gemmini 的本地存储片段
- 可以通过软件配置打开页表翻译
- 可以与 coupled DMA 共享地址翻译上下文

### 5.2 地址组织方式

`SharedScratchpadConfig` 中定义了以下地址函数：

- `local_base_addr(spad_id)`
- `local_bank_base_addr(spad_id, bank_id)`
- `local_bank_addr_sets(spad_id)`

因此，shared-spad 的地址空间组织方式是：

- 先由 `global_base_addr` 定义整个 shared-spad 全局窗口
- 再按 `spad_id` 划分每个 Gemmini 的 local segment
- 再按 `bank_id` 划分 bank 地址集合

当前配置虽然 `local_banks = 1`，但代码结构已经按多 bank 组织，意味着该设计本身是可扩展的。

### 5.3 TileLink 连接拓扑

`SharedScratchpad` 是一个独立的 LazyModule，其内部有三层关键节点：

- `global_node`
- `local_node`
- `bank_xbar`

其语义分别是：

- `global_node`：对总线暴露 shared-spad 的全局可寻址入口
- `local_node`：接收本地访问者，包括 Gemmini 自己的 spad DMA 路径和 coupled DMA
- `bank_xbar`：把全局访问和本地访问统一路由到 bank 级存储体

bank 级存储体当前通过以下方式实例化：

- 真 Gemmini 使用 `TLRAM`
- dummy Gemmini 使用 `TLZero`

因此，当前 shared-spad 不是单纯的软件概念，而是真实挂在 TileLink 上的可寻址片上存储模块。

## 6. 软件可配置的 shared-spad 页表翻译机制

### 6.1 新增 ISA 接口

`GemminiISA.scala` 在现有 Gemmini funct 编码之外新增了四个 shared-spad xlate 控制指令：

- `SPM_XLATE_CFG = 23`
- `SPM_XLATE_RANGE = 24`
- `SPM_XLATE_FLUSH = 25`
- `SPM_XLATE_FAULT = 26`

软件侧对应 helper 已在 `rerocc_gemmini_spm_xlate.h` 中暴露为：

- `rerocc_gemmini_spm_xlate_cfg()`
- `rerocc_gemmini_spm_xlate_range()`
- `rerocc_gemmini_spm_xlate_flush()`
- `rerocc_gemmini_spm_xlate_fault()`

这组接口是 shared-spad 软件可编程化的关键入口。

### 6.2 Gemmini Controller 中维护的 xlate 状态

`Controller.scala` 内部显式维护了一组 shared-spad xlate 寄存器：

- `spm_xlate_enable`
- `spm_xlate_page_shift`
- `spm_xlate_pte_count`
- `spm_xlate_ptbr`
- `spm_xlate_range_base`
- `spm_xlate_range_size`
- `spm_xlate_fault_vaddr`
- `spm_xlate_fault_cause`
- `spm_xlate_fault_valid`
- `spm_xlate_cache_epoch`

这一组状态说明当前硬件实现不是“固定公式地址映射”，而是标准的“软件下发 PTBR + 页数 + 页粒度 + 地址范围”的可配置翻译上下文。

### 6.3 FrontendTLB 中的翻译语义

`FrontendTLB.scala` 将 shared-spad 翻译并入 Gemmini 前端访存路径。其逻辑可概括为：

1. 判断当前请求地址是否命中 `spm_xlate_range_base ~ range_end`
2. 对命中地址计算：
   - `spmOffset = vaddr - range_base`
   - `spmVpn = spmOffset >> pageShift`
   - `spmPageOffset = spmOffset - (spmVpn << pageShift)`
3. 按模式选择三类路径：
   - `direct hit`
   - `passthrough hit`
   - `PTW hit`

三类路径的含义分别是：

- `direct hit`
  不走 PTW，直接用 `shared_base + spmOffset` 形成物理地址
- `passthrough hit`
  命中 shared-spad range，但翻译关闭，直接把原始地址透传
- `PTW hit`
  通过 `PTBR + VPN * 8` 读取 64-bit PTE，再拼出最终物理地址

需要把“RTL 兼容分支”和“当前原型配置的实际运行模式”区分开写：

- 从 `FrontendTLB.scala` 的实现看，硬件逻辑覆盖了“direct remap / passthrough / PTW”三条分支
- 但在当前原型配置中，`GemminiLearningReRoCCCoupledDMAConfigs.scala` 已把 `use_page_table_xlate` 固定为 `true`
- `Controller.scala` 又把该配置直接接到 `tlb.io.spm_use_ptw`

因此，对**当前正在使用的这套实例化系统**而言：

- `direct hit` 更应理解为 RTL 中为“不开 PTW 的 shared-spad 直重定位模式”保留的兼容分支
- 实际运行时会进入的是：
  - `passthrough hit`：命中 shared-spad alias range，但软件暂时关闭 xlate
  - `PTW hit`：命中 shared-spad alias range，且软件开启 xlate 后经 PTE 翻译得到物理地址

论文里如果写“当前系统支持的运行状态”，建议按后两类写；如果写“硬件逻辑完整覆盖的分支”，再单独提 `direct hit`。

### 6.4 PTW 的实现方式

`SpmPageTableWalker.scala` 是一个独立 TileLink client。它的工作非常明确：

- 请求内容是 `{ vpn, vaddr, ptbr, pageShift }`
- 访问地址是 `ptbr + (vpn << 3)`
- 每个 PTE 为 `64 bit`
- 返回结果包括：
  - `pte`
  - `accessFault`
  - `vpn`
  - `vaddr`
  - `pageShift`

这说明软件提交的 PTE backing 本质上是一块普通内存，Gemmini 在运行时通过 TL get 去读表项，而不是把页表硬编码在控制器内部。

### 6.5 fault 与 cache epoch

shared-spad xlate 还具备两类辅助机制：

- `FAULT`
  允许软件读取最近一次 shared-spad 翻译故障的地址和原因
- `cache_epoch`
  允许软件在更新 PTE 后通过 flush/epoch 让前端 TLB 失效

这两个机制非常适合在论文中表述为：

- 软件可观测的地址翻译故障反馈
- 软件可驱动的翻译缓存一致性维护

## 7. Coupled DMA 的结构与 shared-spad 协同

### 7.1 Coupled DMA 的实例化语义

`WithReRoCCCoupledDMAManagers` 为每个 DMA manager 实例化一个 `GemminiCoupledDMA`，且显式使用：

- `OpcodeSet.custom2`
- `gemmini_id = gemminiIdBase + i`
- `shared_scratchpad_config = sharedScratchpadConfig`

这意味着 coupled DMA 不是独立于 Gemmini 的匿名搬运器，而是**与某个 Gemmini 的 shared-spad 局部地址空间一一对应的搬运 manager**。

### 7.2 数据面连接方式

`GemminiCoupledDMA.scala` 中的连接方式分成两部分：

- 对 shared-spad 本地 bank 地址的访问，经 `CoupledSharedSpadRegistry.connectClient()` 直接接入 Gemmini 对应的 `sharedSpad.local_node`
- 对非 shared-spad 地址的访问，经 `masterNode := TLFilter.mSubtract(localBankAddrSets) := dmaXbar` 走常规外部内存路径

因此，coupled DMA 同时具备：

- shared-spad 本地直连能力
- 外部 DRAM 访问能力

这正是“coupled”二字在当前实现中的硬件含义。

### 7.3 xlate sideband 共享

当以下两个条件同时满足时：

- `shared_scratchpad_config.enable`
- `shared_scratchpad_config.share_xlate_with_coupled_dma`

Gemmini 会导出 `SharedSpadXlateConfig` sideband，Coupled DMA 会通过 `BundleBridgeSink` 接收该 sideband。

sideband 中包含：

- `use_ptw`
- `enable`
- `page_shift`
- `pte_count`
- `ptbr`
- `range_base`
- `range_size`
- `shared_base`
- `cache_epoch`

这说明当前 coupled DMA 与 Gemmini 共享的不是“某个单独寄存器值”，而是一整套 shared-spad 地址翻译上下文。

### 7.4 Coupled DMA 自身的翻译与缓存逻辑

`GemminiCoupledDMAImp` 内部还实现了：

- 自己的 `SpmPageTableWalker`
- 自己的 `spmTlbValid / spmTlbVpn / spmTlbPaddrBase`
- `spmPtwPendingValid`
- `cache_epoch` 驱动的 TLB 失效

因此，coupled DMA 并不是简单把地址交给 Gemmini 代查，而是拥有独立的 shared-spad 翻译缓存和 PTW 访问逻辑。共享的是翻译上下文，不是共享一次查表结果。

### 7.5 Coupled DMA 命令接口

当前软件头文件中已经暴露了以下 coupled DMA 指令：

- `rerocc_coupleddma_set_dst(dst_addr, completion_addr)`
- `rerocc_coupleddma_set_src(src_addr, num_bytes)`
- `rerocc_coupleddma_wait()`
- `rerocc_coupleddma_read_monitor(stat_id)`

这说明 coupled DMA 的控制方式是：

1. 先写目标地址和 completion 地址
2. 再写源地址和传输长度
3. 硬件启动 copy request
4. 软件轮询完成状态或读取 monitor

从实现上看，它是一个面向“软件显式 orchestrate”的 copy engine，而不是全自动 DMA 子系统。

## 8. 软件可见的硬件编程模型

综合以上实现，当前硬件最终向软件暴露出一个非常明确的编程模型：

### 8.1 计算面

- 通过 `custom3` 驱动 Gemmini
- 通过 ReRoCC cfg/opcode 绑定把 Gemmini 指令路由到特定 manager

### 8.2 搬运面

- 通过 `custom2` 驱动 Coupled DMA
- 可以在 DRAM 与 shared-spad 之间搬运数据
- 可以在 shared-spad 之间执行本地 copy

### 8.3 地址面

- shared-spad 具有全局地址窗口
- 软件可以给某个 action 配置独立的 alias range 和 PTBR/PTE
- Gemmini 与 Coupled DMA 在同一 alias range 语义下工作

### 8.4 观测与控制面

- 可以 flush shared-spad xlate cache
- 可以读取 xlate fault
- 可以读取 DMA monitor
- 可以通过 ReRoCC acquire / fence / release 管理 manager 生命周期

从论文表述角度，可以把它总结成：

> 当前原型硬件已经把 Gemmini 的本地 scratchpad 提升为一个可由软件编排、可按页映射、可由计算与搬运共享访问语义的片上共享存储空间。

## 9. 代码可直接识别的硬件增量

如果要写“相对已有 Chipyard 生态的额外改动”，当前代码最明确的增量点可以组织为以下五项：

1. 在 Chipyard 配置层增加了 ReRoCC + 多 Gemmini manager + 多 Coupled DMA manager 的统一实例化与 NoC 映射。
2. 在 Gemmini 中引入了 `SharedScratchpad`，把每个加速器的本地存储扩展成具有全局地址空间和本地入口的共享存储模块。
3. 在 Gemmini 前端新增了软件可配置的 shared-spad 页表翻译链路，包括 `CFG/RANGE/FLUSH/FAULT` 指令、FrontendTLB 扩展和专用 PTW。
4. 在 Coupled DMA 中新增了与 shared-spad 本地入口直接相连的数据面，并通过 sideband 共享 Gemmini 的地址翻译上下文。
5. 在 ReRoCC 控制链路上形成了可支持多 manager 分配、opcode 重绑定和 scoped 控制的硬件管理平面。

需要特别注意的是：从当前仓库能直接确认的大部分硬件增量集中在 `chipyard config / gemmini / rerocc`，并没有看到需要单独作为主叙事展开的大规模 Rocket Chip CPU 私有 RTL 分叉。因此论文中若要提及 Rocket Chip，建议写成“复用了 Rocket Chip 的 LazyRoCC、PTW、TileLink 和 Tile 基础设施”，而不是把 Rocket Chip 本身描述成主要改写对象。

## 10. 可直接转写到论文的价值点

以下表述可以直接作为论文草稿的素材：

- 该原型系统并非简单堆叠 Gemmini 和 DMA，而是在 Chipyard/ReRoCC 框架下建立了统一的 manager 资源空间，使编排器能够在软件层按 segment 和 stage 进行物理资源分配。
- 通过 shared scratchpad 扩展，原本局限于单加速器本地可见的 scratchpad 被提升为可全局寻址、可软件映射的共享片上存储。
- 通过软件可配置页表机制，shared-spad 的地址空间不再是固定物理地址窗口，而是可由运行时按 action 动态安装的逻辑别名窗口。
- 通过 xlate sideband 共享，Coupled DMA 与 Gemmini 获得了一致的 shared-spad 地址语义，从而使“搬运”和“计算”能够围绕同一片片上数据视图协同工作。
- 整个硬件路径保持了增量式修改风格：复用现有 RoCC/PTW/TileLink 基础设施，只在必要位置增加 shared-spad、xlate 和 coupled DMA 扩展，降低了原型系统的实现侵入性。

## 11. 2026-04-04 真实 Pre-NoC 聚合改动记录

先说明一个状态更新：

- 本章 `11.1` 到后续 `2026-04-04 ~ 2026-04-05` 的记录，描述的是一条后来已被放弃的 pre-NoC grouped-attachment 实验路线
- 截至 `2026-04-05`，该路线的活动代码已经从当前工作树回退
- 因此，本章现在应视为**归档材料**，主要保留思路、问题和失败经验
- 当前后续方向以：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pair_wrapper_manager_plan_20260405.md`
  为准

本次改动的目标不是“把 Gemmini 和 Coupled DMA 放到相邻或同一物理坐标”，而是更进一步在 **NoC attachment 生成之前** 就把它们聚合成一个对外端口组，从而真实减少 NoC 拓扑中的外部节点和链路数量。

### 11.1 改动前后的关键区别

改动前：

- `Gemmini i` 与 `Coupled DMA i` 即使被映射到相同 physical node id，也仍然各自生成独立的 TL attachment 和独立的 ReRoCC manager edge
- 因而在 Constellation/GlobalNoC 看来，它们仍是两个独立的外部端口，只是坐标重合

改动后：

- 在 `generators/rerocc/src/main/scala/Integration.scala` 中，先按 manager group 聚合，再把聚合后的组接到 SBUS / GlobalNoC / ReRoCCNoC
- 对 TL master 侧，使用 group 级 `TLXbar`
- 对 STL / sbus-slave 侧，使用 group 级 `TLXbar`
- 对 ReRoCC manager 侧，使用 group 级 `ReRoCCXbar`

因此，Gemmini 与其对应 Coupled DMA 现在会：

- 共用一个 TL master attachment
- 共用一个 STL attachment
- 共用一个 ReRoCC outward edge
- 在开启 `connectSbusSlaveToStl` 时，共用一个 sbus-slave attachment

这才是“真正只占一个拓扑节点/端口”的实现方式。

### 11.2 12G12D 目标配置上的直接效果

对于 `12 Gemmini + 12 Coupled DMA`：

- 改动前：对外存在 24 组 manager attachment / manager edge
- 改动后：按 `(0,12) (1,13) ... (11,23)` 成对聚合，只剩 12 组

因此，NoC 规模缩减来自两部分：

1. 外部 attachment 数量下降
2. manager 物理布局从 24 个 manager 坐标压缩为 12 个 group 坐标

这也是本次优化希望降低 LUT / routing 压力的主要来源。

### 11.3 对软件编程模型的不变性

本次改动**不改变软件可见的 manager id / opcode 语义**：

- Gemmini 仍使用 `custom3`
- Coupled DMA 仍使用 `custom2`
- `GEMMINI_BASE_ID = 0`
- `DMA_BASE_ID = numGemmini`
- manager id 顺序仍是先 Gemmini，再 Coupled DMA

因此：

- 现有 baremetal / pipeline runtime 的 manager 编号语义不需要因为本次聚合而改写
- 软件仍然把 Gemmini 和 DMA 当作两个独立 manager 控制
- 改变的是 NoC 入口拓扑，不是软件的控制平面编号

### 11.4 与 shared-spad 本地直连的关系

本次改动**没有改变** `GemminiCoupledDMA.scala` 中对 shared scratchpad 本地 bank 的直连语义：

- 访问 shared-spad 本地 bank 地址时，DMA 仍通过 `CoupledSharedSpadRegistry.connectClient()` 进入对应 Gemmini 的 `sharedSpad.local_node`
- 访问非 local bank / 非 shared-spad 地址时，仍走常规外部内存路径

也就是说：

- “shared-spad 本地直连”依然保留
- 新增的是“Gemmini + DMA 在 NoC 外部 attachment 层面也做真实聚合”
- 这两者并不冲突，分别作用于本地数据面和片上互连拓扑层

### 11.5 相关代码位置

本次实现主要落在以下位置：

- `generators/rerocc/src/main/scala/AttachmentGrouping.scala`
- `generators/rerocc/src/main/scala/Integration.scala`
- `generators/rerocc/src/main/scala/bus/NoC.scala`
- `generators/constellation/src/main/scala/protocol/Protocol.scala`
- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigs.scala`
- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigHelpers.scala`
- `generators/chipyard/src/main/scala/harness/TestHarness.scala`

其中最关键的一点是：

> 只有在 `Integration.scala` 中先聚合 attachment，再交给 TLNoC / ReRoCCNoC / GlobalNoC，才能真正减少 NoC 看到的拓扑规模；仅仅共享 node id 不足以达到这个效果。

### 11.6 当前验证记录

截至本轮修改，已经确认了以下几点：

- 小配置 `WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`
  的 `make replace-rtl` 已经跑通。
- 该配置在日志中已经明确看到真实聚合生效：
  - TLNoC / GlobalNoC 侧把 `(0, 2)` 聚到同一 attachment / node
  - ReRoCC GlobalNoC 侧把 `Managers: List(0, 2)`、`Managers: List(1, 3)` 聚到同一 outwards node
- 目标配置 `4C/12G/12D` 的 `Sbus16` 版本在 elaboration 阶段直接失败，错误为：
  - `requirement failed: rowBits(16) < coreDataBits(64)`
  - 这说明 `Sbus16` 对当前 Rocket L1D 配置不是一个合法参数组合，不是资源超限导致的失败
- 因此，后续目标配置验证改为优先使用 `Sbus128`

对 `4C/12G/12D Sbus128`，本轮已经确认：

- target elaboration 通过
- 聚合映射仍正确，日志中已看到：
  - `8 <- Managers: List(0, 12)`
  - `9 <- Managers: List(1, 13)`
  - ...
  - `19 <- Managers: List(11, 23)`
- Golden Gate 已继续执行，并已生成：
  - `FireSim-generated.sv`
  - `FireSim-f2`

### 11.7 最小 baremetal 回归策略

为了尽量不改软件栈、同时缩短仿真时间，本轮 baremetal 回归采用以下原则：

- 只选代表性负载，不跑完整 Linux / pipeline runtime
- 优先覆盖：
  - Gemmini + CoupledDMA 成对 manager 的基础控制路径
  - shared-spad 显式交错访问路径
  - Gemmini 与 DMA 的 nonblocking overlap 场景
- 尽量把 DMA 传输字节数压到最小有意义规模
- 尽量把长循环迭代数压到 `1`

本轮已实际生成以下快速回归负载：

- `rerocc-baremetal-tests-coupleddma/workload/host-init.sh`
  - 目标：基础 Gemmini/DMA 成对矩阵验证
  - 采用参数：`--matrix single --bytes 512`
- `rerocc-baremetal-tests-coupleddma/workload/host-init-explicit-interleaved.sh`
  - 目标：shared-spad 显式交错路径
- `rerocc-baremetal-tests-coupleddma/workload/host-init-nonblocking.sh`
  - 目标：Gemmini / DMA overlap
  - 采用参数：`--bytes 512 --long-conv-iters 1 --short-conv-iters 1 --long-resadd-iters 1 --long-dma-iters 1 --short-dma-iters 1`

并且已经完成两套参数化编译：

- `4C/12G/12D` 目标参数版本
- `2C/2G/2D` 小配置参数版本

说明：

- 上述 baremetal 负载只通过 `EXTRA_CFLAGS` 调整 manager 数量、base id、DMA bytes 与迭代数
- 没有改写 pipeline runtime 软件栈
- 没有改动软件可见的 manager id / opcode 语义

### 11.8 本地仿真入口的兼容性补丁

为了给 baremetal 回归提供一个更快的本地执行入口，本轮还检查了 `sims/verilator` 下的小配置软件仿真。

发现的问题是：

- Constellation 生成的 router debug print 条件在 Verilator 参数中会展开成
  `+define+PRINTF_COND=TestHarness.printf_cond`
- 但当前 `TestHarness` 模块中原本没有 `printf_cond`
- 这会导致本地 Verilator 在后端解析 `Router_*.sv` 时失败

因此做了一个**仅影响仿真兼容性、不影响功能路径**的小补丁：

- 在 `generators/chipyard/src/main/scala/harness/TestHarness.scala` 中补了一个
  `printf_cond` sink
- 该信号固定为 `false`
- 目的是让这些层级引用在软件仿真里能够解析通过，而不是改变 NoC / ReRoCC / Gemmini 行为

### 11.9 2026-04-04 运行时修复记录（进行中）

本轮继续做 small-config baremetal 回归时，又定位到两类新的实现问题。

#### 11.9.1 grouped attachment 不应裹挟 ReRoCC MiniDCache

原先的真实 pre-NoC 聚合虽然已经把 `Gemmini i` 和 `CoupledDMA i` 合并到了同一个 SBUS attachment，
但 `ReRoCCManagerTile` 里导出的 `tlNode` 实际上把以下几类 master 都混在了一起：

- `rocc.tlNode` 的外部内存访问
- `rocc.atlNode`
- `MiniDCache`

这会带来两个直接后果：

- TLGlobalNoC 侧只看到 `port_named_rerocc_group_*` 两个 ingress，看不到独立的 `ReRoCC i DCache`
- grouped attachment 因为卷入了 coherent DCache 流量，`TLSourceShrinker` / `NamedTLSourceShrinker` 在协议上变成非法

为此，本轮做了结构修复：

- 在 `ReRoCCTileParams` 中新增 `splitDCacheAndExternalTL`
- 在 `ReRoCCManagerTile` 中把：
  - 外部 RoCC TL master（`rocc.tlNode`）继续导出为 grouped attachment 候选
  - `MiniDCache/atlNode` 单独导出为 `cacheTLNode`
- 在 `Integration.scala` 中为 `cacheTLNode` 额外建立独立的 SBUS attachment
- 在 coupled-DMA 聚合配置里，仅对 `aggregatePairedManagers = true` 的配置启用该拆分

修复后，小配置 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`
的 TLGlobalNoC 日志已经变为：

- 独立出现：
  - `ReRoCC 0 DCache`
  - `ReRoCC 1 DCache`
  - `ReRoCC 2 DCache`
  - `ReRoCC 3 DCache`
- 同时仍保留：
  - `port_named_rerocc_group_0`
  - `port_named_rerocc_group_1`

这说明：

- Gemmini/DMA 的真正 pre-NoC 聚合还在
- DCache 已从 grouped attachment 中剥离
- TLGlobalNoC 的 ingress 结构终于与 `sbusInNodeMapping` 的配置意图一致

#### 11.9.2 small-config baremetal 已越过旧的 TL source-id 断言

在完成上面的结构修复后，重新生成并运行 small-config Verilator：

- 旧的 `TLDToNoC` / `IngressUnit` 相关 source-id 断言没有再出现
- baremetal 已能继续前进到 Gemmini 内部

这说明前面一直在排查的 “grouped attachment 返回 `D.source` 落不到 TLGlobalNoC ingress range” 这个主问题，至少在当前 small-config 路径上已经不再是首个阻塞点。

#### 11.9.3 新暴露的问题：DMACommandTracker 同拍 alloc/return 竞态

小配置 baremetal `rerocc_lc_matrix_baremetal_coupleddma-baremetal` 随后在
`StoreController` 的 `DMACommandTracker` 处失败：

- 断言位置：`DMACommandTracker.scala:88`
- 现象：`request_returned` 到达时，对应 `cmd_id` 在 tracker 中尚未标记为 valid

进一步检查后发现，这个 tracker 原实现没有处理以下合法情况：

- 某个 `cmd_id` 在同一个周期里既被 `alloc.fire`
- 又因为 scratchpad 本地路径很短，在同一个周期里收到 `request_returned`

原实现先用旧值检查 `cmds(cmd_id).valid`，因此会把“同拍刚分配、同拍就返回”的场景误报为非法。

为此，本轮已在 `DMACommandTracker.scala` 中补上同拍旁路逻辑：

- 若 `io.alloc.fire && next_empty_alloc === io.request_returned.bits.cmd_id`
  - 则把这次 alloc 视为本次 return 的有效来源
  - 用 `io.alloc.bits.bytes_to_read` 作为返回前的 `bytesLeftBeforeReturn`
- 随后再做：
  - `assert(cmdValidBeforeReturn)`
  - `assert(bytesLeftBeforeReturn >= io.request_returned.bits.bytes_read)`

新的生成 RTL 中已经能看到：

- `allocatesSameCmd`
- `bytesLeftBeforeReturn`
- `cmdValidBeforeReturn`

这些信号，说明该修复已经进入生成结果。

#### 11.9.4 当前状态

截至本文档追加时：

- 结构修复已完成并进入生成结果
- `DMACommandTracker` 竞态修复已写入源码并进入新生成 RTL
- small-config Verilator 正在做带该补丁的增量重编
- 下一步是重新运行：
  - `rerocc_lc_matrix_baremetal_coupleddma-baremetal`
  - 通过后再补跑 1 个额外快速 baremetal

### 11.10 2026-04-05 Small-Config 本地仿真调试整理版（供交接）

本节是当前 small-config Verilator 调试的整理稿，目标是把“已经做了什么、现在卡在哪里、下一个 AI
该怎么接”放在同一个位置，避免再分散到临时文件。

当前 authoritative 的交接信息以本节为准。

#### 11.10.1 当前调试范围

当前聚焦的配置与仿真器是：

- 配置：
  - `chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`
- 仿真器：
  - `/home/ubuntu/chipyard/sims/verilator/simulator-chipyard.harness-GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`

当前目标不是只让 `mt_hello-baremetal` 打印，而是要先修通 host 侧加载 / 启动链路，
随后再恢复 small-config 的快速 baremetal 回归。

#### 11.10.2 到目前为止已经完成的工作

本阶段已经完成的工作可以分成三类。

第一类，是 small-config 结构修复，已经在前一节落地：

- pre-NoC grouped attachment 已与 `MiniDCache` 拆分
- `Gemmini i` 与 `CoupledDMA i` 的 grouped attachment 仍然保留
- `DMACommandTracker` 的同拍 `alloc/return` 竞态已经修复

这意味着：

- 先前的 grouped attachment / TL source-id 主问题已经不是当前首个阻塞点
- 调试重点已经前移到 host 加载 / 启动路径

第二类，是 NoC / node mapping 方向的实验：

- 曾把 `serial_tl` 与 `pbus` 从同一个 infra node 上拆开
- 目的是排除共享 node 导致的 serial-TL 冲突

实验结论是：

- 这个改动没有消除 `mt_hello-baremetal` 的卡死
- 因而它不是当前主因

第三类，是本轮专门为 TSI / serial-TL 调试加的插桩：

1. 在 `generators/testchipip/src/main/scala/tsi/TSIToTileLink.scala` 中新增：

- `+tsi_debug=1`
- `+tsi_stall_cycles=<n>`

功能是：

- 打印 `mem.a.fire`
- 打印 `mem.d.fire`
- 在长时间无进展时打印 `STALL` 状态

2. 在 `generators/chipyard/src/main/scala/harness/TestHarness.scala` 中，将原先为
Verilator 兼容性保留的固定 `printf_cond = false` sink 改成 plusarg 可控：

- `+th_printf_cond=1`

这里必须明确说明：

- 这是纯调试用途
- 只影响 Verilator 下 `printf` 是否可见
- 不改变 NoC / Gemmini / ReRoCC 的功能路径

3. 调试版仿真器已经成功重编完成

当前二进制里已经确认包含以下字符串：

- `[TSI-TL] A ...`
- `[TSI-TL] D ...`
- `[TSI-TL] STALL ...`

因此可以确认：

- 调试插桩已经真正进入最终仿真器

#### 11.10.3 已完成实验与关键现象

本轮已完成的关键实验如下。

实验 A：直接跑 `mt_hello-baremetal`

- 现象：
  - 只打印：
    - `[UART] UART0 is here (stdin/stdout).`
  - 后续没有主程序输出

实验 B：打开 `+verbose` 看启动路径

- 现象：
  - 两个 hart 都进入 boot ROM
  - 最后停在 `wfi`
  - PC 收敛到：
    - `0x10034`

这说明：

- 不是 CPU 一上来就崩掉
- 更像是 host 侧后续启动 / 唤醒流程没有完成

实验 C：最小空闲路径验证

- 运行：
  - `none +init_read=0x1000 +no_hart0_msip`
- 结果：
  - 稳定返回 `0x80000000`

这说明：

- `TSIToTileLink` 不是完全坏掉
- `serial_tl` 也不是完全无响应
- `boot-address-reg@0x1000` 的默认值是正确的

实验 D：回放对齐 `init_write` 流

- 不加载 ELF
- 只把 `mt_hello-baremetal` 两个 `PT_LOAD` 段里的对齐 32-bit 内容转成大量：
  - `+init_write=0xADDR:0xDATA`
- 最后再执行：
  - `+init_read=0x1000 +no_hart0_msip`

结果是：

- 大量 `init_write` 本身可以持续完成
- 最后的 `Reading 1000 ...` 会挂住

这个实验非常关键，因为它把问题进一步收敛成：

- 不是“程序运行一段时间后才挂”
- 不是“只有完整 ELF loader 才会触发”
- 而是“TSI / serial-TL 在经历足够多写事务后进入停滞状态”

#### 11.10.4 已排除或明显弱化的解释

截至当前，可以明确排除或显著弱化以下解释。

1. “serial_tl 整体完全坏掉”

- 不成立
- 因为最小 `init_read=0x1000` 路径是通的

2. “boot address 错了”

- 不成立
- 因为 `boot-address-reg@0x1000` 返回的是期望值 `0x80000000`

3. “只是 `serial_tl` 和 `pbus` 共用一个 node 的问题”

- 目前证据不支持
- 因为拆开 node 之后现象没有变化

4. “只是 ELF 尾字节 / 非对齐写触发的 read-modify-write 问题”

- 目前证据也不支持
- 因为即使只回放对齐 32-bit 写流，最后的 `init_read` 仍然会挂住

#### 11.10.5 当前最强工作假设

截至本文档整理时，当前最强假设是：

- `TSIToTileLink` 的简单空闲读路径是正常的
- 但在经历较长写事务序列后，`tsi2tl` 自身状态机或其下游 TileLink / serial-TL
  握手进入停滞状态
- 因而 host 侧后续的：
  - `init_read`
  - `MSIP`
  等访问得不到返回

当前最需要确认的是：

- `tsi2tl` 最后卡在：
  - `s_write_data`
  - `s_write_ack`
  - `s_read_req`
  - `s_read_data`
  中的哪一个

以及卡住时到底是哪一侧不再前进：

- `mem.a.valid/ready`
- `mem.d.valid/ready`
- `io.tsi.in.valid/ready`
- `io.tsi.out.valid/ready`

#### 11.10.6 下一步建议

建议后续按以下顺序推进。

1. 先找一个“尽量小但仍能稳定复现卡死”的 `init_write` 数量

- 不要一上来就回放完整 `mt_hello-baremetal`
- 否则调试日志太大，不利于看最后几笔事务

2. 用调试版仿真器抓 `TSIToTileLink` 状态

推荐直接运行：

```bash
cd /home/ubuntu/chipyard
source env.sh
stdbuf -oL -eL \
  sims/verilator/simulator-chipyard.harness-GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA \
  +permissive \
  +th_printf_cond=1 \
  +tsi_debug=1 \
  +tsi_stall_cycles=2000 \
  ...最小复现的 init_write 序列... \
  +init_read=0x1000 \
  +no_hart0_msip \
  +permissive-off \
  none
```

3. 如果 `printf` 仍然不足以判断，就直接上 `gdb`

重点观察这些 Verilated state：

- `TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__state`
- `TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__cmd`
- `TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__addr`
- `TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__len`
- `TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__idx`
- `TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__tsiStallCounter`

4. 根据停滞状态决定修复位置

- 若卡在 `s_write_ack`：
  - 优先查为什么 `mem.d.valid` 不回来
- 若卡在 `s_read_req` / `s_read_data`：
  - 优先查 serial-TL / downstream ready-valid
- 若卡在 `s_write_data`：
  - 优先查 `mem.a.ready` 与前一笔 write body 的交接

5. 修完后回归顺序

- 先回归：
  - `mt_hello-baremetal`
- 再回归快速 baremetal：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/bareMetalC/rerocc_lc_matrix_baremetal_coupleddma-baremetal`

#### 11.10.7 给下一个 AI 的 Prompt

下面这段 prompt 可以直接交给下一个 AI。

```text
你现在在 /home/ubuntu/chipyard 仓库里继续接手调试。请用中文交流，优先直接动手，不要只给计划。

当前目标是继续调通 small-config 本地 Verilator 启动路径，当前配置是：
- chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA
- 仿真器：
  /home/ubuntu/chipyard/sims/verilator/simulator-chipyard.harness-GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA

当前最重要结论：
1. mt_hello-baremetal 现在只打印：
   [UART] UART0 is here (stdin/stdout).
   然后卡住。
2. 开 +verbose 后可以看到两个 hart 都在 boot ROM 里停到 wfi，PC 最后在 0x10034。
3. none +init_read=0x1000 +no_hart0_msip 是通的，返回 0x80000000。
4. 所以 serial/TSI 不是完全坏掉。
5. 更关键的是：
   - 不加载 ELF
   - 只回放 mt_hello-baremetal 的对齐 32-bit init_write 流
   - 然后再做 +init_read=0x1000
   - 也会卡住
6. 当前 strongest hypothesis 是：
   - TSI / serial-TL 在经历足够多写事务后进入停滞状态
   - 不是简单的 boot-address 配置错
   - 也不只是 ELF 尾字节 / 非对齐问题

本轮已经做过的源码改动：
1. 在 generators/testchipip/src/main/scala/tsi/TSIToTileLink.scala 中加入：
   - +tsi_debug=1
   - +tsi_stall_cycles=<n>
   可以打印 mem.a.fire / mem.d.fire / STALL。
2. 在 generators/chipyard/src/main/scala/harness/TestHarness.scala 中，把 printf_cond 改成 plusarg 可控：
   - +th_printf_cond=1
   这是纯调试用途，只影响 Verilator 下 printf 是否可见，不改功能路径。
3. 调试版仿真器已经重编成功，二进制里已确认包含：
   - [TSI-TL] A ...
   - [TSI-TL] D ...
   - [TSI-TL] STALL ...

你接下来直接做这些事：
1. 先找一个“尽量小但仍能稳定卡死”的 init_write 数量，减少日志体积。
2. 用调试版仿真器运行：
   cd /home/ubuntu/chipyard
   source env.sh
   stdbuf -oL -eL \
     sims/verilator/simulator-chipyard.harness-GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA \
     +permissive \
     +th_printf_cond=1 \
     +tsi_debug=1 \
     +tsi_stall_cycles=2000 \
     ...你的最小复现 init_write 序列... \
     +init_read=0x1000 \
     +no_hart0_msip \
     +permissive-off \
     none
3. 抓最后几笔 A / D / STALL，判断到底停在 s_write_data / s_write_ack / s_read_req / s_read_data 哪一个。
4. 如果 printf 仍然不够，就直接用 gdb 看这些符号：
   - TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__state
   - TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__cmd
   - TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__addr
   - TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__len
   - TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__idx
   - TestDriver__DOT__testHarness__DOT__ram__DOT__tsi2tl__DOT__tsiStallCounter
5. 如果抓到是 s_write_ack 卡住，就重点查为什么 mem.d.valid 不回来。
6. 如果抓到是 s_read_req 或 s_read_data 卡住，就重点查 serial-TL / downstream ready-valid。
7. 修完以后先回归 mt_hello-baremetal，再跑：
   /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/bareMetalC/rerocc_lc_matrix_baremetal_coupleddma-baremetal

测试计划也请一并执行，按下面顺序，不要一上来就跑最重用例：

阶段 0：bootstrap sanity
- 先运行：
  none +init_read=0x1000 +no_hart0_msip
- 通过标准：
  必须返回 0x80000000
- 目的：
  确认最小 TSI/boot-address 路径仍然通

阶段 1：boot sanity
- 运行：
  mt_hello-baremetal
- 通过标准：
  不能只停留在 `[UART] UART0 is here (stdin/stdout).`
  必须继续出现真正的程序输出
- 目的：
  先确认 host 加载 / 启动链路恢复

阶段 2：主 quick case，优先验证 coupled-DMA 基本功能
- 在不改软件栈语义的前提下，只通过 EXTRA_CFLAGS 缩小工作量，重编：
  cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC
  GEMMINI_NUM_CPU_CORES=2 make rerocc_lc_matrix_baremetal_coupleddma-baremetal \
    EXTRA_CFLAGS='-DREROCC_NUM_GEMMINI=2 -DREROCC_NUM_DMA=2 -DREROCC_GEMMINI_BASE_ID=0 -DREROCC_DMA_BASE_ID=2 -DREROCC_LOGICAL_CORES=1 -DREROCC_MATRIX_MODE=2 -DREROCC_DMA_BYTES=512'
- 说明：
  这里 `REROCC_MATRIX_MODE=2` 表示 single pair，只跑单个 gemmini/dma 对；
  `REROCC_DMA_BYTES=512` 把 DMA 传输量压到很小，加快仿真
- 运行重编后的：
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/bareMetalC/rerocc_lc_matrix_baremetal_coupleddma-baremetal
- 通过标准：
  需要看到：
  - `GEMMINI_MATRIX_RESULT ... fail=0`
  - `DMA_MATRIX_RESULT ... fail=0`
  - `ALL_TESTS_PASS`
- 目的：
  用最快的 coupled-DMA 代表性 case 覆盖：
  - ReRoCC acquire/release
  - Gemmini manager
  - Coupled DMA
  - shared scratchpad / xlate

阶段 3：轻量并发 smoke test
- 如果阶段 2 通过，再重编：
  cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/bareMetalC
  GEMMINI_NUM_CPU_CORES=2 make rerocc_lc_nonblocking_baremetal_coupleddma-baremetal \
    EXTRA_CFLAGS='-DREROCC_NUM_GEMMINI=2 -DREROCC_NUM_DMA=2 -DREROCC_GEMMINI_BASE_ID=0 -DREROCC_DMA_BASE_ID=2 -DREROCC_DMA_BYTES=512 -DREROCC_LONG_CONV_ITERS=1 -DREROCC_SHORT_CONV_ITERS=1 -DREROCC_LONG_RESADD_ITERS=1 -DREROCC_LONG_DMA_ITERS=1 -DREROCC_SHORT_DMA_ITERS=1'
- 说明：
  这里保留 nonblocking 场景覆盖，但把迭代数压到最小，只做 smoke test
- 运行重编后的：
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/bareMetalC/rerocc_lc_nonblocking_baremetal_coupleddma-baremetal
- 通过标准：
  需要看到：
  - `SCENARIO_RESULT ... pass=1` 的场景结果
  - `ALL_TESTS_PASS`
- 目的：
  在小成本下验证并发 / nonblocking 路径没有被当前修复破坏

执行原则：
- 任何阶段失败，都先修当前阶段，不要直接跳到更重阶段
- 优先保留“只通过 EXTRA_CFLAGS 缩小规模”的方式，不要改 pipeline runtime 软件栈
- 每修一轮，都把现象、修复点和是否通过对应阶段测试写回文档

你必须记住的约束：
- 用户偏好中文。
- 用户希望 AI 自主推进，不要频繁停下来问。
- 继续记录文档，不要只在终端里做。
- 尽量不要改 pipeline runtime 软件栈。
- 工作区是 dirty 的，不要回滚不属于你的改动。
- 当前 TestHarness 变更是调试开关，不是功能路径修复；如果最后不需要它，可以再清理，但先别急着回退。
```

### 11.11 2026-04-05 状态更新：路线归档与后续方向

截至 `2026-04-05`，这里补一个明确结论：

- 真实 pre-NoC grouped attachment 路线已经归档，不再作为当前实现方向
- 之所以归档，不是因为它“完全没有价值”，而是因为它最终到达的是：
  - 两个 manager 的 attachment 聚合
  - 而不是一个真正的 pair-level manager
- 当前希望达到的终态是：
  - `(Gemmini i + CoupledDMA i)` 作为一个真正的 `pair-wrapper manager`

因此后续工作原则是：

- 不再恢复 `aggregatePairedManagers`
- 不再恢复 `AttachmentGrouping.scala`
- 不再恢复 `NamedTLSourceShrinker.scala`
- 不再继续把 grouped attachment 当作当前主线修补

当前 authoritative 的下一步计划与交接 prompt 已移动到：

- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pair_wrapper_manager_plan_20260405.md`

### 11.12 2026-04-05 硬件事实更新：pair-wrapper 小配置已落地

截至 `2026-04-05` 的当前 checkpoint，pair-wrapper 小配置方向已经从“计划”进入“代码已落地”状态。

本轮已经落地且应视为当前硬件事实的内容：

- `generators/rerocc/src/main/scala/manager/Parameters.scala`
  - `ReRoCCTileParams` 新增 `preserveIncomingOpcode: Boolean = false`
- `generators/rerocc/src/main/scala/manager/Manager.scala`
  - `ReRoCCManager` 不再要求单一 opcode
  - 现在按“合法 opcode 集合”检查 incoming 指令
  - 在 pair-wrapper 配置中可以保留 incoming `custom2/custom3`
- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMAPairWrapper.scala`
  - 已新增真正的 pair-wrapper
  - 一个 wrapper 内部固定包含：
    - `Gemmini(custom3, gemmini_id = pairId)`
    - `GemminiCoupledDMA(custom2, gemmini_id = pairId)`
  - 对外作为一个物理 ReRoCC manager 暴露
- `generators/chipyard/src/main/scala/config/fragments/ReRoCCGemminiCoupledDMAPairFragments.scala`
  - 已新增 `WithReRoCCGemminiCoupledDMAPairManagers`
- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`
  - 已新增 pair-manager 专用配置链
  - 第一条小配置为：
    - `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2P2x1x2CoupledDMAPairManager`

同时确认：

- 没有恢复 `aggregatePairedManagers`
- 没有恢复 grouped attachment / grouped manager edge 路线
- 没有覆盖当前 separate-manager 的 large dummy `Sbus256/Sbus128` 基线
- 没有修改 `pipeline-runtime/src/prt_rerocc.c`

本轮已确认的编译事实：

- `make -C sims/verilator ... firrtl CONFIG=GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2P2x1x2CoupledDMAPairManager`
  已通过
- chisel/elaboration 日志显示：
  - `ReRoCC Manager id 0 is a gemmini.GemminiCoupledDMAPairWrapper`
  - `ReRoCC Manager id 1 is a gemmini.GemminiCoupledDMAPairWrapper`
- 这说明当前 manager 计数已经按 `P=2` 展开，而不是旧的 `Gemmini + DMA = 4`

本轮已确认的 baremetal 编译事实：

- `mt_hello-baremetal`
- `rerocc_lc_matrix_baremetal_coupleddma-baremetal`
- `rerocc_lc_resadd_explicit_interleaved-baremetal`
- `rerocc_lc_nonblocking_baremetal_coupleddma-baremetal`

以上 4 个 binary 均已在当前仓库中重编通过。

其中 pair-manager 最小软件适配只发生在：

- `bareMetalC/learn-gemmini/rerocc_lc_matrix_baremetal_coupleddma.c`
- `bareMetalC/learn-gemmini/rerocc_lc_nonblocking_baremetal_coupleddma.c`

并且都通过 `REROCC_PAIR_MANAGER_MODE=1` 走“同一 acquired cfg 绑定 `custom3 + custom2`”路径。

本轮已确认的 runtime 事实：

- 阶段 2：
  - log: `/tmp/pair_stage2_matrix_trace.log`
  - `rerocc_lc_matrix_baremetal_coupleddma-baremetal` 已通过
  - 关键结果：
    - `GEMMINI_MATRIX_RESULT mode=single pass=1 fail=0 expected=1`
    - `DMA_MATRIX_RESULT mode=single pass=1 fail=0 expected=1 bytes=512`
    - `ALL_TESTS_PASS`
- 阶段 3：
  - log: `/tmp/pair_stage3_resadd_bias_focus_quiet_nodram.log`
  - `rerocc_lc_resadd_explicit_interleaved-baremetal` 已通过
  - 关键结果：
    - `CASE_RESULT bias_mvin3_runtime_alias_focus PASS`
    - `ALL_TESTS_PASS`
- 阶段 4：
  - log: `/tmp/pair_stage4_nonblocking_final2.log`
  - `rerocc_lc_nonblocking_baremetal_coupleddma-baremetal` 已通过
  - 关键结果：
    - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
    - `ALL_TESTS_PASS`

本轮同时新增并确认了一个软件事实：

- stage 4 的计划要求把 `bytes=512`、所有迭代数压到 `1`
- 在这种“最小 smoke”形态下，原 nonblocking test 的 completion-order 指标不再稳定
- 因此 `bareMetalC/learn-gemmini/rerocc_lc_nonblocking_baremetal_coupleddma.c`
  现在只在 `long_iters > short_iters` 时保留原来的 latency-order check
- 当 stage 4 进入 `long_iters == short_iters == 1` 的 smoke 配置时：
  - pass 只要求 `ok0 && ok1`
  - `overlap` / `short_before_long` 继续打印，但只作为观测指标

为了减少本地 smoke 噪声，仍保留：

- `tools/DRAMSim2/AddressMapping.cpp`
  - 把 DRAMSim 对未 64B 对齐事务的 warning 改成单次提示
- 本地回归如需 DRAMSim 覆盖，继续使用仓库内 `tools/DRAMSim2/libdramsim.so`

当前 pair-wrapper 小配置方向已完成阶段 0 到阶段 4 的闭环验证。
下一步只剩计划中的阶段 5：large dummy pair-wrapper 配置与后续 FireSim / buildbitstream 评估。

## 13. 2026-04-05 补充：large dummy pair-wrapper 的代码与 elaboration 事实

在已有 pair-wrapper 小配置之外，当前源码还已经落地了 large dummy pair-wrapper 的 stage 5 入口。

相关代码入口位于：

- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`

该文件中的 pair-manager 参数化配置已经直接支持以下旋钮：

- `numPairs`
- `useDummyGemmini`
- `sharedSpadBytes`
- `useCompactPairManagerLayout`
- `globalNoCVirtualChannelDepth`

并新增了两条 large dummy pair-wrapper 具体配置：

- `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus256`
- `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`

从源码可直接确认，这两条配置共享如下硬件参数：

- `4` 个 Rocket core，布局为 `2x2`
- `12` 个 pair-wrapper manager，布局为 `4x3`
- `2` 个 memory channel
- dummy Gemmini
- `16x16` mesh
- `useGlobalNoC = true`
- `useDeterministicGlobalNoCRouting = true`
- `sharedSpadBytes = 1 MiB`
- `useCompactPairManagerLayout = true`
- `globalNoCVirtualChannelDepth = 4`

两条配置唯一关键差异是 `sbusWidthBits`：

- `Sbus256` 变体：`32 * 8`
- `Sbus128` 变体：`16 * 8`

本轮本地 elaboration 已进一步确认这两条大配置都能展开为真实的 `12` 个 pair-wrapper manager，而不是旧的 “Gemmini + DMA 分离计数” 形态：

- 日志显示 `ReRoCC Manager id 0` 到 `11` 全部为 `gemmini.GemminiCoupledDMAPairWrapper`
- ReRoCC outwards mapping 为 `Managers: List(0)` 到 `List(11)`
- 设备树与 TL 映射里出现了 `12` 个 `rerocc-mgr@...` CSR 窗口
- shared scratchpad 的全局窗口按 `1 MiB` 粒度落在：
  - `0x40000000`
  - `0x40100000`
  - ...
  - `0x40b00000`

因此，就当前仓库源码与本轮 elaboration 证据而言，large dummy pair-wrapper 的 stage 5 硬件前提已经从“计划”进入“代码已落地且可本地展开”的状态。

## 14. 2026-04-06 补充：large dummy pair-wrapper 的 `post_synth` 面积归因事实

本节记录当前 large dummy pair-wrapper `Sbus256` buildbitstream 路线在远端
Vivado `post_synth` 阶段暴露出的资源事实。这里的重点不是“是否最终能过位流”，
而是解释当前 NoC 面积的真实来源。

本节对照使用的两份报告为：

- 当前 pair-wrapper：
  - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/26_04_06-085945.post_synth_utilization.rpt`
- 当前 active separate-manager baseline：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-04--01-15-02-f2_gemmini_rerocc_globalnoc_coupleddma_dummy16x16_4c12g12d_sbus256_20mhz/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2G12x4x3D12x4x3CoupledDMADummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/reports/26_04_04-044837.post_synth_utilization.rpt`

### 14.1 先纠正一个直觉误差

和当前 active separate-manager baseline 相比，pair-wrapper 的 NoC 面积并不是
“完全没有缩小”，而是已经缩小了，但缩幅只有约 `20%`，显著小于从“mesh 变紧凑”
得到的直觉预期。

关键热点对比如下：

| 行项目 | pair-wrapper LUT | baseline LUT | 变化 |
| --- | ---: | ---: | ---: |
| `cl_firesim` | 1,406,466 | 1,480,982 | -5.0% |
| `sbus` | 533,460 | 669,845 | -20.4% |
| `globalNoCDomain` | 521,561 | 653,783 | -20.2% |
| `NoC` | 507,714 | 631,858 | -19.6% |
| `rerocc_tile` total | 507,075 | 484,701 | +4.6% |

### 14.2 当前 pair-wrapper 仍然不是“pre-NoC 完全折叠”

从当前源码可直接确认：

- pair-wrapper 使用的是独立参数化配置
  - `GemminiLearningReRoCCPairManagerConfigs.scala`
- pair-wrapper 仍然保留：
  - `WithGlobalNoC`
  - `GlobalTLNoCParams`
  - 完整的 `sbusNodeMapping`
- 当前 pair node 在映射上仍承载多类 TL 入口/出口：
  - `ReRoCC i DCache`
  - `port_named_rerocc_i[`
  - `sport_named_rerocc_i[`
  - `sport_named_rerocc_sbus_i[`
  - `Gemmini i-`

因此，当前路线虽然把 manager 计数从 `24` 变成了 `12`，但并没有把 pair 相关的
TL 暴露在 NoC 之前完全折叠成“极少数超节点”。这也是 NoC 只缩约 `20%`、而不是
线性随“逻辑 pair 数减半”缩小的根本原因之一。

### 14.3 更细的重复结构统计

为了避免层级统计重叠，本轮额外统计了重复出现的综合结构。

| 结构 | baseline | pair-wrapper | 变化 |
| --- | ---: | ---: | ---: |
| `rerocc_tile` 数量 | 24 | 12 | -50.0% |
| `rerocc_tile` 总 LUT | 484,701 | 507,075 | +4.6% |
| `rerocc_tile` 平均 LUT | 20.2k | 42.3k | +109.5% |
| baseline 中 12 个大 Gemmini tile 总 LUT | 401,310 | N/A | N/A |
| baseline 中 12 个小 DMA tile 总 LUT | 83,391 | N/A | N/A |
| pair 内部 `GemminiCoupledDMAPairWrapper` 总 LUT | N/A | 455,009 | N/A |
| `coupler_from_port_named_rerocc_*` 数量 | 24 | 12 | -50.0% |
| `coupler_from_port_named_rerocc_*` 总 LUT | 14,412 | 9,151 | -36.5% |
| `router_sink_domain*` 数量 | 63 | 40 | -36.5% |
| `router_sink_domain*` 总 LUT | 631,858 | 507,714 | -19.6% |
| `router_sink_domain*` 平均 LUT | 10.0k | 12.7k | +26.6% |
| `ingress_unit_*` 数量 | 130 | 82 | -36.9% |
| `ingress_unit_*` 总 LUT | 53,329 | 41,598 | -22.0% |
| `egress_unit_*` 数量 | 131 | 95 | -27.5% |
| `egress_unit_*` 总 LUT | 25,041 | 19,747 | -21.1% |

### 14.4 这些数字说明了什么

可以直接得出的硬件事实是：

- direct coupler 数量的减少是有效的：
  - `coupler_from_port_named_rerocc_*` 总 LUT 已经明显下降
  - 这说明 pair 化确实减少了最表层的 sbus ingress 数量
- 但是 surviving router / ingress / egress 的平均代价上升了：
  - `router_sink_domain*` 平均 LUT 从约 `10.0k` 增到约 `12.7k`
  - `ingress/egress` 总面积也没有跟实例数同比下降
- 同时，manager 侧逻辑没有缩小，反而略变大：
  - 原 baseline 的 `12` 个大 Gemmini tile 加 `12` 个小 DMA tile，总计
    `484,701 LUT`
  - 现在 `12` 个 pair `rerocc_tile` 总计 `507,075 LUT`

因此，当前 buildbitstream 路线里“mesh 缩了但 NoC 没有按预期缩小”的真正原因是：

- 主导成本已经不再只是 router 个数
- 而是每个 pair endpoint 上更重的 TL 接口集合、缓冲、source/in-flight 状态、
  以及 `Sbus256` 下更宽的数据通路

### 14.5 当前最重要的资源结论

截至这轮 `post_synth` 分析，可以把当前 stage5 pair-wrapper 的资源结论概括为：

- pair-wrapper 已经比 active separate-manager baseline 更省 NoC 面积
- 但节省幅度只在 `20%` 左右
- `Sbus256 + 12 pair + 1 MiB shared spad + VC depth 4` 下，面积主导项已转向：
  - 宽总线协议代价
  - pair endpoint 自身复杂度
  - NoC router / ingress / egress 的平均复杂度

换句话说，如果后续还需要继续压面积，单纯继续缩 mesh 布局不会是最有效的杠杆；
更有效的杠杆将落在“减少每个 pair 对外暴露的协议重量”和“瘦身 NoC 路径语义”上。

## 15. 2026-04-07 补充：`4c8p8 Sbus256` 已拿到 AGFI，但当前运行时不可用

在前述 `12 pair Sbus256` 资源分析之外，本轮还新增了一条更小的 large dummy
pair-wrapper 结果：

- 配置方向：
  - `4 CPU`
  - `8 pair`
  - `dummy Gemmini`
  - `16x16`
  - `shared spad = 1 MiB`
  - `Sbus256`
- build 结果目录：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-06--18-16-00-f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c8p8_sbus256_20mhz/`
- AGFI：
  - `agfi-01818b1718ae9b7a0`

这说明：

- pair-wrapper 大配置已经不只是“能 elaboration / 能综合”
- 也已经至少有一版真正完成了 FireSim `buildbitstream -> tar2afi`

但这并不等于它已经是一个可用的 FireSim runtime 硬件目标。

### 15.1 当前运行时失败发生在 very-early bring-up

针对上述 AGFI，本轮已实际搭建并执行了最小 smoke：

- FireMarshal `linux-poweroff` build/install 成功
- FireSim `launchrunfarm` 成功
- FireSim `infrasetup` 失败

失败点不是 workload 本身，而是 driver preflight：

- `+check-fingerprint` 没有完成
- 日志只到：
  - `entered simulation flow execution`
- 没有到：
  - `finished waiting`
  - `FireSim fingerprint: 0x46697265`

相关日志为：

- `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--02-51-42-infrasetup-H1TACA6XDZ2321BU.log`

并且这不是 FireSim manager 默认 `30s` timeout 过短造成的假象。本轮还在远端
F2 host 上手动复验：

- `timeout --kill-after=5s 180s sudo ./FireSim-f2 +slotid=0 +check-fingerprint`

结果仍然超时。

因此，当前这版 `4c8p8 Sbus256` AGFI 的更准确结论是：

- 映像已成功生成并注册
- 但 very-early host-FPGA 初始化没有完成
- 当前不能当作可用 runtime 基线，也不能当作 stage5 已验证通过

### 15.2 当前最坏时序并不指向 NoC router 组合深度

本轮新增两份关键时序证据：

- `post_route_timing`：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-04-06--18-16-00-f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c8p8_sbus256_20mhz/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P8x4x2CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config/build/reports/cl_f2-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P8x4x2CoupledDMAPairManagerDummy16x16Sbus256-FRFCFS16GBQuadRank_BaseF2Config.2026_04_06-181805.post_route_timing.rpt`
- 从 `post_route.VIOLATED.dcp` 额外拉出的 timing summary：
  - `/home/ubuntu/chipyard/tmp/analysis_4c8p8_pair/timing_summary.rpt`

当前最关键的数字为：

- 设计总 WNS：
  - `-5.015ns`
- 设计总 TNS：
  - `-1075.048ns`
- failing endpoints：
  - `1696`
- `WRAPPER/CL/clk_main_a0` 域内自身也有明显违例：
  - WNS `-0.932ns`
  - TNS `-318.980ns`
  - failing endpoints `1140`

但最差路径本身不是 NoC router 的长逻辑链，而是：

- source 位于：
  - `WRAPPER/CL/PIPE_DDR_STAT_ACK0/pipe_reg[...]`
- destination 位于 static shell 侧 `clk_main_a0` 域
- data path delay：
  - `11.419ns`
- 其中 route 占比：
  - `11.340ns`
  - `99.3%`
- logic levels：
  - `0`
- 物理上包含：
  - `SLR Crossing[2->1]`

这说明当前最坏时序的主导因素更接近：

- CL 与 static shell 之间的跨 SLR 全局布线
- DDR/status 相关 shell-boundary 必经路径
- 宽总线 shell-facing infrastructure 带来的放置扩散与路由压力

而不是“内部 NoC hop 数太多导致组合链太深”。

### 15.3 为什么“mesh 已缩小但延迟还是很大”

结合第 14 节的面积归因和本轮时序事实，可以把当前现象概括为：

1. pair 数从 `12` 降到 `8` 后，面积确实继续下降
   - `cl_firesim`：
     - `1,406,466 -> 1,112,333 LUT`
     - `-20.9%`
   - `NoC`：
     - `507,714 -> 399,295 LUT`
     - `-21.4%`

2. 但最坏路径仍然落在 shell 边界相关的 route-dominated crossing
   - 说明“把 NoC 做得更紧凑”并没有自动把最坏路径带走

3. 与 `4c12g12d separate sbus128` 对比时，当前 `4c8p8 pair sbus256` 的 NoC 仍然很大
   - `4c12g12d separate sbus128` `NoC`：
     - `447,588 LUT`
   - `4c8p8 pair sbus256` `NoC`：
     - `399,295 LUT`
   - 仅约 `10.8%` 差异

这组对比说明：

- 纯粹减少 pair 数和压缩 mesh 布局当然有用
- 但 `Sbus256`、shell-facing 宽路径、以及当前 manager 数据面挂在完整 system-bus
  语义上的做法，仍然把设计推到了一个很高的布线/布局成本平台

### 15.4 与较小历史设计的区别

历史较小设计虽然也曾 timing violated，但至少还能走到：

- `finished waiting`
- `FireSim fingerprint: 0x46697265`
- `Commencing simulation.`

然后才在 target cycle 0 deadlock。

对应记录见：

- `/home/ubuntu/chipyard/tmp/firesim-aws-f2/test-logs/2026-03-15-runworkload-local-agfi-hangcheck.md`

因此，当前 `4c8p8 Sbus256` 这版的问题等级更高：

- 不是“勉强带病运行”
- 而是已经在 very-early bring-up 阶段暴露出不可用信号

### 15.5 与当前已跑通 runtime smoke 的 `4c12p12 pair Sbus128` 的直接对照

当前已经存在一版真正通过 FireSim runtime smoke 的大 pair-wrapper 配置：

- config：
  - `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
- AGFI：
  - `agfi-0dc8dcfa4c7735f40`
- `infrasetup` 成功日志：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--05-47-11-infrasetup-IICY0DY3T4M3MH91.log`
- `runworkload` 成功日志：
  - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--05-51-26-runworkload-NZ7FEGNCORQ86JJF.log`

它和当前失败的 `4c8p8 pair Sbus256` 对照后，有几个结论非常关键：

1. 成功版总面积并不更小
   - `4c8p8 pair Sbus256` `cl_firesim`：
     - `1,112,333 LUT`
     - `579,510 FF`
     - `1,407 DSP`
   - `4c12p12 pair Sbus128` `cl_firesim`：
     - `1,164,888 LUT`
     - `604,807 FF`
     - `2,079 DSP`

2. 两版最坏时序路径类型几乎相同
   - `4c8p8 pair Sbus256`：
     - WNS `-5.015ns`
     - 最坏路径位于 `WRAPPER/CL/PIPE_DDR_STAT_ACK0/...`
     - `11.419ns` data path 里 `11.340ns` 是 routing
   - `4c12p12 pair Sbus128`：
     - WNS `-4.943ns`
     - 最坏路径同样位于 `WRAPPER/CL/PIPE_DDR_STAT_ACK0/...`
     - `11.336ns` data path 里 `11.255ns` 是 routing

3. 但 runtime bring-up 行为完全不同
   - 失败版三次都只到：
     - `entered simulation flow execution`
   - 成功版则继续到：
     - `finished waiting`
     - `FireSim fingerprint: 0x46697265`
     - 后续 `runworkload` 也完成

4. 失败版已在新的 F2 host 上再次复现
   - 独立重跑所用 runtime/hwdb：
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus256_linux_poweroff_rerun.yaml`
     - `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c8p8_sbus256_smoke_rerun.yaml`
   - runfarm tag：
     - `pair-smoke-4c8p8-sbus256-rerun-20260407`
   - 新 host：
     - `i-0f006359e65a52669`
     - `192.168.1.246`
   - 新 `infrasetup` 结果：
     - `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--06-26-29-infrasetup-PV1ZJI9OCHQ8TP76.log`
   - 结果仍是 3 次 `+check-fingerprint` timeout，最终 `infrasetup` 退出码 `1`

这说明当前问题不能简单收敛成：

- “总面积太大”
- “WNS 太差”
- “NoC hop 太多”

更接近的解释是：

- `Sbus256` 这一宽 system-bus 形态改变了 pair-wrapper 周边、shell-facing 通路、
  以及布局/布线压力分布
- 即使在 pair 数从 `12` 降到 `8` 后，这种实现形态仍可能把设计留在
  “bitstream 可注册，但 very-early host-FPGA bring-up 失效”的状态
- 而 `Sbus128` 虽然没有把 shell-boundary 最坏路径彻底修干净，却已经足以让
  fingerprint preflight 和最小 Linux smoke 通过

### 15.6 当前最稳妥的硬件结论

到本轮为止，可以把“为什么延迟还是很大”收敛为下面这句：

- NoC 拓扑和 pair-level compact layout 会影响拥塞与 spread，但当前已观察到的最坏
  时序和最早运行时失败，更直接地由 shell-boundary 必经路径、跨 SLR 全局路由、
  以及 `Sbus256` 下的宽链路/协议基础设施主导

因此，如果后续还要继续修当前路线，最有价值的杠杆不会只是继续缩 mesh，而会是：

- 继续减少 pair 对外暴露的宽 TL/system-bus 语义
- 减少 shell-boundary 必经状态路径的往返
- 只有在这些手段都证据充分失败后，再把 `Sbus128` 当作真正的资源 fallback
