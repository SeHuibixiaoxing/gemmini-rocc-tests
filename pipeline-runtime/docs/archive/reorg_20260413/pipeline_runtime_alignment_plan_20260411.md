# pipeline-runtime 对齐 MudnacSim + HybridMapper 的修改计划

更新时间：`2026-04-11`

## 1. 文档目标

这份文档只做三件事：

1. 说明当前 `pipeline-runtime` 与 `MudnacSim + HybridMapper` 基线相比，哪些地方已经一致，哪些地方还不一致。
2. 说明后续应该如何修改 `pipeline-runtime`，才能对齐正确语义。
3. 把暂时还没定死的问题单独列出来，避免把未确认结论混进修改计划。

当前阶段只写文档，不改代码。

## 2. 基线与范围

本文档默认采用以下边界：

- 正确语义基线：
  `HybridMapper` 决定 tensor 类型、ring 大小、stage-local 大小和 artifact 表达；
  `MudnacSim` 定义执行和物理分配语义。
- 目标实现范围：
  当前 `pipeline-runtime` 的设备主路径。
- 算子范围：
  `conv`、`resadd`。

这意味着本文档不试图一步解决：

- 未支持算子的数值执行
- CPU fallback 的最终最优语义

## 3. 已确认的当前差异

### 3.1 已基本一致的部分

以下几类语义，当前 runtime 与基线没有根本性冲突：

- 普通 `DRAM` 路径：
  输入从 DRAM 读到本地 SPM，输出从本地 SPM 写回 DRAM。
- `SHARED_SPM` 的 alias-group 思路：
  通过共享页集合避免显式 send。
- `ALL_RINGBUFFER` 的大方向：
  `PIPE` binding 本身不占本地页，执行时可以直接工作在 ring slot 上。
- Gemmini 描述符构造思路：
  用 stage 执行视图现算地址，而不是消费外部指令级计划。

### 3.2 `DRAM_DEPEN` 当前语义错误

这是当前最核心的根偏差。

基线语义：

- `DRAM_DEPEN = 本地 SPM + DRAM 地址环`

当前 runtime 实现：

- 只要 `DRAM_DEPEN` 有 ring，就把 `dram_base_addr` 清零
- 后续 `C1/C2` 就会改成在 `slot_pages` 和 ring 的 `slot_pages` 之间搬运

源码锚点：

- `prt_runtime.c:3007-3009`
- `prt_runtime.c:3050-3052`
- `prt_scheduler.c:244-247`
- `prt_scheduler.c:406-410`

直接结论：

- 当前实现把 `DRAM_DEPEN` 变成了“本地 SPM + SPM 数据环”。
- 这不是调度优化，而是语义偏离。

### 3.3 尺寸不匹配当前没有被正确定义

基线里，`MudnacSim` 已经对 `ISOLATE_SPM` 无 ring 路径做了最小范围发送：

- 页数取两边最小值
- 发送命令时使用较小一边的 shape/stride

源码锚点：

- `pipeline.cpp:706-737`

当前 runtime 则仍然要求：

- `dst_pages->size == src_pages->size`

并默认整页拷贝。

源码锚点：

- `prt_dma.c:970-999`

直接结论：

- 当前 runtime 还不能正确处理“前一层 `conv` 输出与下一层 `conv` 输入大小不匹配”的情况。
- 这不是简单地把断言放宽就能解决，因为它还涉及最后一页尾部字节。
- 但在当前 `conv + resadd` 范围里，transport 层不需要再恢复真实 shape/stride；按有效字节数做地址区间搬运即可。

### 3.4 当前 transport 长度默认等于完整本地页长度

当前关键路径都在用整页长度：

- `pages_bytes_rt() = page_count * page_size`
- `C2` 的导出 DMA 请求字节数直接取 `pages_bytes_rt(slot_pages)`
- `C3` 的 send 路径只能整页等长拷贝

源码锚点：

- `prt_scheduler.c:106-110`
- `prt_scheduler.c:395-410`
- `prt_scheduler.c:481-487`

直接结论：

- 即便后续放宽“页数相等”，也仍然会在尾页上过拷。

### 3.5 `ALL_RINGBUFFER` 还需要补两条约束

当前 `ALL_RINGBUFFER` 在执行路径上已经接近正确，但仍有两类问题要在后续核查和收紧：

1. segment 边界不应出现 `ALL_RINGBUFFER`
2. 内部纯 transport tensor 不应在 export 末端被无条件 materialize 回 model alias

这两点的基线依据见已存在的语义审计文档：

- `hybridmapper_mudnacsim_pipeline_runtime_buffer_semantics_matrix_20260410.md`
- `hybridmapper_mudnacsim_tensor_semantics_audit_20260409.md`

## 4. 目标正确语义

这一节不描述“现在怎么做”，只描述“后续应对齐到什么语义”。

### 4.1 `DRAM`

目标语义保持不变：

- entry：从 DRAM 取数据到本地 SPM
- export：从本地 SPM 写回 DRAM
- stage 执行：总是在自己的本地执行视图上工作

### 4.2 `DRAM_DEPEN`

目标语义必须改回：

- entry：
  先等待 ring 中对应 subbatch ready；
  ready 后，从 ring 给出的 DRAM 地址 fetch 到自己的本地 SPM。
- export：
  等待 ring 有空位；
  把当前 subbatch 对应的 DRAM 地址发布到 ring；
  或者把本地结果 flush 到 ring 为该 slot 保留的 DRAM 传输地址。
- stage 执行：
  始终使用自己的本地 SPM 视图，不直接在 ring slot 上工作。

换句话说：

- `DRAM_DEPEN` 的 ring 是“地址同步机制”，不是“数据存放位置”。

### 4.3 `ISOLATE_SPM`

目标语义：

- 无 ring：
  生产者的本地 SPM 结果发送到消费者的本地 SPM。
- 有 ring：
  ring 里存 SPM 页；生产者和消费者通过 ring slot 与各自本地页交互。

尺寸规则：

- transport 有效范围取生产者与消费者的共同有效最小范围。

### 4.4 `SHARED_SPM`

目标语义保持不变：

- 共享的是同一组物理页，而不是每次都显式 send。
- 生命周期由使用计数和 alias group 管理。

### 4.5 `ALL_RINGBUFFER`

目标语义：

- 这是纯 ring transport。
- `PIPE` 自己不拥有独立本地存储。
- 层执行直接在 ring slot 页上进行。
- ring slot 大小按最大尺寸分配。
- 边界 tensor 不允许是 `ALL_RINGBUFFER`。

## 5. 后续修改方案

### 5.1 子系统一：ring 表示层

后续 runtime 需要在内部把 ring 分成两种 storage kind：

- SPM 页环
- DRAM 地址环

当前 `prt_ringbuf_t` 只有：

- `slot_pages`

源码锚点：

- `prt_types.h:165-178`

因此后续修改方向是：

- 保留现有 SPM 页环给 `ISOLATE_SPM` 和 `ALL_RINGBUFFER`
- 为 `DRAM_DEPEN` 增加地址环表示
- 外部 YAML schema 只做最小必要扩展；ring kind 仍由 tensor type 和 ring 配置推导，新增的只是显式 transport 字节信息

### 5.2 子系统二：transport 大小与有效字节数

后续 runtime 需要显式引入“transport 有效范围”这个概念。

推荐规则：

- `DRAM_DEPEN`
  传输有效字节数取生产者与消费者共同有效最小字节数
- `ISOLATE_SPM`
  同样取共同有效最小字节数
- `ALL_RINGBUFFER`
  ring 物理空间按最大尺寸，但每个消费者只读取自己需要的有效前缀

这里要再加一条固定约束：

- transport 层按字节窗口工作，DMA 不需要理解真实 tensor 布局

并且这个规则不能只在 runtime 内部现算，还需要：

- 由 artifact 显式写入
- runtime 再根据 stage-local bytes 和 tensor type 规则现场重算
- 两者逐项对比校验

这些显式 transport 字节信息在当前计划里按 `tensor_id` 记录即可，不需要再细化到 producer-consumer 边。原因是：

- `HybridMapper` 当前对 ring count、ring size、ring use count 都是按 `tensor_id` 建模
- `Model` 当前对 `min_size/max_size` 也是按 `tensor_id` 聚合
- `SASearch` 还断言一个 `tensor_id` 在 segment 内只有一个 producer stage
- runtime 侧 ring binding、buffer binding、页分配也都是按 `tensor_id` 查找和绑定

这个规则需要同时作用在：

- 页数选择
- DMA 请求字节数
- `DRAM_DEPEN` 地址环 slot 的有效字节数
- `ALL_RINGBUFFER` slot 的逻辑有效字节数

### 5.3 子系统三：DMA helper

当前 runtime 已经有一个按精确字节数拷贝 SPM 虚拟地址区间的 helper：

- `prt_dma_copy_spm_va()`

源码锚点：

- `prt_dma.c:1065-1135`

而当前页列表拷贝接口仍然只有：

- 整页等长的 `prt_dma_copy_spm_pages()`

源码锚点：

- `prt_dma.c:970-999`

后续修改方向建议是：

- 新增“页列表 + 精确字节数”的 SPM 拷贝 helper
- `C3`
- `C5/C6`
- `DRAM_DEPEN` 的 `C1/C2`
  都改成按 transport bytes 走

### 5.4 子系统四：执行视图保持 stage-local

这一点必须强调，因为很容易改错。

当前 `stage_prepare_exec_views()` 绑定执行视图时，用的是：

- `local_spm_tensor_addr`
- `local_spm_first_vpage`
- `local_spm_page_count`
- `local_spm_tensor_bytes`

源码锚点：

- `prt_runtime.c:1891-2042`

后续修改的正确方向是：

- stage 执行视图继续代表“本 stage 执行时看到的本地张量布局”
- transport 的有效大小不能反向覆盖 stage 执行视图

换句话说：

- 先分清“本地执行布局”
- 再单独定义“跨 stage 搬多少”

### 5.5 子系统五：`DRAM_DEPEN` 的内部内存来源

在只做最小外部 schema 扩展的前提下，后续实现上需要一个 runtime 私有的 DRAM transport arena。

原因：

- 基线要求 `DRAM_DEPEN` 使用地址环
- 但当前 runtime 没有一个现成的、按 action 私有管理的 DRAM ring slot 分配器

因此推荐方案是：

- 为每个 action 私有地保留一片 DRAM transport 地址空间
- 每个 `DRAM_DEPEN` ring slot 记录这片空间中的一个起始地址和有效字节数
- 分配大小按最小有效范围向页对齐

这仍然满足：

- 外部 schema 只做最小必要扩展
- 内部语义回到 `MudnacSim` 基线

### 5.6 子系统六：`ALL_RINGBUFFER` 的导出限制

后续修改要补两条硬约束：

1. artifact validator 直接拒绝边界 `ALL_RINGBUFFER`
2. 只有真正的 segment 输出 tensor 才允许 export alias materialization

否则会出现：

- 内部纯 transport tensor 被重新 materialize 到模型别名地址

这会把本应只存在于 ring 的中间结果重新暴露成“模型层输出”。

### 5.7 子系统七：layer mapping validator

当前 validator 主要核对：

- `layer_id`
- `accUtil`
- `dram_bypass`
- `spm_bypass`
- `split_kind`
- 本地 SPM 地址、首虚拟页、页数、字节数

源码锚点：

- `prt_gemmini_artifacts.c:483-532`

后续建议补充的校验是：

- 边界 `ALL_RINGBUFFER` 非法
- `DRAM_DEPEN` 的 ring 不应被 lower 成 SPM ring
- `ALL_RINGBUFFER` 的 `PIPE` binding 必须是 `slot_count=0, pages_per_slot=0`
- `local_spm_tensor_bytes` 与 page count 必须自洽
- artifact 显式写入的 transport 字节信息必须与 runtime 现场重算值一致

## 6. 推荐实施顺序

虽然当前阶段不改代码，但为了后续实现不乱序，建议从文档上先固定实施顺序。

### 阶段 1：校正语义表示

- 明确 ring kind
- 明确 transport bytes 规则
- 给 artifact 补显式 transport 字节字段
- 明确边界 `ALL_RINGBUFFER` 约束

### 阶段 2：校正搬运逻辑

- 改 `DRAM_DEPEN`
- 改精确字节 DMA helper
- 改 `C3/C5/C6` 的尺寸不匹配路径

### 阶段 3：收紧导出与校验

- 收紧 alias materialization
- 收紧 artifact validator

### 阶段 4：补设备侧验收

- FireSim 设备主路径回归
- 日志写文件而不是只依赖 UART
- live 排障时只通过私网 IP SSH 到实例

## 7. 设备侧验收要求

后续进入实现阶段后，建议以这些条件作为通过标准：

1. `conv -> conv` 存在尺寸不匹配的 case 能跑通。
2. `conv -> resadd`、`resadd -> conv` 的主路径仍然正确。
3. `DRAM_DEPEN` 的 entry/export 日志能明确看到它在等待和发布“地址环”，而不是读写 SPM ring。
4. `ALL_RINGBUFFER` 内部 tensor 不再被错误写回 model alias。
5. FireSim 运行前必须确认 image 是新版本。
6. 运行日志优先写文件，并通过 watchdog 周期性 `sync`，避免只依赖 UART。
7. SSH 排障必须使用实例私有地址，不使用公网地址。

## 8. 待确认问题

以下问题现在收缩到“显式 transport 信息如何落地”这一类：

1. 对 `ALL_RINGBUFFER`，是否需要单独引入 `ring_slot_effective_bytes`，以便把“页分配大小”和“逻辑有效字节数”分开表达。
2. `transport_effective_bytes` / `ring_slot_effective_bytes` 是新增独立字段更稳，还是复用现有某个 `util` 字段并统一改成“字节语义”更稳。
3. 这些按 `tensor_id` 的显式字节值，是否同时在 YAML loader、artifact validator 和 topology builder 三处都做一致性校验。
