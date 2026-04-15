# HybridMapper / MudnacSim 缓存语义核查

更新时间：`2026-04-10`

## 1. 文档目标

本文件只做静态语义核查，目标有三件事：

- 把当前 `pairdummy/sbus128/bertmini segment=3` 的最新结论写清楚。
- 系统梳理 `HybridMapper` 和 `MudnacSim` 对各类 tensor/buffer 类型的定义、约束和执行路径。
- 明确指出当前 `pipeline-runtime` 与上述语义基线的偏离点，供后续修复时对照。

本文件刻意区分三类表述：

- `代码明确实现`：能在当前代码里直接定位到实现或生成规则。
- `当前推断`：基于现有代码和当前 case 得出的推论，仍建议后续用更多 case 复核。

本次 `2026-04-10` 复核额外遵循一条硬规则：

- 对 `MudnacSim`，**不使用代码注释作为证据**。
- 若某结论只能从注释得到、不能从执行路径或数据结构使用方式得到，本文件一律删除或降级为“未证实”。

## 2. 结论摘要

- 当前 `bertmini segment=3` 的 `tensor 6` 不是 segment 边界 tensor，而是 segment 内部 `stage0 -> stage1` 的运输 tensor。
- `HybridMapper` 当前实际启用的搜索算子里，segment 边界 tensor 会先被初始化为 `TYPE_IO / IO_SINGLE`，`_op_change_tensor_meta_type` 与 `_op_change_tensor_type` 又跳过 `TYPE_IO`，`merge/split` 也通过 `_init_segment_tensor_info()` 重建边界类型；因此在**当前启用的搜索流程**里，边界 tensor 不会被改造成 `INTER_PURE_DECOUPLING / ALL_RINGBUFFER`。
- `HybridMapper` 里的“访存类型”不是只决定 `TensorTypeRuntime`；同一个 `TensorType` 还同时决定：
  - 是否先计入 DRAM 访问
  - 归入 DRAM add/max 还是 NOC add/max
  - `dramBypass/spmBypass`
  - 是否进入 runtime `entry/exportTensorIdList`
  - 是否 double buffer
  这些字段后续又会继续参与 runtime artifact 的 layout 匹配，以及 `MudnacSim` 的 `pipeLayerMappingTable` 查表。
- `MudnacSim` 中的 `ALL_RINGBUFFER` 语义是 pure ring transport，不额外保留 stage 本地 buffer 存储。
- `MudnacSim` 源码会独立识别拓扑边界，但本轮没有发现它把“边界集合”和 `TensorStayType` 做完全硬绑定的断言。
- 当前 `pipeline-runtime` 的关键偏离不是“把 `ALL_RINGBUFFER` 分类错了”，而是 export 后仍会把这种内部运输 tensor 无条件 materialize 回 model alias。
- 当前 worktree 里的 alias target dedup patch 只能算调试止血，不能作为最终语义修复。

## 3. 当前 bertmini 证据

### 3.1 `tensor 6` 的 alias 地址是共享的

`代码明确实现`

文件：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml`

关键事实：

- layer 3 输出 `tensor 6`
  - `address[3] = 470016`
  - `address2[3] = 8997888`
- layer 4 输入 `tensor 6`
  - `address[2] = 470016`
  - `address2[2] = 8997888`

这说明：

- `address/address2` 不是“layer 3 输出一份、layer 4 输入另一份”的独立 storage。
- 同一 logical tensor 在不同 layer slot 上复用了同一对 alias 地址。

### 3.2 `segment 3` 中 `tensor 6` 是内部 `ALL_RINGBUFFER`

`代码明确实现`

文件：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.ours2.yaml`

关键事实：

- `segment_idx: 3`
- stage 0:
  - `entryTensorIdList: [3, 2]`
  - `entryTensorTypeList: [DRAM, DRAM]`
  - `exportTensorIdList: [6]`
  - `exportTensorTypeList: [ALL_RINGBUFFER]`
- stage 1:
  - `entryTensorIdList: [6, 4]`
  - `entryTensorTypeList: [ALL_RINGBUFFER, DRAM]`
  - `exportTensorIdList: [7]`
  - `exportTensorTypeList: [DRAM]`
- ring 配置：
  - `ring_buffer_count: {6: 2}`
  - `ring_buffer_size_per: {6: 512}`
  - `ring_buffer_use_count: {6: 1}`
- buffer binding：
  - stage 0 export 对 `tensor 6` 的 `PIPE` binding:
    `slot_count=0 pages_per_slot=0`
  - stage 1 entry 对 `tensor 6` 的 `PIPE` binding:
    `slot_count=0 pages_per_slot=0`
  - 另有独立 `RING` binding:
    `tensor_id=6 slot_count=2 pages_per_slot=512`

这说明：

- `tensor 6` 的运行时 transport storage 预期就是单独的 ring。
- 对应 stage entry/export pipe binding 本身不该再拥有独立 slot。

### 3.3 同一份 bertmini pipeline 中还有同类模式

`代码明确实现`

同一文件中还可以看到：

- `segment 12` 的 `tensor 19`
- `segment 14` 的 `tensor 22`
- `segment 30` 的 `tensor 46`

它们都表现为：

- 前一 stage export = `ALL_RINGBUFFER`
- 后一 stage entry = `ALL_RINGBUFFER`
- 同时存在独立 `RING` binding

当前未看到的模式：

- segment 首 stage 的入口 tensor 是 `ALL_RINGBUFFER`
- segment 末 stage 的出口 tensor 是 `ALL_RINGBUFFER`

### 3.4 对当前 case 的直接结论

`当前推断`

- 用户记忆中的语义
  “整个子图的输入/输出不应是 `ALL_RINGBUFFER`”
  从当前 `HybridMapper` 生成路径和当前 artifact 来看是有较强支撑的。
- 但当前 `segment 3 / tensor 6` 这个具体例子，不是该类违规。
- 因而当前问题更像是：
  runtime 把“内部 ring-only transport tensor”
  错误拉回了 alias materialization 路径。

## 4. HybridMapper 语义核查

### 4.1 类型体系

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/StageRecord.py`

#### 4.1.1 Meta type

| Meta type | 含义 |
| --- | --- |
| `TYPE_IO` | segment 边界 IO tensor |
| `TYPE_NORMAL_DRAM` | 走 DRAM 的内部 tensor |
| `TYPE_NORMAL_SPM` | 走独立 SPM 的内部 tensor |
| `TYPE_SHARED` | stage 间共享 SPM |
| `TYPE_PURE_DECOUPLING` | 纯解耦 transport |

#### 4.1.2 Mapping-time tensor type

| Mapping type | 对应 meta type | 备注 |
| --- | --- | --- |
| `IO_SINGLE` | `TYPE_IO` | runtime buffer 存在 |
| `IO_DOUBLE` | `TYPE_IO` | runtime buffer 存在 |
| `IO_NOBUFFER` | `TYPE_IO` | `get_not_runtime_buffer()`，不会进入 runtime entry/export 列表 |
| `INTER_DRAM_SINGLE` | `TYPE_NORMAL_DRAM` | runtime buffer 存在 |
| `INTER_DRAM_DOUBLE` | `TYPE_NORMAL_DRAM` | runtime buffer 存在 |
| `INTER_SINGLE` | `TYPE_NORMAL_SPM` | runtime buffer 存在 |
| `INTER_DOUBLE` | `TYPE_NORMAL_SPM` | runtime buffer 存在 |
| `INTER_SHARED_WRITE` | `TYPE_SHARED` | runtime buffer 存在 |
| `INTER_SHARED_READ` | `TYPE_SHARED` | runtime buffer 存在 |
| `INTER_PURE_DECOUPLING` | `TYPE_PURE_DECOUPLING` | runtime 上映射为 `ALL_RINGBUFFER` |

#### 4.1.3 Runtime type 映射

| Mapping type 组 | Runtime type |
| --- | --- |
| `IO_SINGLE` / `IO_DOUBLE` | `DRAM` |
| `INTER_DRAM_SINGLE` / `INTER_DRAM_DOUBLE` | `DRAM_DEPEN` |
| `INTER_SINGLE` / `INTER_DOUBLE` | `ISOLATE_SPM` |
| `INTER_SHARED_WRITE` / `INTER_SHARED_READ` | `SHARED_SPM` |
| `INTER_PURE_DECOUPLING` | `ALL_RINGBUFFER` |

补充点：

- `IO_NOBUFFER` 不会进入 runtime `entryTensorIdList/exportTensorIdList`。
- `StageCandidateFactory.create_stage_candidate()` 里，`entry/exportTensorIdList` 会显式过滤 `TensorType.get_not_runtime_buffer()`。

#### 4.1.4 `TensorType` 还同时决定哪些访存约束

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/StageRecord.py`

除了上面的 runtime type 映射外，同一个 `TensorType` 还会被源码投影成多组约束：

| 约束函数 | 当前源码返回值 |
| --- | --- |
| `get_need_dram_add()` | `IO_SINGLE` / `IO_NOBUFFER` / `INTER_DRAM_SINGLE` |
| `get_need_dram_max()` | `IO_DOUBLE` / `INTER_DRAM_DOUBLE` |
| `get_need_noc_add()` | `INTER_SINGLE` |
| `get_need_noc_max()` | `INTER_DOUBLE` |
| `get_need_acces_dram_first()` | `IO_SINGLE` / `IO_DOUBLE` / `IO_NOBUFFER` / `INTER_DRAM_SINGLE` / `INTER_DRAM_DOUBLE` |
| `get_not_runtime_buffer()` | 只有 `IO_NOBUFFER` |
| `is_spm_bypass()` | 只有 `IO_NOBUFFER` |
| `is_dram_bypass()` | 除 `UNKNOWN` 和 `IO_NOBUFFER` 外，其余已知类型都为真 |
| `get_runtime_with_doublebuffer()` | `IO_DOUBLE` / `INTER_DRAM_DOUBLE` / `INTER_DOUBLE` / `INTER_SHARED_WRITE` / `INTER_SHARED_READ` |
| `get_runtime_without_doublebuffer()` | `IO_SINGLE` / `INTER_DRAM_SINGLE` / `INTER_PURE_DECOUPLING` |

直接结论：

- `HybridMapper` 里“访存类型”的决定并不是单输出，而是同一个 `TensorType` 同时派生出：
  - 资源/代价模型使用的 DRAM/NOC 分类
  - runtime 是否需要 entry/export buffer
  - runtime 看到的 `TensorTypeRuntime`
  - stage-layer mapping 查表使用的 `dramBypass/spmBypass`
  - double-buffer 属性
- `dramBypass/spmBypass` 是一个比 `TensorTypeRuntime` 更粗的投影：
  - 只有 `IO_NOBUFFER` 会得到 `dram_bypass=0, spm_bypass=1`
  - 其余已知 tensor type 都会得到 `dram_bypass=1, spm_bypass=0`
  - weight tensor 也固定走 `dram_bypass=1, spm_bypass=0`
- 因而不能把“runtime type 是什么”和“bypass 是什么”当成两套独立语义；
  它们是从同一个 mapping-time `TensorType` 平行导出的两组字段。

### 4.2 segment 边界 tensor 的初始化与变异约束

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/SASearch.py`

关键实现：

- `_init_segment_tensor_info()`
  - 若某 tensor 在 segment 内 `in_degree == 0` 或 `out_degree == 0`
  - 则初始化为：
    - `stage_tensor_type[tid] = IO_SINGLE`
    - `segment_tensor_metatype[tid] = TYPE_IO`
- `_op_change_tensor_meta_type()`
  - 若当前 `meta_type == TYPE_IO`，直接 `continue`
- `_op_change_tensor_type()`
  - 若 `meta_type == TYPE_IO`，也直接 `continue`

直接结论：

- 在当前启用的 SA 搜索流程里，
  segment 边界 tensor 一旦被初始化成 `TYPE_IO`，
  后续不会被 `_op_change_tensor_meta_type()`
  或 `_op_change_tensor_type()`
  改成 `TYPE_PURE_DECOUPLING`。
- 当 `enable_partition_search` 打开时，
  当前真正加入 `_op_list` 的也是
  `_op_merge_adjacent_segments()` 和 `_op_split_segment()`；
  它们都会通过 `_init_segment_tensor_info()` 重新初始化新 segment 的边界类型。
- 代码里另外还存在一个
  `_op_shift_boundary_left()` helper，
  它会手工改写 `tensor_meta_type`；
  但它当前不在 `_op_list` 里，本轮没有把它算入“当前启用搜索流程”。

这条结论对用户的记忆有强支撑：

- “整个子图的输入/输出不应是 `ALL_RINGBUFFER`”
  至少在当前 HybridMapper **实际启用的** 搜索实现中，
  确实是被代码路径强烈维护的。

### 4.3 ring buffer 生成规则

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/SASearch.py`

关键规则来自 `_generate_ring_buffer()`：

| Meta type | 是否生成 ring | ring slot size |
| --- | --- | --- |
| `TYPE_PURE_DECOUPLING` | 总是生成 | 用 `max size` |
| `TYPE_NORMAL_SPM` | 只有 depth gap `> 1` 才生成 | 用 `min size` |
| `TYPE_NORMAL_DRAM` | 总是生成 | 用 `min size` |
| `TYPE_SHARED` | 当前这个函数不会直接因 `TYPE_SHARED` 启用 ring；若其它路径未来启用，则 size 规则会落到 `max size` 分支 | 条件启用时用 `max size` |

对应代码行为：

- `TYPE_PURE_DECOUPLING`:
  `use_ring_buffer = True`
- `TYPE_NORMAL_SPM`:
  `diff > 1` 时才启用 ring
- `TYPE_NORMAL_DRAM`:
  `use_ring_buffer = True`
- 当前函数里没有
  `TYPE_SHARED -> use_ring_buffer = True`
  这一分支；
  这里只能看出：
  如果未来别的路径给 `TYPE_SHARED` 开 ring，
  slot size 会落在 `tensor_max_size` 分支
- slot 大小：
  - `TYPE_SHARED` 和 `TYPE_PURE_DECOUPLING` 用 `tensor_max_size`
  - 其它用 `tensor_min_size`

### 4.4 各 meta type 的 SPM 占用模型

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/SASearch.py`

| Meta type | stage 内本地 SPM | shared SPM | ring SPM |
| --- | --- | --- | --- |
| `TYPE_IO` | 按 `IO_SINGLE/DOUBLE` 计入 stage | 无 | 无 |
| `TYPE_NORMAL_DRAM` | 按 `INTER_DRAM_SINGLE/DOUBLE` 计入 stage | 无 | 可有 |
| `TYPE_NORMAL_SPM` | 按 `INTER_SINGLE/DOUBLE` 计入 stage | 无 | depth gap 大时可有 |
| `TYPE_SHARED` | 不计入各 stage 本地独立空间 | 计入 `tensor_spm_util_shared` | 当前这条生成路径不直接为其开 ring |
| `TYPE_PURE_DECOUPLING` | 不再给 stage 本地 buffer 计额外空间 | 无 | 必有 |

特别注意：

- `TYPE_PURE_DECOUPLING` 在 `_generate_tensor_spm_need()` 里不会给 stage 本地 buffer 记额外空间；
  它依赖的是 `tensor_spm_util_in_ringbuffer`。
- 这与后文 `MudnacSim` 的“ALL ring 不额外保留本地 `pSpmPages`”是吻合的。

### 4.5 访存类型确定链

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/StageRecord.py`
- `conference/HybridMapper/HybridMapper/SASearch.py`
- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`
- `conference/MudnacSim/src/runtime/runtime.cpp`

把源码连起来看，当前 `HybridMapper -> artifact -> MudnacSim` 的决定链是：

#### 4.5.1 搜索态先固定 `tensor_type_dict`

`SASearch.PipelineSolution.LayerSolution` 初始化时，会直接根据 `tensor_type_dict` 生成：

- `_io_tensor_need_dram`
  - 由 `TensorType.get_need_acces_dram_first()` 决定
- `_dram_bytes`
  - 只累计 `_io_tensor_need_dram == 1` 的 IO tensor
- `_dram_bypass/_spm_bypass`
  - 由 `_initialize_bypass_lists()` 决定
- `_layer_mapping`
  - 立刻以 `MTMRecord.MapKey(self._dram_bypass, self._spm_bypass, self._num_acc)` 查表

也就是说：

- 在 `HybridMapper` 搜索内部，
  `TensorType` 不只是“给 runtime 打标签”；
  它已经先参与了 DRAM 统计和 layer mapping 选择。

#### 4.5.2 stage cycle / ring / SPM 需求继续复用同一组分类

`SASearch.py` 后续又继续拿同一个 `TensorType` 分类做代价与资源估计：

- `_generate_stage_cycles_per_layer()`
  - 用 `get_need_dram_add()/get_need_dram_max()`
    决定 DRAM 周期落在 add 还是 max
  - 用 `get_need_noc_add()/get_need_noc_max()`
    决定 NOC 周期落在 add 还是 max
- `_generate_ring_buffer()`
  - 用 `TensorMetaType`
    决定哪些 tensor 会生成 ring，ring slot 用 `min size` 还是 `max size`
- `_generate_tensor_spm_need()`
  - 用 `TensorMetaType`
    决定张量算作 stage 本地 SPM、shared SPM，还是 ring SPM

这说明：

- `HybridMapper` 选择某个 `TensorType` 后，
  其访存语义已经同时影响
  “会不会上 DRAM、会不会上 NOC、会不会建 ring、SPM 怎么记账”。

#### 4.5.3 `StageCandidate` 再把同一决定展开成 runtime 字段

`StageCandidateFactory.create_stage_candidate()` 会基于同一个 `tensorTypeDict` 同时生成：

- `entryTensorIdList/exportTensorIdList`
  - 先过滤掉 `IO_NOBUFFER`
- `entryTensorTypeList/exportTensorTypeList`
  - 由 `TensorTypeRuntime.generate_from_mapping_type()` 生成
- `entryTensorDoubleBufferList/exportTensorDoubleBufferList`
  - 由 `mapping_type.with_doublebuffer()` 生成
- `dramBypassList/spmBypassList`
  - 由 `get_dram_spm_bypass()` 生成

因此：

- runtime yaml 里的 `entry/export tensor type`
  和
  stage 的 `dramBypass/spmBypass`
  不是两套互不相关的来源；
  它们都来自同一个 `tensorTypeDict`。

#### 4.5.4 artifact 生成脚本把 bypass 当成硬匹配条件

`create-pipeline-runtime-artifacts.py` 的 `match_stage_runtime_mapping()` 匹配 runtime layer mapping 时，要求同时满足：

- `layer_id`
- `target_accel`
- `mapping_dram_bypass == stage_dram`
- `mapping_spm_bypass == stage_spm`
- `split_kind`

匹配不到就直接报错。

这意味着：

- `dramBypass/spmBypass` 不是装饰字段；
  它是 artifact 选择具体 runtime layout 的硬约束。
- 因而审计 runtime 语义时，
  不能只看 `entryTensorTypeList/exportTensorTypeList`，
  还必须把 bypass 一起看。

#### 4.5.5 `MudnacSim` runtime 继续拿 bypass 做 layer mapping 查表 key

`StageMappingParser::stageCandidateToStageMapping()` 里：

- `key.accNum = can.accUtil`
- `key.dramBypass = can.dramBypassList[li]`
- `key.spmBypass = can.spmBypassList[li]`
- 然后用这个 key 去 `layer->pipeLayerMappingTable.find(key)`

对 lazy fetch，它还会：

- 复制一份 `keyLazy`
- 把非 stage IO tensor 的 bypass 清零
- 再查一次 `pipeLayerMappingTable`

直接结论：

- `HybridMapper` 关于“访存类型”的决定，
  会一路传到 `MudnacSim` 的具体 `LayerMapping` 选择。
- 因而当前语义核查不能只停留在
  `TensorTypeRuntime = DRAM / ISOLATE_SPM / ...`
  这一层，
  还必须把
  `mapping type -> bypass key -> runtime layer mapping lookup`
  这一整条链一起核查。

### 4.6 runtime artifact 生成规则

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/StageRecord.py`
- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`

#### 4.6.1 `IO_NOBUFFER` 不会进入 runtime entry/export 列表

关键路径：

- `StageCandidateFactory.create_stage_candidate()`
  - `entryTensorIdList/exportTensorIdList`
    会过滤 `get_not_runtime_buffer()`
- `get_not_runtime_buffer()` 当前只包含 `IO_NOBUFFER`

含义：

- `IO_NOBUFFER` 是 mapping-time 类型，
  但不是 runtime stage pipe buffer 类型。

#### 4.6.2 `ALL_RINGBUFFER` 的 pipe binding 会被置零，并追加独立 `RING` binding

关键路径：

- 对 entry/export tensor 生成 `PIPE` binding 时：
  - 如果 `tensor_type == "ALL_RINGBUFFER"`
  - 则：
    - `slot_count = 0`
    - `pages_per_slot = 0`
- 随后再根据 `ring_buffer_count/ring_buffer_size_per`
  追加一条 `kind="RING"` 的 binding

这非常关键：

- artifact 层已经明确表达了
  “`ALL_RINGBUFFER` 的 transport storage 不属于 stage 自己的 `PIPE` slots”
- 它应该由独立 ring binding 提供

#### 4.6.3 `SHARED_SPM` 用 alias group 聚合

关键路径：

- 若 `tensor_type == "SHARED_SPM"`
  - 会在 `shared_groups` 里为该 tensor 建 alias group
  - 统一 `slot_count/pages_per_slot`
  - 通过 `alias_group_id` 让多个 stage 共享同一组存储定义

## 5. MudnacSim 语义核查

### 5.1 `TensorStayType` 在源码中的真实作用

`代码明确实现`

文件：

- `conference/MudnacSim/src/runtime/mapping.cpp`
- `conference/MudnacSim/src/runtime/runtime.cpp`

源码能直接证明的事实只有这些：

- `StageMapping::stringToTensorStayType()` / `tensorStayTypeToString()`
  负责在字符串和枚举之间做一一映射。
- `StageMapping::getTensorStayType()` /
  `isDRAMPipeBuffer()` /
  `isDRAMDepenPipeBuffer()` /
  `isIsolatePipeBuffer()` /
  `isSharedPipeBuffer()` /
  `isAllRingbufferPipeBuffer()`
  只是基于 entry/export 列表中的枚举值做分类。
- `StageMappingParser::stageCandidateToStageMapping()`
  把 yaml 中的
  `entryTensorTypeList/exportTensorTypeList`
  解析成这些枚举。

也就是说：

- 从 `MudnacSim` 源码本身，能直接看到的是
  “这些名字如何被解析和分流”。
- 但**不能**仅凭枚举名就下结论说
  `DRAM` 一定是边界、
  `ALL_RINGBUFFER` 一定只会出现在边界内或边界外；
  这类更强语义必须看后续执行路径和映射生成代码。

### 5.2 拓扑边界是如何在源码里被识别的

`代码明确实现`

文件：

- `conference/MudnacSim/src/runtime/mapping.cpp`
- `conference/MudnacSim/src/runtime/pipeline.cpp`

`SegmentMapping::createMap()` 会独立计算：

- `entryTensorNoIndegree`
- `exportTensorNoOutdegree`
- `input_tensor_id_stage`
- `output_tensor_id_stage`

计算依据不是 `TensorStayType`，
而是：

- 某个 entry tensor 有没有对应的 producer stage
- 某个 export tensor 有没有对应的 consumer stage

目前我在 `MudnacSim` 源码里明确看到的使用点：

- `Pipeline::getTotalBatchProcessed()`
  会遍历 `output_tensor_id_stage`
  来计算整个 pipeline 已完成的 batch 数

本轮没有在 `MudnacSim` 源码里找到的东西：

- 没找到“boundary tensor 必须是 `DRAM`”的硬断言
- 没找到“boundary tensor 不能是 `ALL_RINGBUFFER`”的硬断言
- 没找到“中间 stage 必须至少有一个 `ISOLATE_SPM/SHARED_SPM` entry”的硬断言

因此当前更准确的说法是：

- `MudnacSim` 源码会**独立识别**拓扑边界；
- 但本轮静态核查没有发现它把“边界集合”和
  `TensorStayType`
  做了完全硬绑定。

### 5.3 创建 pipe buffer / ring buffer 的类型分流

`代码明确实现`

文件：

- `conference/MudnacSim/src/runtime/pipeline.cpp`
- `conference/MudnacSim/src/runtime/runtime.cpp`

#### 5.3.1 `ALL_RINGBUFFER` 不分配本地 `pSpmPages`

`createPipebuffer()` 中：

- 若 `tensor_type != ALL_RINGBUFFER`
  - 从 `in_stage_spm_pages_` 取本地 `pSpmPages`
- 若 `tensor_type == ALL_RINGBUFFER`
  - `pipe_buffer.pSpmPages[0/1]` 只初始化为空 `PageSet`

这和 HybridMapper artifact 中
`PIPE slot_count=0 pages_per_slot=0`
完全一致。

#### 5.3.2 各类型进入不同运行索引

`createPipebuffer()` 中：

| 类型 | 索引到的路径 |
| --- | --- |
| `DRAM` / `DRAM_DEPEN` | `tensorId2Entry/ExportDramOrDramDepenPipeBuffer` |
| `ISOLATE_SPM` | 视有无 ring，进入无 ring send/recv 路径或带 ring 的 isolate 路径 |
| `SHARED_SPM` | `tensorId2SharedPipeBuffer` |
| `ALL_RINGBUFFER` | `tensorId2EntryAllRingbufferPipebuffer` / `tensorId2ExportAllRingbufferPipebuffer` |

#### 5.3.3 ring buffer 的分配与 shape/stride 选择

`runtime.cpp` 中，ring buffer 物理资源分配会先看该 tensor 在 owner stages 中是否出现：

- `isAllRingbufferPipeBuffer()` 或 `isSharedPipeBuffer()`：
  `use_max_size_in_ringbuffer = true`
- `isDRAMDepenPipeBuffer()`：
  `is_dram_depen = true`，随后直接 `continue`，
  即 DRAM_DEPEN 不在 SPM 中为 ring 分配物理页

`pipeline.cpp` 中，真正创建 `RingBuffer` 对象时再分三类：

- 进入 `tensorId2Entry/ExportDramOrDramDepenPipeBuffer` 的 tensor：
  - 创建 DRAM ring
  - 用 `min shape / min strides`
  - 底层存的是 DRAM 地址区间
- 进入
  `tensorId2IsolatePipeBufferNoRingbuffer` /
  `tensorId2SharedPipeBuffer` /
  `tensorId2EntryIsolatePipeBufferWithRingbuffer` /
  `tensorId2ExportIsolatePipeBufferWithRingbuffer`
  的 tensor：
  - 创建 SPM ring
  - 用 `min shape / min strides`
- 进入
  `tensorId2EntryAllRingbufferPipebuffer` /
  `tensorId2ExportAllRingbufferPipebuffer`
  的 tensor：
  - 创建 SPM ring
  - 用 `max shape / max strides`

这也与 HybridMapper 的 `TYPE_PURE_DECOUPLING` 使用 `max size` 对应上了。

需要特别强调的一点：

- `createRingBuffer()` 的第二个分支把
  `tensorId2SharedPipeBuffer`
  也算进去了；
  也就是说如果 `ring_buffer_config_` 里真的出现某个 `SHARED_SPM` tensor，
  源码会给它创建一个 min-size 的 SPM ring。
- 但后面的执行态**没有**
  `processSharedSpmPipebufferWithRingbuffer()` 这类处理函数。
- 所以从当前源码看，
  `SHARED_SPM + ring_buffer_config_`
  不是一个有完整执行态支撑的独立路径。
- 这也是为什么本文件后面会把
  `SHARED_SPM`
  写成“当前执行态只有 no-ring handler”。

### 5.4 各类型的执行路径

`代码明确实现`

文件：

- `conference/MudnacSim/src/runtime/pipeline.cpp`

#### 5.4.1 `DRAM`

入口执行代码：

- `processEntryDramPipebuffer()`
- 当 `pipe_buffer->tensorStayType == DRAM` 时，
  等 batch 足够后，
  直接从 `dramBaseAddr[buffer_idx]` 发 `fetch`
- fetch 完成后 buffer 变 full

出口执行代码：

- `processExportDramPipebuffer()`
- 当 `pipe_buffer->tensorStayType == DRAM` 时，
  有完整数据后，
  直接向 `dramBaseAddr[buffer_idx]` 发 `flush`
- flush 完成后 buffer 变 empty

源码要点：

- `DRAM` 和 `DRAM_DEPEN`
  共享同一套 entry/export 处理函数。
- 在 fetch/flush 完成后的公共收尾逻辑里，
  若 `use_ring_buffer_` 为真，
  仍会调用 `ring_buffer_->use()` / `fill()`。
- 因而仅从 `MudnacSim` 执行源码看，
  不能把 `DRAM` 简化成
  “绝不会带 ring 的边界类型”；
  更准确的说法是：
  当前 `DRAM` 分支在 issue 阶段直接使用 `dramBaseAddr`，
  而不是 ring 提供的地址。

#### 5.4.2 `DRAM_DEPEN`

入口：

- 仍走 `processEntryDramPipebuffer()`
- 但要先等 `ring_buffer_->hasReadyBuffer(subBatchOffset)`
- 之后从 `ring_buffer_->getDramBaseAddr()` 发 `fetch`

出口：

- 仍走 `processExportDramPipebuffer()`
- 发 `flush` 前要先等 `ring_buffer_->hasIdleBuffer()`
- flush 完成后 `ring_buffer_->fill(subBatchOffset)`

源码要点：

- 依然有本地 stage buffer
- 运行时依赖 `ring_buffer_`
  提供 DRAM 地址和 ready/idle 条件
- `processEntryDramPipebuffer()` 的 `DRAM_DEPEN` 分支
  会显式 `assert(pipe_buffer->ring_buffer_ != nullptr)`
  且 `assert(pipe_buffer->use_ring_buffer_)`
- 因而对当前源码来说，
  `DRAM_DEPEN` 是“带本地 stage buffer 的 ring-gated DRAM 路径”，
  不是 pure ring transport

#### 5.4.3 `ISOLATE_SPM`

无 ring：

- `processIsolateSpmPipebufferWithoutRingbuffer()`
- 当前后 stage 的 buffer 状态满足条件时，
  在 producer 的本地 SPM pages 和 consumer 的本地 SPM pages 之间发 `send`

有 ring：

- entry 走 `processEntryIsolateSpmPipebufferWithRingbuffer()`
  - 从 ring slot pages 发送到本地 stage buffer
  - 完成后 `ring_buffer_->use()`
- export 走 `processExportIsolateSpmPipebufferWithRingbuffer()`
  - 从本地 stage buffer 发送到 ring slot pages
  - 完成后 `ring_buffer_->fill()`

源码要点：

- `ISOLATE_SPM` 的核心语义仍是“producer/consumer 各自有本地 buffer”
- ring 只是跨 depth gap 的 transport 辅助
- 它不同于 `ALL_RINGBUFFER`

#### 5.4.4 `SHARED_SPM`

当前执行路径：

- `processSharedSpmPipebufferWithoutRingbuffer()`

源码事实：

- producer 和 consumer 共享同一组 SPM buffer
- pipeline 只做状态转移，不发 send/flush/fetch
- 通过 `tag` 和 `bufferHasFullData` 在共享 buffer 上做握手

源码要点：

- 共享的是同一块 SPM 存储
- `processSharedSpmPipebufferWithoutRingbuffer()`
  会显式 `assert(pre_pipe_buffer->withDoubleBuffer)`
  且对每个 consumer 也 `assert(nxt_pipe_buffer->withDoubleBuffer)`
- 所以“`SHARED_SPM` 需要 double buffer”
  这一条在当前执行态里是**真实硬约束**
- 当前执行态没有
  `processSharedSpmPipebufferWithRingbuffer()`；
  因而从源码看，
  `SHARED_SPM` 的实际处理路径只有这个 no-ring 状态机

#### 5.4.5 `ALL_RINGBUFFER`

入口：

- `processEntryAllRingbuffer()`
- 若 ring 对应 offset ready：
  - 直接把 `pipe_buffer->pSpmPages[0]`
    指到 `ring_buffer_->getPSpmPages(offset)`
  - stage 消费完成后调用 `ring_buffer_->use(offset)`

出口：

- `processExportAllRingbuffer()`
- 初次或下一轮使用时：
  - 直接把 `pipe_buffer->pSpmPages[0]`
    指到 `ring_buffer_->getPSpmPages(offset)`
- stage 生产完成后调用 `ring_buffer_->fill(offset)`

源码要点：

- `ALL_RINGBUFFER` 不是
  “本地 stage buffer + ring 备份”
- 而是：
  `pipe_buffer` 只作为一个控制壳，
  真正的数据页直接来自 ring slot

## 6. 各类型在 HybridMapper 与 MudnacSim 之间的对应关系

| HybridMapper meta/mapping | Runtime type | MudnacSim 当前源码执行路径 | 是否有本地 stage buffer | ring 结论 |
| --- | --- | --- | --- | --- |
| `TYPE_IO: IO_SINGLE/DOUBLE` | `DRAM` | 走 `processEntry/ExportDramPipebuffer()` 的 `DRAM` 分支，issue 时直接用 `dramBaseAddr` | 有 | 当前 HybridMapper 生成路径不会为 `TYPE_IO` 生成 `ring_buffer_count`；MudnacSim 源码本身不靠枚举名硬禁止带 ring |
| `TYPE_IO: IO_NOBUFFER` | 不进入 runtime entry/export | 直接绕过 runtime pipe buffer | 无 | 否 |
| `TYPE_NORMAL_DRAM: INTER_DRAM_SINGLE/DOUBLE` | `DRAM_DEPEN` | 仍走 DRAM entry/export 函数，但 issue 前要看 ring ready/idle，DRAM 地址也来自 ring | 有 | 当前源码执行分支要求 ring 存在 |
| `TYPE_NORMAL_SPM: INTER_SINGLE/DOUBLE` | `ISOLATE_SPM` | 无 ring 时 stage-to-stage `send`；有 ring 时在本地 buffer 与 ring slot 之间 `send` | 有 | depth gap 大时 HybridMapper 会生成 ring |
| `TYPE_SHARED: INTER_SHARED_WRITE/READ` | `SHARED_SPM` | 当前执行态只有 `processSharedSpmPipebufferWithoutRingbuffer()`，通过共享页和 tag 做状态转移 | 共享页集合，不是各 stage 独立副本 | 当前 HybridMapper 不直接给 `TYPE_SHARED` 生成 ring；MudnacSim 对 shared+ring 没有独立 handler |
| `TYPE_PURE_DECOUPLING: INTER_PURE_DECOUPLING` | `ALL_RINGBUFFER` | 走 `processEntry/ExportAllRingbuffer()`，pipe buffer 直接指向 ring slot pages | 无独立本地存储 | 当前源码执行分支要求 ring 存在 |

补充说明：

- 上表里的 `Runtime type` 只是 `TensorType` 的一部分投影。
- 同一个 `TensorType` 在 `HybridMapper` 里还同时决定：
  - DRAM/NOC 周期归类
  - 是否先计入 DRAM bytes
  - `dramBypass/spmBypass`
  - 是否进入 runtime entry/export
  - 是否 double-buffer
- 其中 `dramBypass/spmBypass` 又会继续参与：
  - artifact 生成时的 runtime layout 匹配
  - `MudnacSim` 里 `pipeLayerMappingTable` 的查表
- 因而如果只看运行时 `TensorStayType` 字符串，
  会漏掉一大半真正决定执行路径的前置约束。

## 7. `address/address2` 的语义

### 7.1 HybridMapper 侧

`代码明确实现`

文件：

- `conference/HybridMapper/HybridMapper/Model.py`

关键实现：

- `dataAddrMap` 先为每个 tensor 分配一遍地址
- `dataAddrMap2` 再为每个 tensor 分配第二遍地址
- 写 `model.layers.yaml` 时，
  每个 layer slot 只是引用
  `self.dataAddrMap[data]`
  和
  `self.dataAddrMap2[data]`

直接结论：

- `address/address2` 是面向 logical tensor 的两套全局 alias 视图。
- 它不是“每个 layer slot 自己私有的一份 storage”。

### 7.2 MudnacSim 侧

`代码明确实现`

文件：

- `conference/MudnacSim/src/runtime/model_loader.cpp`
- `conference/MudnacSim/src/runtime/runtime.cpp`

关键实现：

- loader 会同时装载 layer slot 上的 `address` 和 `address2`
- `StageMappingParser::stageCandidateToStageMapping()`
  会把同一个 tensor id 对应的两套地址
  记录到
  `stage_mapping.tensorId2DramBaseAddr[tensorId] = {addr, addr2}`

直接结论：

- 从 `MudnacSim` 运行时实现看，
  `address/address2`
  会被当作同一 tensor id 的两套 DRAM base address 视图一起装入。
- 这支撑“它们不是两个不同 logical tensor”这个结论。
- 但仅靠 `MudnacSim` 源码，
  我不再把它表述成更强的
  “一定是 double-buffer 语义”；
  更严格的说法是：
  runtime 维护的是
  `tensor id -> {addr, addr2}`
  这对地址。

## 8. 当前 pipeline-runtime 的偏离点

### 8.1 对 `ALL_RINGBUFFER` 的分类本身并没有错

`代码明确实现`

文件：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`

关键实现：

- `classify_kind()`
  - `ALL_RINGBUFFER -> C7_ENTRY_ALL_RING / C8_EXPORT_ALL_RING`
- `stage_tensor_current_pages()`
  - 对 `C7/C8`，
    若本地 `slot_pages` 为空，
    会回退到 `ring->slot_pages[...]`

说明：

- 当前 runtime 认识到这是一种独立类型。
- 问题不在“字符串没识别”。

### 8.2 真正错位在 export alias materialization

`代码明确实现`

关键路径：

- `sync_stage_export_aliases()`
  - 对 export tensor，只要满足
    `spm_bypass == 0 && local_spm_page_count > 0`
    就会进入 alias sync 路径
- `copy_tensor_pages_to_model_aliases()`
  - 以 `tensor_id` 为键扫描整个 model 的所有 layer slot
  - 对命中的 `address/address2`
    全部执行 copy
- `get_layer_tensor_source_slice()`
  - source truth 仍然是 layer slot 的 `address/address2`

这意味着：

- runtime 当前仍在把
  “某个 tensor id 的 export 结果”
  理解成
  “应 materialize 回整个 model 中所有该 tensor 的 alias”
- 对 `ALL_RINGBUFFER` 这种内部 transport tensor，
  这就与 `MudnacSim` 的 ring-only 语义发生冲突了

### 8.3 runtime 还没有加载 tensor-level canonical storage plan

`代码明确实现`

文件：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_yaml_loader.c`

关键实现：

- `prt_load_model_yaml()` 结束时：
  - `out->num_tensors = 0`
  - `out->tensors = NULL`

含义：

- 当前 runtime 并没有单独维护“逻辑 tensor 级别的 canonical storage ownership plan”。
- 它更像是直接依赖 layer-local alias 视图来推断 source/destination。

这进一步解释了为什么：

- 当前实现容易把内部 transport tensor
  错误 materialize 回 layer alias。

### 8.4 当前 worktree 的 dedup patch 只能算止血

`代码明确实现`

现状：

- `copy_tensor_pages_to_model_aliases()`
  当前会按 `(dst_addr, expect_size)` 做去重，
  遇到重复目标就跳过。

我对这条 patch 的判断：

- 它可以避免“同一地址被重复 copy”
  这类表面问题。
- 但它没有回答真正的语义问题：
  - 哪些 tensor 应该 materialize
  - materialize 到哪一层 alias 才是合法的
  - 哪些内部 transport tensor 根本不该 materialize

所以：

- 这条 patch 不能当最终修复。

## 9. 当前最合理的语义解释

`当前推断`

当前最一致的解释是：

- `HybridMapper`
  已经把 `INTER_PURE_DECOUPLING`
  表达成
  “segment 内部纯 ring transport”
- `MudnacSim`
  也按这个语义执行
- 但 `pipeline-runtime`
  仍残留着一种更偏 layer-alias/materialize-first 的实现假设
- 因而一旦遇到内部 `ALL_RINGBUFFER` tensor，
  它就会把本应只在 ring 中周转的临时运输结果，
  又拉回 alias/export 路径

## 10. 后续建议核查点

以下是建议继续核查的方向，不是本文件下结论说已经修好：

- 在 artifact 生成阶段增加硬校验：
  - segment 首 stage entry 不允许 `ALL_RINGBUFFER`
  - segment 末 stage export 不允许 `ALL_RINGBUFFER`
- 在 runtime 侧显式区分：
  - 逻辑 tensor 的 canonical storage
  - transport buffer/ring buffer
- 对 `ALL_RINGBUFFER` 的 export alias sync 增加语义门控：
  - 内部 ring-only transport tensor 不应无条件 materialize
- 如果后续发现 HybridMapper 以外的后处理脚本会改写 yaml，
  还要继续核查：
  - 是否有脚本可能把合法的内部 `ALL_RINGBUFFER`
    错误推到 segment 边界

## 11. 本轮引用文件

- `conference/HybridMapper/HybridMapper/StageRecord.py`
- `conference/HybridMapper/HybridMapper/SASearch.py`
- `conference/HybridMapper/HybridMapper/Model.py`
- `conference/HybridMapper/scripts/create-test-graph-partition.py`
- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`
- `conference/MudnacSim/src/runtime/mapping.h`
- `conference/MudnacSim/src/runtime/mapping.cpp`
- `conference/MudnacSim/src/runtime/runtime.cpp`
- `conference/MudnacSim/src/runtime/pipeline.cpp`
- `conference/MudnacSim/src/runtime/pipeline_buffer.h`
- `conference/MudnacSim/src/runtime/pipeline_buffer.cpp`
- `conference/MudnacSim/src/runtime/model_loader.cpp`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_yaml_loader.c`
- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml`
- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.ours2.yaml`
