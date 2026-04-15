# HybridMapper 面向 Gemmini pipeline-runtime 的修改计划

更新时间：`2026-04-11`

## 1. 文档目标

这份文档只回答一个问题：

- 如果目标执行后端是当前 `pipeline-runtime` 的设备主路径，也就是 `Gemmini + ReRoCC + DMA`，那么 `HybridMapper` 应该如何修改，才能生成与该运行时兼容、同时又与 `MudnacSim` 语义对齐的编排结果。

本文档当前只覆盖：

- 算子范围：`conv`、`resadd`
- 执行路径范围：设备主路径
- 产物范围：pipeline YAML、layer mapping YAML、runtime artifact 的生成规则

本文档当前不覆盖：

- `pooling`、`matmul`、`attention` 等未接入当前 runtime 的算子
- CPU 回退路径的最终性能语义
- 代码实现细节

本文档中的“已确认事实”都来自源码，不依赖 `MudnacSim` 注释。

## 2. 代码事实来源

本文档主要依据以下源码：

- `conference/HybridMapper/HybridMapper/StageRecord.py`
- `conference/HybridMapper/HybridMapper/SASearch.py`
- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`
- `conference/MudnacSim/src/runtime/pipeline.cpp`
- `conference/MudnacSim/src/runtime/pipeline_buffer.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_gemmini_adapter.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_types.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_artifacts.c`

## 3. Gemmini runtime 真实接口要求

这一节先回答“`HybridMapper` 后端必须迁就什么”，而不是先谈搜索器。

### 3.1 当前 runtime 只支持 `conv` 和 `resadd`

`pipeline-runtime` 当前只在 `get_layer_io_ids()`、`build_stage_task_desc()` 中显式识别两类层：

- `conv`
- `resadd`

源码锚点：

- `prt_runtime.c:943-970`
- `prt_runtime.c:3313-3495`

直接结论：

- `HybridMapper` 当前若面向这个 runtime 生成产物，后端 lowering 必须先只覆盖 `conv` 和 `resadd`。
- `pooling` 等未支持算子仍然不在当前计划范围内；本文不讨论它们如何插入 pipeline，也不把它们作为尺寸不匹配规则的前提。

### 3.2 `conv` 描述符的硬约束

`prt_gemmini_conv_desc_t` 暴露给后端的字段包括：

- 输入、权重、偏置、输出指针
- 输入/输出高宽、通道数、分组数
- 输入、权重、输出 stride
- 卷积 stride、padding、kernel 大小
- 激活、输出缩放、pool 参数、执行模式

源码锚点：

- `prt_gemmini_adapter.h:13-50`

当前 `build_stage_conv_desc()` 进一步施加了这些约束：

- 只接受 `conv`
- `param_len >= 10`
- `address_count >= 4`
- `KH == KW`
- `sH == sW`
- `sH != 0`
- 输入、权重、输出 stride 不能小于对应逻辑宽度
- `pool_size` 当前被固定成 `1`
- `pool_stride` 当前被固定成 `0`
- `pool_padding` 当前被固定成 `0`

源码锚点：

- `prt_runtime.c:3313-3404`

直接结论：

- 当前 runtime 的 `conv` 路径不是“通用卷积 API”，而是“无 pooling 的 Gemmini 卷积子集”。
- `HybridMapper` 后端不能把 pooling 的执行责任留给 runtime。

### 3.3 `resadd` 描述符的硬约束

`prt_gemmini_resadd_desc_t` 的核心字段是：

- `A`、`B`、`C`
- `I`、`J`
- `stride`

源码锚点：

- `prt_gemmini_adapter.h:52-64`

当前 `build_stage_resadd_desc()` 的关键约束是：

- 只接受 `resadd`
- 三个 tensor 的 stride 必须相等
- `stride` 不能小于 `J`
- 逻辑尺寸是从 layer 参数和 tensor size 推出来的，不是来自 `HybridMapper` 专门下发的 GEMM 指令级计划

源码锚点：

- `prt_runtime.c:3422-3495`

直接结论：

- `HybridMapper` 不能把 `resadd` lower 成任意形状的加法，只能 lower 成当前 runtime 能接受的“共享 stride 的二维视图”。

### 3.4 runtime 自己构造 Gemmini 描述符，不直接消费层内指令计划

当前 runtime 是这样拿到 Gemmini 执行描述符的：

1. 先按 `stage_prepare_exec_views()` 绑定 stage 执行视图。
2. 再用 `stage_tensor_exec_addr()` 取输入/输出/权重地址。
3. 最后现场构造 `prt_gemmini_conv_desc_t` 或 `prt_gemmini_resadd_desc_t`。

源码锚点：

- `prt_runtime.c:1891-2042`
- `prt_runtime.c:2045-2075`
- `prt_runtime.c:3313-3495`

直接结论：

- `HybridMapper` 目前不需要下发“每条 Gemmini 指令”。
- 它真正要保证的是：stage 本地张量布局、split 类型、bypass 标志、ring 和本地页大小，能让 runtime 在执行时推导出正确地址和正确布局。

### 3.5 当前 split 语义

runtime 当前识别的 split 类型是：

- `single`
- `oc`
- `spatial`
- `resadd_spatial`
- `unspec`

Gemmini 适配层再根据 `split_kind` 和 `tile_count` 选具体执行路径。

源码锚点：

- `prt_types.h:62-68`
- `prt_gemmini_adapter.c:2985-3067`

直接结论：

- `HybridMapper` 后端不需要生成 Gemmini 指令级切分，但需要把每个 stage lower 到这几个 split 语义之一。

### 3.6 layer mapping validator 实际检查什么

当前 runtime 的 layer mapping 校验，不消费整个 `HybridMapper` 搜索状态，只检查一小组会影响执行视图的字段：

- `layer_id`
- `target_accel`
- `dram_bypass`
- `spm_bypass`
- `split_kind`
- `others_spm_tensor_addr`
- `others_first_tensor_page_num`
- `others_spm_tensor_page_count`
- `others_spm_tensor_util`

源码锚点：

- `prt_gemmini_artifacts.c:483-532`

直接结论：

- `HybridMapper` 修改时，不必把内部搜索状态整个暴露给 runtime。
- 重点是把上述“会影响执行视图和调度”的字段稳定地生成出来。

## 4. HybridMapper 当前可复用部分

### 4.1 tensor type 到 runtime type 的投影已明确

当前 `StageRecord.py` 已经定义好 mapping type 到 runtime type 的投影：

| HybridMapper type | runtime type |
| --- | --- |
| `IO_SINGLE` / `IO_DOUBLE` | `DRAM` |
| `INTER_SINGLE` / `INTER_DOUBLE` | `ISOLATE_SPM` |
| `INTER_SHARED_WRITE` / `INTER_SHARED_READ` | `SHARED_SPM` |
| `INTER_DRAM_SINGLE` / `INTER_DRAM_DOUBLE` | `DRAM_DEPEN` |
| `INTER_PURE_DECOUPLING` | `ALL_RINGBUFFER` |

源码锚点：

- `StageRecord.py:120-149`
- `StageRecord.py:198-217`

直接结论：

- 这部分不需要推倒重做，可以继续复用。

### 4.2 double buffer、bypass 约束已明确

当前 `StageRecord.py` 还同时规定了：

- 哪些类型是双缓冲
- 哪些类型是单缓冲
- 什么情况下 `spm_bypass`
- 什么情况下 `dram_bypass`

源码锚点：

- `StageRecord.py:151-182`

直接结论：

- `HybridMapper` 的“tensor 类型”不只是命名，它同时决定 buffer 个数和 bypass 语义。
- 后续修改必须保持这层约束，不应只保留字符串类型名。

### 4.3 动态规划分段和模拟退火搜索主体可复用

当前 `SASearch` 的主体工作仍然是：

- 初始化 segment tensor 信息
- 搜索 tensor meta type
- 搜索 tensor type
- 估算 stage cycle 和资源占用

这些属于“前半段搜索器”，与 Gemmini 具体 lowering 不是强绑定关系。

源码锚点：

- `SASearch.py:638-645`
- `SASearch.py:1145-1160`
- `SASearch.py:1231-1245`

直接结论：

- 动态规划段划分、模拟退火、代价评估总体框架都可以保留。

## 5. 已确认的基线语义约束

### 5.1 segment 边界 tensor 当前不应变成 `ALL_RINGBUFFER`

当前启用的搜索路径里：

- segment 初始化时把边界 tensor 设成 `TYPE_IO`
- `_op_change_tensor_meta_type()` 不改 `TYPE_IO`
- `_op_change_tensor_type()` 也不改 `TYPE_IO`

源码锚点：

- `SASearch.py:638-645`
- `SASearch.py:1145-1155`
- `SASearch.py:1231-1239`

直接结论：

- `ALL_RINGBUFFER` 的设计前提是内部纯 transport，不是 segment 边界 IO。
- 这个约束要在新的 lowering 文档里被写成硬约束。

### 5.2 ring 大小和本地 SPM 大小的基线规则

当前 `SASearch` 的 ring 规则是：

- `TYPE_PURE_DECOUPLING` 总是建 ring
- `TYPE_NORMAL_SPM` 只有跨深度差大于 1 才建 ring
- `TYPE_NORMAL_DRAM` 总是建 ring
- `TYPE_NORMAL_DRAM` 的 ring 不占 SPM
- `TYPE_PURE_DECOUPLING` 的 ring slot 大小按最大尺寸
- `TYPE_NORMAL_SPM` / `TYPE_NORMAL_DRAM` 的 ring slot 大小按最小尺寸

源码锚点：

- `SASearch.py:305-335`

当前 stage 本地 SPM 规则是：

- `TYPE_IO`、`TYPE_NORMAL_DRAM`、`TYPE_NORMAL_SPM` 的 stage-local 大小按本层实际 tensor 大小决定，再乘单缓冲或双缓冲
- `TYPE_SHARED` 按最大尺寸乘 2
- `TYPE_PURE_DECOUPLING` 不额外要 stage-local SPM

源码锚点：

- `SASearch.py:337-393`

直接结论：

- “stage 本地执行大小”和“ring transport 大小”在基线里本来就是两套规则。
- 后续修改不能把这两者混成同一个概念。

### 5.3 artifact 生成规则当前已经表达了 `ALL_RINGBUFFER` 特例

当前 artifact 生成脚本会把：

- 普通 `PIPE` binding 的 `slot_count/pages_per_slot` 设成 stage-local 大小
- `SHARED_SPM` 做 alias group 合并
- `ALL_RINGBUFFER` 的 `PIPE` binding 强制写成 `slot_count=0, pages_per_slot=0`
- 独立 `RING` binding 再根据 `ring_buffer_count` 和 `ring_buffer_size_per` 生成

源码锚点：

- `create-pipeline-runtime-artifacts.py:721-801`

直接结论：

- 外部 artifact schema 当前已经能表达 `ALL_RINGBUFFER` 的“纯 ring transport”。
- 后续更应该修正 runtime 和 lowering 语义，而不是先改 schema。

## 6. MudnacSim 基线对 `DRAM_DEPEN` 和尺寸不匹配的处理

### 6.1 `DRAM_DEPEN` 在基线里是 DRAM 地址环，不是 SPM 数据环

`MudnacSim` 建 ring 时明确区分了三类：

- `DRAM` / `DRAM_DEPEN`：建 DRAM 地址环，尺寸使用最小 shape/stride
- `ISOLATE_SPM` 等：建 SPM 页环，尺寸使用最小 shape/stride
- `ALL_RINGBUFFER`：建 SPM 页环，尺寸使用最大 shape/stride

源码锚点：

- `pipeline.cpp:338-374`
- `pipeline_buffer.h:7-46`

进一步地，`DRAM_DEPEN` 在执行时：

- 入口从 ring 里取 `dram_base_addr`
- 出口向 ring 里写 `dram_base_addr`
- 网络层执行时仍然使用自己本地的 `pSpmPages`

源码锚点：

- `pipeline.cpp:420-520`
- `pipeline.cpp:532-620`

直接结论：

- `DRAM_DEPEN` 的基线定义是“本地 SPM + DRAM 地址环”。

### 6.2 基线对 `ISOLATE_SPM` 尺寸不匹配按最小范围发送

`MudnacSim` 在无 ring 的 `ISOLATE_SPM` 发送路径里，显式做了两件事：

- 页数取 `min(src_pages, dst_pages)`
- 真正发送命令时，用较小一边的 `shape/strides`

源码锚点：

- `pipeline.cpp:706-737`

源码中的直接语义是：

- 这是为了兼容跳过 `pooling`、`padding` 一类节点后造成的输入输出尺寸变化
- transport 只搬双方共同有效的前缀范围

直接结论：

- 基线源码里确实是用较小一边的 shape/stride 表达这个“最小共同范围”。
- 对当前 `conv + resadd` 范围的 `pipeline-runtime` 规划，可以把这条基线进一步抽象成：`ISOLATE_SPM` / `DRAM_DEPEN` 的跨 stage transport 只搬运 `min(src_bytes, dst_bytes)` 的前缀字节区间。
- 在这个范围内，DMA 不需要知道真实 tensor 布局，只需要正确模拟这段地址范围的拷贝。

## 7. 面向当前 runtime 的 HybridMapper 修改计划

### 7.1 总体策略

总体策略定为：

- 保留前半段搜索器
- 修改后半段 lowering
- 外部 schema 尽量小改，而不是完全不改
- 对 `conv/resadd` 明确一套 Gemmini 兼容约束
- 尺寸不匹配一律按 transport 字节规则处理：`ISOLATE_SPM` / `DRAM_DEPEN` 取 `min`，`ALL_RINGBUFFER` 取 `max`

这意味着真正要改的是：

- 边界 tensor 约束
- stage-local 布局生成规则
- ring 语义与 size 语义
- runtime artifact 生成时的硬断言

### 7.2 必须新增或收紧的 lowering 约束

后续 `HybridMapper` 文档化后，建议把以下约束写成硬规则：

1. segment 边界 tensor 不允许 lower 成 `ALL_RINGBUFFER`
2. `ALL_RINGBUFFER` 只能用于内部纯 transport
3. `DRAM_DEPEN` 的 ring 语义固定是 DRAM 地址环
4. `ISOLATE_SPM` 和 `DRAM_DEPEN` 的 ring slot 大小固定按最小尺寸
5. `ALL_RINGBUFFER` 的 ring slot 大小固定按最大尺寸
6. `local_page_count/local_spm_tensor_bytes` 表示 stage 执行时本地看到的张量大小，不能偷换成 transport 大小
7. `ISOLATE_SPM` / `DRAM_DEPEN` 的 transport 有效字节数固定取生产者与消费者本地字节数的最小值，DMA 只按地址区间搬运，不依赖真实 tensor layout 元数据
8. `ALL_RINGBUFFER` 的 ring slot 有效字节数固定取参与 stage 的最大本地字节数；页分配向上按页对齐，但有效字节数要单独保留
9. `resadd` 的两个输入在模型声明阶段就必须尺寸一致；这不是 runtime 里用最小范围兜底修补的问题

### 7.3 对 `conv + resadd` 的尺寸不匹配处理

当前范围内，尺寸不匹配只被定义为 transport 语义，而且是按字节窗口处理，不再要求 DMA 理解真实数据布局：

- 对 `DRAM_DEPEN` 和 `ISOLATE_SPM`
  传输有效字节数固定为 `min(producer_local_bytes, consumer_local_bytes)`；DMA 只需要从对应基地址起复制这段前缀字节区间
- 对 `ALL_RINGBUFFER`
  ring slot 的物理空间按最大尺寸分配，逻辑有效字节数也取参与方最大值；各 stage 执行时仍按自己的本地逻辑尺寸使用这段地址空间
- 对 `resadd`
  两个输入张量本身如果尺寸不匹配，应在模型或 lowering 阶段直接判非法；不在 runtime 里做最小化修补

这样做的理由是：

- 它与 `MudnacSim` 的最小/最大规则一致
- 它把 transport 语义收敛成“按地址区间搬多少字节”，更适合当前 DMA 实现
- 它与当前 artifact 中“stage-local 大小”和“ring slot 大小”分离的表达方式兼容
- `HybridMapper` 现有实现本来就是按 `tensor_id` 聚合 `min_size/max_size`、ring count、ring slot 大小和 use count；当前语义里不需要再把 transport bytes 细化到 producer-consumer 边级别

### 7.4 对 artifact 生成脚本的修改方向

在允许对 schema 做最小必要扩展的前提下，后续建议改动点是：

- 为边界 `ALL_RINGBUFFER` 增加显式拒绝
- 对 `DRAM_DEPEN` 输出时补充“这是 DRAM ring”的内部语义断言
- 保证 `local_spm_tensor_bytes` 始终填精确字节数，不能只回填页数乘页面大小
- 为显式 transport 语义增加按 `tensor_id` 建立的字节级字段
- 对 `ISOLATE_SPM` / `DRAM_DEPEN` 按 `tensor_id` 显式写入 `transport_effective_bytes`
- 对 `ALL_RINGBUFFER` 按 `tensor_id` 显式写入 `ring_slot_effective_bytes`
- runtime 侧仍根据 `local_spm_tensor_bytes` 和 tensor type 规则现场计算期望值，并与 artifact 显式值逐项对比校验
- 对 `ring_buffer_size_per`、`tensor_spm_util_in_ringbuffer` 与新增字节级字段之间的 min/max 规则增加一致性检查

这里按 `tensor_id` 记录是有源码依据的：

- `SASearch._generate_tensor_meta_type()` 要求同一个 `tensor_id` 在不同 stage 上投影出的 meta type 必须一致
- `SASearch._generate_ring_buffer()` 按 `tensor_id` 建 ring，并断言一个 `tensor_id` 只有一个 producer stage
- `Model` 里的 `min_size/max_size`、`in_degree/out_degree` 也都是按 `tensor_id` 在整个 segment 上聚合
- runtime 侧 ring binding、buffer binding、page allocation 的主键同样都是 `tensor_id`

## 8. 当前不建议做的事

当前不建议优先做这些事：

- 为了对齐语义，先重做整个 `HybridMapper`
- 先改动公共 YAML schema
- 在 runtime 里临时补一个伪 `pooling` 执行路径
- 让 runtime 直接消费 `HybridMapper` 的层内指令计划

理由很简单：

- 当前 runtime 的真实接口边界不是指令计划，而是 stage 布局和 transport 合同。

## 9. 待确认问题

前面 4 个开放项和 schema 粒度问题现在都已经收敛，当前只剩表示方式问题：

1. `transport_effective_bytes` 和 `ring_slot_effective_bytes` 是单独新增字段更清楚，还是复用某个现有 `util` 字段但改成“字节”语义更稳妥。
2. validator 是在 artifact 解析阶段立即校验这些按 `tensor_id` 的显式字节值，还是在拓扑构建阶段结合 stage-local 视图再做一次二次校验。
