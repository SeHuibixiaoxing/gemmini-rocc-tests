# HybridMapper / MudnacSim / pipeline-runtime Buffer 类型执行语义矩阵

更新时间：`2026-04-10`

## 1. 文档目标

这份文档只做静态源码核查，目标是把以下三件事整理清楚：

1. `HybridMapper + MudnacSim` 这条基线里，各个 buffer 类型作为输入 / 输出时，执行逻辑是什么。
2. 各个 buffer 类型作为输入 / 输出时，片上 `SPM` 或 `DRAM` 空间怎么分配，空间大小由什么决定。
3. 当前 `pipeline-runtime` worktree 里的实现到底是什么，它和基线相比有哪些一致点、有哪些偏差。

本文刻意把三类来源分开：

- `HybridMapper`：决定 tensor 访存类型、ring 是否存在、stage / shared / ring 的页数需求，以及 runtime artifact 的 `buffer binding`。
- `MudnacSim`：定义基线执行语义，以及基线 SPM / DRAM 物理资源分配方式。
- 当前 `pipeline-runtime`：描述当前 worktree 的真实实现，不替它做“应然语义”假设。

对 `MudnacSim`，本文只以源码执行路径为证据，不引用代码注释作为结论依据。

## 2. 术语和读法

- `输入 / 输出` 指 runtime stage 的 `entry tensor` / `export tensor`。
- `本地 SPM` 指 stage 自己持有的 `PIPE` pages。
- `ring slot` 指独立 `RING` binding 对应的每个 slot。
- `DRAM ring` 指 ring 里存的是一组 DRAM 地址区间，而不是 SPM 页。
- `SPM ring` 指 ring 里直接持有 SPM 页。
- `层执行时看到什么`，指网络层真正读写的页集合来自哪里。

本文中的“基线”含义是：

- 类型和大小约束，以 `HybridMapper` 源码和 `create-pipeline-runtime-artifacts.py` 的产物规则为准。
- 执行和物理分配语义，以 `MudnacSim` 源码为准。

## 3. HybridMapper 侧：类型来源与约束

### 3.1 Mapping type 到 runtime type 的投影

源码锚点：

- `conference/HybridMapper/HybridMapper/StageRecord.py:120`
- `conference/HybridMapper/HybridMapper/StageRecord.py:126`
- `conference/HybridMapper/HybridMapper/StageRecord.py:132`
- `conference/HybridMapper/HybridMapper/StageRecord.py:139`
- `conference/HybridMapper/HybridMapper/StageRecord.py:146`
- `conference/HybridMapper/HybridMapper/StageRecord.py:208`

| HybridMapper mapping type | runtime type |
| --- | --- |
| `IO_SINGLE` / `IO_DOUBLE` | `DRAM` |
| `INTER_DRAM_SINGLE` / `INTER_DRAM_DOUBLE` | `DRAM_DEPEN` |
| `INTER_SINGLE` / `INTER_DOUBLE` | `ISOLATE_SPM` |
| `INTER_SHARED_WRITE` / `INTER_SHARED_READ` | `SHARED_SPM` |
| `INTER_PURE_DECOUPLING` | `ALL_RINGBUFFER` |
| `IO_NOBUFFER` | 不进入 runtime entry/export buffer |

### 3.2 HybridMapper 决定访存类型时，同时决定了哪些约束

源码锚点：

- `conference/HybridMapper/HybridMapper/StageRecord.py:89`
- `conference/HybridMapper/HybridMapper/StageRecord.py:96`
- `conference/HybridMapper/HybridMapper/StageRecord.py:103`
- `conference/HybridMapper/HybridMapper/StageRecord.py:115`
- `conference/HybridMapper/HybridMapper/StageRecord.py:150`

同一个 `TensorType` 在 `HybridMapper` 里不是只决定一个 runtime type。它还同时决定：

| 约束项 | 由谁决定 | 当前源码含义 |
| --- | --- | --- |
| 是否进 runtime `entry/exportTensorIdList` | `get_not_runtime_buffer()` | 只有 `IO_NOBUFFER` 被过滤掉 |
| 是否 double buffer | `get_runtime_with_doublebuffer()` / `get_runtime_without_doublebuffer()` | `IO_DOUBLE` / `INTER_DRAM_DOUBLE` / `INTER_DOUBLE` / `INTER_SHARED_*` 为双缓冲；`INTER_PURE_DECOUPLING` 为单缓冲 |
| 是否计入 DRAM add / max | `get_need_dram_add()` / `get_need_dram_max()` | `IO_*`、`INTER_DRAM_*` 走 DRAM 代价模型 |
| 是否计入 NOC add / max | `get_need_noc_add()` / `get_need_noc_max()` | `INTER_*` 走 NOC 代价模型 |
| 是否 `spm_bypass` | `is_spm_bypass()` | 只有 `IO_NOBUFFER` 直接绕过 SPM |
| 是否 `dram_bypass` | `is_dram_bypass()` | 除 `IO_NOBUFFER` 外，其余已知类型都保留 DRAM alias 语义 |

直接结论：

- `HybridMapper` 的“访存类型”不是只决定 transport 类型，它同时决定 runtime buffer 是否存在、buffer 数量、bypass 属性，以及资源估算路径。
- 因此比较 `pipeline-runtime` 和基线时，不能只看字符串类型名，还要看它是否复现了同一组约束。

### 3.3 segment 边界 tensor 的额外约束

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:638`
- `conference/HybridMapper/HybridMapper/SASearch.py:1145`
- `conference/HybridMapper/HybridMapper/SASearch.py:1231`

当前启用的搜索流程里：

- `segment` 边界 tensor 在 `_init_segment_tensor_info()` 里会被初始化为 `TYPE_IO + IO_SINGLE`。
- `_op_change_tensor_meta_type()` 遇到 `TYPE_IO` 直接跳过。
- `_op_change_tensor_type()` 也会跳过 `TYPE_IO`。

这意味着在当前 `HybridMapper` 生成路径里，segment 边界 tensor 不会被改成 `INTER_PURE_DECOUPLING / ALL_RINGBUFFER`。

这条约束和后文的 `ALL_RINGBUFFER` 语义一起看很重要：`ALL_RINGBUFFER` 的设计前提是“纯 transport”，不是 segment 边界 IO。

### 3.4 ring / stage / shared 的大小生成规则

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:305`
- `conference/HybridMapper/HybridMapper/SASearch.py:337`

`HybridMapper` 的大小决定逻辑分三类。

#### 3.4.1 ring 是否存在、每 slot 多大

`SASearch._generate_ring_buffer()` 的规则是：

| meta type | 是否建 ring | `slot_count` | `pages_per_slot` | ring 是否占 SPM |
| --- | --- | --- | --- | --- |
| `TYPE_PURE_DECOUPLING` | 总是建 | `diff + 1` | `ceilPage(max_size)` | 是 |
| `TYPE_NORMAL_SPM` | 只有 `diff > 1` 才建 | `diff + 1` | `ceilPage(min_size)` | 是 |
| `TYPE_NORMAL_DRAM` | 总是建 | `diff + 1` | `ceilPage(min_size)` | 否 |
| `TYPE_SHARED` | 当前生成逻辑不建 | 无 | 无 | 无 |
| `TYPE_IO` | 当前生成逻辑不建 | 无 | 无 | 无 |

其中：

- `diff = 后继最大深度 - 生产者深度`
- `TYPE_NORMAL_DRAM` 的 ring 是“地址环”，所以 `ring_buffer_use_spm = False`
- `TYPE_PURE_DECOUPLING` 使用 `max_size`
- `TYPE_NORMAL_SPM` / `TYPE_NORMAL_DRAM` 使用 `min_size`

#### 3.4.2 stage 本地 SPM 需求怎么定

`SASearch._generate_tensor_spm_need()` 的规则是：

| meta type | stage 本地 SPM 需求 |
| --- | --- |
| `TYPE_IO` | `ceilPage(layer.getDataSize(tid)) * (1 或 2)` |
| `TYPE_NORMAL_DRAM` | `ceilPage(layer.getDataSize(tid)) * (1 或 2)` |
| `TYPE_NORMAL_SPM` | `ceilPage(layer.getDataSize(tid)) * (1 或 2)` |
| `TYPE_SHARED` | `ceilPage(max_size) * 2` |
| `TYPE_PURE_DECOUPLING` | 不额外记 stage-local SPM，只要求 ring 存在 |

直接结论：

- `DRAM`、`DRAM_DEPEN`、`ISOLATE_SPM` 都有 stage-local SPM。
- `SHARED_SPM` 有 stage 可见页，但它的物理页按“共享页集合”统一分配，而且按 `max_size * 2` 计。
- `ALL_RINGBUFFER` 不单独要 stage-local storage。

### 3.5 runtime artifact 里的 buffer binding 是怎么落地的

源码锚点：

- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py:636`
- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py:760`

`create-pipeline-runtime-artifacts.py` 最终会把上面的规则翻译成 runtime 可见的三类 binding：

| binding kind | 规则 |
| --- | --- |
| `PIPE` | 默认 `slot_count = 1/2`，`pages_per_slot = stage.local_page_count[tensor_idx]` |
| `PIPE + SHARED_SPM` | 给相同 tensor 建 `alias_group_id`，组内取最大 `slot_count/pages_per_slot` |
| `PIPE + ALL_RINGBUFFER` | 强制 `slot_count = 0`，`pages_per_slot = 0` |
| `WEIGHT` | `pages_per_slot = local_page_count[tensor_idx]` |
| `RING` | `slot_count = ring_buffer_count[tensor_id]`，`pages_per_slot = ring_buffer_size_per[tensor_id]` |

这里要特别注意两件事：

1. `pipeline-runtime` 最终看到的 `pages_per_slot`，不是运行时重新推导出来的，而是 artifact 固化下来的。
2. 对 `ALL_RINGBUFFER`，artifact 明确表达的是：
   `PIPE` 本身不拥有 slot page，真正的 storage 只在独立 `RING` binding 里。

## 4. 基线与当前实现的逐类型矩阵

## 4.1 `DRAM`

### 4.1.1 基线：`HybridMapper + MudnacSim`

来源：

- `IO_SINGLE` / `IO_DOUBLE -> DRAM`

输入执行逻辑：

- stage entry 会从 `dramBaseAddr` 把数据 fetch 到本地 `pSpmPages[idx]`。
- 网络层执行时读取的是这份 stage-local SPM buffer，不是直接读 DRAM。

输出执行逻辑：

- 网络层先把结果写到本地 `pSpmPages[idx]`。
- stage export 再把这份本地 SPM flush 回 `dramBaseAddr`。

SPM / DRAM 分配逻辑：

- stage-local SPM 页由 `tensorSpmUtilInStage[stage][tensor]` 分配。
- `DRAM` 类型本身没有 ring。
- 对应 DRAM 地址来自 layer 的 model alias / `tensorId2DramBaseAddr`。

空间大小由什么决定：

- stage-local 每个 slot 的页数来自 `ceilPage(layer.getDataSize(tid))`。
- 是否乘 `2` 由 `IO_SINGLE` / `IO_DOUBLE` 决定。
- artifact 里 `PIPE.pages_per_slot = local_page_count[tensor_idx]`。

源码锚点：

- `conference/MudnacSim/src/runtime/pipeline.cpp:420`
- `conference/MudnacSim/src/runtime/pipeline.cpp:532`
- `conference/MudnacSim/src/runtime/runtime.cpp:962`

### 4.1.2 当前 `pipeline-runtime`

输入执行逻辑：

- `classify_kind()` 把 `DRAM` 归到 `C1`。
- `prt_process_c1()` 在 `dram_base_addr != 0` 时，调用 `prt_dma_copy_dram_to_spm_pages()` 把 DRAM 拷到 `slot_pages[idx]`。
- 层执行时，`stage_tensor_current_pages()` 返回的是这份 `slot_pages[in_use_idx]`。

输出执行逻辑：

- `classify_kind()` 把 `DRAM` export 归到 `C2`。
- `prt_process_c2()` 在 `dram_base_addr != 0` 时，调用 `prt_dma_copy_spm_pages_to_dram()` 把 `slot_pages[idx]` flush 回 DRAM。

SPM / DRAM 分配逻辑：

- `PIPE` binding 会按 `slot_count * pages_per_slot` 分配本地 SPM 页。
- `build_topology_from_pipeline()` 会把 model alias 地址解析到 `dram_base_addr[0/1]`。

空间大小由什么决定：

- 物理页分配大小来自 artifact 的 `PIPE.pages_per_slot`。
- 层执行视图大小来自 stage map 的 `local_spm_page_count / local_spm_tensor_bytes`。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:716`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2895`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:226`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:355`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:658`

### 4.1.3 差异

- 对普通 `DRAM` 路径，当前 `pipeline-runtime` 与基线基本一致。
- 代码层面 `DRAM` 和 `DRAM_DEPEN` 被合并到同一个 `C1/C2` 分类里，真正区分靠 `dram_base_addr` 是否被清零。

## 4.2 `DRAM_DEPEN`

### 4.2.1 基线：`HybridMapper + MudnacSim`

来源：

- `INTER_DRAM_SINGLE` / `INTER_DRAM_DOUBLE -> DRAM_DEPEN`
- `HybridMapper` 对 `TYPE_NORMAL_DRAM` 总是建 ring，但 `ring_buffer_use_spm = False`

输入执行逻辑：

- consumer entry 先等待 ring 中对应 subbatch ready。
- ready 之后，不是从 ring slot 的 SPM 页取数据，而是从 `ring_buffer_->getDramBaseAddr(offset)` fetch 到自己的本地 SPM。
- 网络层执行时仍然只读本地 SPM。

输出执行逻辑：

- producer export 在本地 SPM 上产出结果。
- export 时先等待 ring 有 idle slot。
- 然后把本地 SPM flush 到 `ring_buffer_->getDramBaseAddr(offset)`。
- ring 只负责提供“本次该写哪个 DRAM 地址”，不承载数据页。

SPM / DRAM 分配逻辑：

- stage-local SPM 仍然像普通 `DRAM` / `ISOLATE_SPM` 一样按 stage 分配。
- ring 本身不分配 SPM 物理页。
- `createRingBuffer()` 为它申请的是 `dram_addr_manager_` 返回的 DRAM 地址区间。

空间大小由什么决定：

- stage-local 大小：`ceilPage(layer.getDataSize(tid)) * (1 或 2)`。
- ring 每 slot 大小：`ceilPage(min_size)`。
- ring 的 shape / stride 也取 `min_shape / min_strides`。

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:305`
- `conference/MudnacSim/src/runtime/pipeline.cpp:338`
- `conference/MudnacSim/src/runtime/pipeline.cpp:420`
- `conference/MudnacSim/src/runtime/pipeline.cpp:532`
- `conference/MudnacSim/src/runtime/runtime.cpp:1066`

### 4.2.2 当前 `pipeline-runtime`

输入执行逻辑：

- `classify_kind()` 仍把 `DRAM_DEPEN` 归到 `C1`。
- 但 `build_topology_from_pipeline()` 只要看到 `tensor_type == "DRAM_DEPEN" && has_ring`，就把 `dram_base_addr[0/1]` 直接置零。
- `prt_process_c1()` 看到 `buf->ring != NULL && dram_base_addr == 0` 后，会走
  `ring->slot_pages -> buf->slot_pages[idx]`
  的 `SPM -> SPM` 拷贝。

输出执行逻辑：

- `prt_process_c2()` 在同样条件下，会走
  `buf->slot_pages[idx] -> ring->slot_pages[slot]`
  的 `SPM -> SPM` 拷贝。

SPM / DRAM 分配逻辑：

- `RING` binding 在 `prt_action_alloc_spm()` 中总是分配 SPM 页。
- 因此当前 runtime 里的 `DRAM_DEPEN` ring 被具体实现成了一个 `SPM ring`。

空间大小由什么决定：

- `PIPE.pages_per_slot` 仍来自 artifact 的 stage-local `local_page_count`。
- `RING.pages_per_slot` 来自 artifact 的 `ring_buffer_size_per`。
- 当前运行时不会再区分“这是 DRAM 地址环还是 SPM 数据环”。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:3007`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:3050`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:226`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:355`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:658`

### 4.2.3 差异

这是当前最重要的根本性语义偏差：

- 基线：`DRAM_DEPEN` 是“本地 SPM + DRAM 地址环”。
- 当前 runtime：`DRAM_DEPEN` 被实现成了“本地 SPM + SPM 数据环”。

这不是调度细节差异，而是 transport 语义本身变了：

- 基线 ring 不承载数据，只承载地址。
- 当前 ring 直接承载数据页。

额外还有一个实现后果：

- 当前 `prt_dma_copy_spm_pages()` 要求 `src_pages->size == dst_pages->size`。
- 这和基线 `DRAM_DEPEN` 的“local size 与 ring min-size 可以不完全相同”也不一致。

## 4.3 `ISOLATE_SPM`（无 ring）

### 4.3.1 基线：`HybridMapper + MudnacSim`

来源：

- `INTER_SINGLE` / `INTER_DOUBLE -> ISOLATE_SPM`
- 只有当 `diff <= 1` 时，`TYPE_NORMAL_SPM` 才不建 ring

输入执行逻辑：

- consumer entry 自己持有一份本地 SPM buffer。
- producer export 完成后，runtime 发 `send`，把 producer 的本地 SPM 内容送到 consumer 的本地 SPM。
- 网络层执行时，consumer 读的是自己的本地 SPM。

输出执行逻辑：

- producer 把结果写到自己的本地 SPM。
- 没有 DRAM flush，也没有 ring。
- 数据通过 `pre_local_spm -> nxt_local_spm` 直接传输。

SPM / DRAM 分配逻辑：

- producer 和 consumer 各自分配自己的 stage-local SPM。
- 没有 ring 分配，也不需要 DRAM 地址分配。

空间大小由什么决定：

- 本地每个 slot 的页数都来自 `ceilPage(layer.getDataSize(tid))`。
- 双缓冲与否由 `INTER_SINGLE` / `INTER_DOUBLE` 决定。
- 但真正发送时，`MudnacSim` 明确取 `min(src_pages, dst_pages)` 作为实际发送页数。

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:337`
- `conference/MudnacSim/src/runtime/pipeline.cpp:636`
- `conference/MudnacSim/src/runtime/runtime.cpp:962`

### 4.3.2 当前 `pipeline-runtime`

输入 / 输出执行逻辑：

- `classify_kind()` 把无 ring 的 `ISOLATE_SPM` 归到 `C3`。
- `prt_process_c3()` 会在 producer full、consumer empty 时，调用
  `prt_dma_copy_spm_pages(dst=nxt.slot_pages, src=pre.slot_pages)`。
- 数据路径仍然是 `pre_local_spm -> nxt_local_spm`。

SPM / DRAM 分配逻辑：

- producer 和 consumer 的 `PIPE` binding 各自分配本地 SPM 页。
- 不分配 ring，不分配 DRAM transport storage。

空间大小由什么决定：

- 物理页数量来自各自的 `PIPE.pages_per_slot`。
- 但 `prt_dma_copy_spm_pages()` 要求源和目的页数严格相等。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:455`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:970`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:658`

### 4.3.3 差异

- 数据流方向和“各 stage 持有独立本地 SPM”这件事，与基线一致。
- 但当前 runtime 丢失了基线里“按较小一侧发送”的兼容语义。
- 只要 producer / consumer 的本地页数不完全相等，当前实现就会在 DMA helper 处失败。

## 4.4 `ISOLATE_SPM`（有 ring）

### 4.4.1 基线：`HybridMapper + MudnacSim`

来源：

- `INTER_SINGLE` / `INTER_DOUBLE -> ISOLATE_SPM`
- `TYPE_NORMAL_SPM` 只有在 `diff > 1` 时才建 ring

输入执行逻辑：

- consumer entry 等待 ring ready。
- ring ready 后，把 `ring slot -> consumer 本地 SPM`。
- 网络层执行时仍然读取 consumer 本地 SPM。

输出执行逻辑：

- producer export 把结果先写在 producer 本地 SPM。
- export 时等待 ring idle，然后做 `producer 本地 SPM -> ring slot`。

SPM / DRAM 分配逻辑：

- stage-local SPM 仍然按每个 stage 各自分配。
- ring 是独立 `SPM ring`，其每个 slot 都有独立物理页。

空间大小由什么决定：

- 本地 stage buffer 大小：`ceilPage(layer.getDataSize(tid)) * (1 或 2)`。
- ring 每 slot 大小：`ceilPage(min_size)`。
- `MudnacSim` 的执行路径允许 `ring_page_num <= local_page_num`，因此 ring 可以比本地 buffer 小。

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:305`
- `conference/MudnacSim/src/runtime/pipeline.cpp:831`
- `conference/MudnacSim/src/runtime/pipeline.cpp:951`
- `conference/MudnacSim/src/runtime/runtime.cpp:1066`

### 4.4.2 当前 `pipeline-runtime`

输入执行逻辑：

- `classify_kind()` 把有 ring 的 `ISOLATE_SPM entry` 归到 `C5`。
- `prt_process_c5()` 直接做 `ring->slot_pages[slot] -> buf->slot_pages[idx]`。

输出执行逻辑：

- `classify_kind()` 把有 ring 的 `ISOLATE_SPM export` 归到 `C6`。
- `prt_process_c6()` 直接做 `buf->slot_pages[idx] -> ring->slot_pages[slot]`。

SPM / DRAM 分配逻辑：

- `PIPE` binding 分配 stage-local SPM。
- `RING` binding 分配 ring slot 的 SPM 页。

空间大小由什么决定：

- 本地和 ring 的物理页大小都直接取 artifact 的 `pages_per_slot`。
- 实际 DMA helper 仍然要求两侧页数严格相等。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:592`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:617`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:970`

### 4.4.3 差异

- 宏观 transport 形态和基线一致，都是“本地 SPM <-> SPM ring”。
- 但当前 runtime 仍然丢失了基线允许的 `ring size <= local size` 语义。
- 这意味着基线里可接受的 `min-size ring + larger local buffer` 组合，在当前 runtime 里可能直接失败。

## 4.5 `SHARED_SPM`

### 4.5.1 基线：`HybridMapper + MudnacSim`

来源：

- `INTER_SHARED_WRITE` / `INTER_SHARED_READ -> SHARED_SPM`
- 当前 `HybridMapper._generate_ring_buffer()` 不会为 `TYPE_SHARED` 生成 ring

输入执行逻辑：

- consumer entry 和 producer export 指向同一组共享页。
- runtime 不发 DMA。
- `processSharedSpmPipebufferWithoutRingbuffer()` 只靠 `full` 和 `tag` 的状态转移，让 consumer 知道共享页何时可读。
- 网络层执行时，consumer 直接读共享页本身。

输出执行逻辑：

- producer 网络层直接把结果写到共享页。
- stage 执行完成后，runtime 把 consumer 的 `full` 状态置起。

SPM / DRAM 分配逻辑：

- 共享页只分配一组物理页，然后把同一组页句柄分给所有使用该 tensor 的 entry/export stage。
- `AllocSpmAllBankPageInter()` 中是“先分一组 shared_page_sets，再复制引用给所有 stage”。
- `AllocSpmMinDis()` 中会依据 `sharedTensorIsReadFirst` 选择 preferred acc：
  - `read_first = true` 时倾向输入侧 stage
  - 否则倾向输出侧 stage

空间大小由什么决定：

- 共享物理页大小取 `ceilPage(max_size) * 2`。
- `SHARED_SPM` 被强制视为双缓冲。
- 网络层实际访问的 shape / stride 仍来自各自 stage 上的 tensor 视图；共享物理页只是按 `max_size` 兜底。

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:337`
- `conference/MudnacSim/src/runtime/pipeline.cpp:793`
- `conference/MudnacSim/src/runtime/runtime.cpp:1017`
- `conference/MudnacSim/src/runtime/runtime.cpp:1293`

### 4.5.2 当前 `pipeline-runtime`

输入 / 输出执行逻辑：

- `classify_kind()` 把 `SHARED_SPM` 统一归到 `C4`。
- `prt_process_c4()` 不发 DMA，只切换 `full` / `tag`。
- 实际层执行时，各 stage 访问的是物理上同一组页。

SPM / DRAM 分配逻辑：

- artifact 中，相同 shared tensor 的 `PIPE` binding 会共享同一个 `alias_group_id`。
- `prt_action_alloc_spm()` 对同一 `alias_group_id` 只分配一组 `slot_pages`，再复用给组内多个 pipebuf。
- `prt_action_bind_topology()` 绑定完成后，`runtime_shared_aliasing_valid()` 还会检查这些 shared pipebuf 是否真的指向同一批 `ppn`。

空间大小由什么决定：

- 分配大小来自 artifact 中 alias group 聚合后的 `pages_per_slot`，通常是组内最大值。
- 当前 runtime 不重新利用 `sharedTensorIsReadFirst` 之类的放置策略。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:526`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:396`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:658`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:909`

### 4.5.3 差异

- “共享同一组页 + 不发 DMA + 只切状态”这一核心语义，与基线一致。
- 当前 runtime 的主要偏差不在执行状态机，而在放置策略：
  它对 `alias_group_id != 0` 统一使用 `all_gemmini_mgr_ids` 做 preferred managers，没有复现 `MudnacSim` 里基于 `sharedTensorIsReadFirst` 的局部性选择。

## 4.6 `ALL_RINGBUFFER`

### 4.6.1 基线：`HybridMapper + MudnacSim`

来源：

- `INTER_PURE_DECOUPLING -> ALL_RINGBUFFER`
- `TYPE_PURE_DECOUPLING` 总是建 ring
- 当前 `HybridMapper` 的 segment 边界 tensor 不会被生成为这种类型

输入执行逻辑：

- entry 不拥有独立 stage-local data storage。
- `processEntryAllRingbuffer()` 在 ring ready 后，直接把 `pipe_buffer->pSpmPages[0]` 指向当前 ring slot 的页。
- 网络层执行时，consumer 读的就是这组 ring slot 页。

输出执行逻辑：

- export 初次进入时，先把 `pipe_buffer->pSpmPages[0]` 指向 ring 的一个 idle slot。
- 网络层执行时，producer 直接把结果写进这组 ring slot 页。
- stage 完成后，runtime 调 `ring.fill()` 标记该 slot 已填充。

SPM / DRAM 分配逻辑：

- `PIPE` 本身不分配独立本地页。
- 真正的 storage 全在 `RING` binding 对应的 SPM ring 里。

空间大小由什么决定：

- ring 每 slot 大小取 `ceilPage(max_size)`。
- ring 的 shape / stride 取 `max_shape / max_strides`。
- stage 自己的 shape / stride 仍按各自 layer 视图解释这组页。

源码锚点：

- `conference/HybridMapper/HybridMapper/SASearch.py:305`
- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py:636`
- `conference/MudnacSim/src/runtime/pipeline.cpp:222`
- `conference/MudnacSim/src/runtime/pipeline.cpp:338`
- `conference/MudnacSim/src/runtime/pipeline.cpp:1066`
- `conference/MudnacSim/src/runtime/pipeline.cpp:1095`

### 4.6.2 当前 `pipeline-runtime`

输入执行逻辑：

- `classify_kind()` 把 `ALL_RINGBUFFER entry` 归到 `C7`。
- `C7` 不做数据 DMA。
- 当 ring ready 时，`page_list_copy()` 把当前 ring slot 的页描述复制到 `buf->slot_pages[0]`，然后把 `buf->full[0]` 置起。
- 层执行时，`stage_tensor_current_pages()` 优先返回 `slot_pages[0]`；如果它仍为空，则回退到 ring slot 页。

输出执行逻辑：

- `classify_kind()` 把 `ALL_RINGBUFFER export` 归到 `C8`。
- `C8` 也不做数据 DMA。
- 它会把 ring slot 的页描述复制进 `buf->slot_pages[0]`，让层直接在这组页上写。
- stage 完成后通过 `ring_fill_locked()` 发布。

SPM / DRAM 分配逻辑：

- artifact 对 `ALL_RINGBUFFER` 的 `PIPE` binding 明确给出 `slot_count = 0, pages_per_slot = 0`，因此 `prt_action_alloc_spm()` 不会给 pipebuf 额外分配本地页。
- 独立 `RING` binding 会分配 ring slot 的 SPM 页。
- `page_list_copy()` 复制的是页描述数组，不是新分配一份物理页。

空间大小由什么决定：

- ring 物理页大小来自 artifact 的 `RING.pages_per_slot`，通常等于 `ring_buffer_size_per = ceilPage(max_size)`。
- 层执行视图大小仍来自 stage map 的 `local_spm_page_count / local_spm_tensor_bytes`。
- 因而当前 runtime 其实存在两层 size：
  - transport storage 的真实容量：ring slot 页数
  - layer 绑定到 alias window 时的可见页数：stage local page count

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:716`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:1846`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:145`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:701`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:750`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c:658`

### 4.6.3 差异

- “没有独立本地数据页，层直接在 ring 页上读写”这一核心语义，当前 runtime 基本保住了。
- 但表示方式和 `MudnacSim` 不同：
  - 基线：`pSpmPages[0]` 直接指向 ring slot 页
  - 当前 runtime：把 ring slot 的页描述复制到 `slot_pages[0]`
- 这不是物理存储层面的额外复制，但会让“pipebuf 自己也持有一份页描述”。

当前 runtime 还有一个基线没有的风险点：

- `sync_stage_export_aliases()` 对所有 `spm_bypass == 0 && local_spm_page_count > 0` 的 export tensor 都可能做 alias materialization。
- 对 `ALL_RINGBUFFER` 这种“纯内部 transport tensor”，这会把本来只该留在 ring 里的内部结果重新 materialize 回 model alias。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:1846`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2251`

## 4.7 `IO_NOBUFFER`（特例：不是 runtime buffer）

### 4.7.1 基线：`HybridMapper + MudnacSim`

来源：

- `IO_NOBUFFER`

执行逻辑：

- `StageRecord.create_stage_candidate()` 会把它从 runtime `entryTensorIdList/exportTensorIdList` 里过滤掉。
- 它的语义是 `spm_bypass = 1`，不建立 runtime pipe buffer。

分配逻辑：

- 不为它分配 runtime `PIPE` pages。
- 层执行时直接走 model alias / host 侧地址。

源码锚点：

- `conference/HybridMapper/HybridMapper/StageRecord.py:115`
- `conference/HybridMapper/HybridMapper/StageRecord.py:409`

### 4.7.2 当前 `pipeline-runtime`

执行逻辑：

- 对 `spm_bypass == 1` 或 `local_spm_page_count == 0` 的 tensor，`stage_prepare_exec_views()` 不会给它绑定 SPM exec view。
- `stage_tensor_exec_addr()` / `stage_tensor_host_addr()` 会回退到 layer 上的 model alias 地址。

分配逻辑：

- 没有 `PIPE` binding 就不会分配本地 SPM 页。

源码锚点：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:1891`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2045`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2088`

## 5. 差异总结

### 5.1 根本性语义偏差

`DRAM_DEPEN` 是当前最严重的问题。

- 基线：`DRAM_DEPEN = 本地 SPM + DRAM 地址环`
- 当前 runtime：`DRAM_DEPEN = 本地 SPM + SPM 数据环`

这会同时改变：

- ring 承载的对象
- ring 是否占 SPM
- C1/C2 的真实 transport 数据路径
- local pages 与 ring pages 的大小关系约束

### 5.2 兼容性偏差

当前 runtime 的 `prt_dma_copy_spm_pages()` 强制要求源 / 目的页数完全一致，而基线并不是这样：

- `ISOLATE_SPM` 无 ring：基线按 `min(src, dst)` 发送
- `ISOLATE_SPM` 有 ring：基线允许 `ring_page_num <= local_page_num`
- `DRAM_DEPEN` 基线也不是 SPM-to-SPM 拷贝，因此没有这类等页数要求

因此当前 runtime 比基线更脆弱，只要 `min-size ring` 或层间 shape 变化引入页数不一致，就可能直接失败。

### 5.3 放置策略偏差

`SHARED_SPM` 的执行状态机基本一致，但放置策略不一致：

- 基线 `MudnacSim`：会看 `sharedTensorIsReadFirst`
- 当前 runtime：alias group 统一偏向 `all_gemmini_mgr_ids`

这不是功能语义错误，但会改变共享页的空间局部性。

### 5.4 表示方式差异

`ALL_RINGBUFFER` 在两边都保留了“没有独立 stage-local storage，层直接在 ring 页上工作”的大语义，但表示方式不同：

- 基线：pipebuf 直接指到 ring 页
- 当前 runtime：pipebuf 拷一份 ring 页描述

这本身未必错，但它和当前 `sync_stage_export_aliases()` 的无差别 materialization 组合在一起，会把内部 transport tensor 暴露回 model alias。

## 6. 结论

从源码看，当前 `pipeline-runtime` 和基线的一致性可以分成三档：

- `基本一致`：`DRAM`、`SHARED_SPM` 的主状态机
- `大体同路，但兼容性变窄`：`ISOLATE_SPM` 有 ring / 无 ring
- `根本性语义偏离`：`DRAM_DEPEN`

如果后续要按 `HybridMapper + MudnacSim` 对齐实现，修复优先级建议是：

1. 先把 `DRAM_DEPEN` 从 `SPM data ring` 改回 `DRAM address ring`
2. 再补回 `ISOLATE_SPM` 对页数不完全相等场景的兼容
3. 最后收紧 `ALL_RINGBUFFER` 的 export alias materialization 条件
