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
