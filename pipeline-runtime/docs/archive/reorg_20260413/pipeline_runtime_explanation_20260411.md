# pipeline-runtime 工作原理说明

更新时间：`2026-04-11`

## 1. 文档边界

本文档只解释当前 `pipeline-runtime` 的真实实现，不替它补“应然语义”。

本文档范围：

- 设备主路径：`Gemmini + ReRoCC + DMA`
- 当前支持层：`conv`、`resadd`
- 当前 artifact 合同：模型 YAML、pipeline YAML、layer mapping YAML

本文档不做两件事：

- 不把当前实现自动等同于正确语义
- 不替后续修改做代码设计决定

## 2. 术语说明

为避免文档里出现太多未解释英文缩写，这里先统一解释。

- `runtime`
  指当前 `pipeline-runtime` 这套 C 代码运行时系统。
- `stage`
  指一段可调度执行单元。它通常对应一层，或者后续可能对应一个小层组。
- `segment`
  指一个 pipeline 段。当前运行时一次调度一个 segment，对应一个 action。
- `action`
  指 runtime 为一个 segment 生成并持有的一份执行实例，内部包含这次运行分配到的加速器、片上页、执行拓扑和线程状态。
- `subbatch`
  指一个大 batch 被切分后的子批次。流水线会按 subbatch 推进。
- `SPM`
  指片上暂存存储。这里主要是 shared scratchpad，也就是共享片上页空间。
- `DRAM`
  指片外主存地址空间。在 Linux 模式下，很多时候对应主机虚拟地址背后的内存。
- `ring`
  指环形缓冲区。它可能存的是片上页，也可能在正确语义里存的是一组 DRAM 地址。
- `bypass`
  指是否绕过某一级存储。
  `spm_bypass=1` 表示该 tensor 不占用 stage 本地片上执行视图。
  `dram_bypass=1` 表示该 tensor 不走常规 DRAM alias 路径。
- `alias`
  指模型 tensor 在模型 blob 或主机虚拟地址中的别名地址。
- `exec view`
  指 stage 真正执行时看到的本地张量视图，包括起始虚拟页、页数、字节数和地址偏移。
- `Gemmini`
  指卷积和残差加法的计算后端。
- `ReRoCC`
  指用于发控制命令或辅助管理的 RoCC 扩展接口。
- `DMA`
  指直接内存访问搬运通道，用于 DRAM 和 SPM 之间，或 SPM 与 SPM 之间拷贝数据。

## 3. runtime 的整体执行流程

当前 runtime 的真实执行流程可以分成 8 步：

1. 读取模型描述和模型二进制。
2. 读取 pipeline 描述。
3. 校验 layer mapping 与 stage 视图约束是否一致。
4. 为每个 segment 生成一个 `action`。
5. 为这个 `action` 分配加速器 manager、片上页和页表翻译上下文。
6. 按 pipeline artifact 构建 `pipebuf`、`ringbuf`、跨 stage 配对关系。
7. 为每个 stage 启动 worker，按 subbatch 推进各类 buffer 和层执行。
8. 在 segment 完成后回收本次 `action` 的资源。

补充说明：

- 当前实现一次只推进一个 active action。
- 一个 action 内部可以有多个 stage worker 并发运行。

这套结论可参考现有总架构文档：

- `PAPER_SOFTWARE_RUNTIME_ARCHITECTURE.md`

## 4. 核心数据结构

这一节只解释与当前问题直接相关的结构体。

### 4.1 `prt_ringbuf_t`

定义位置：

- `prt_types.h:165-178`

字段说明：

- `segment_idx`
  这个 ring 属于哪个 segment。
- `buffer_id`
  对应 artifact 中哪条 buffer binding。
- `tensor_id`
  这个 ring 服务于哪个 tensor。
- `size`
  ring slot 数量。
- `head`
  当前最老、还没被完全消费掉的 subbatch 偏移。
- `tail`
  当前已经发布完成的尾部偏移。
- `out_degree`
  一个 slot 发布后要被消费多少次，通常对应消费者数量。
- `use_count`
  记录每个 subbatch 还剩多少次消费。
- `slot_pages`
  当前 runtime 中 ring 每个 slot 对应的片上页列表。

当前真实实现里最关键的一点是：

- `prt_ringbuf_t` 只保存 `slot_pages`，也就是“SPM 页环”。
- 它没有保存“每个 slot 对应一个 DRAM 地址”的独立表示。

这也是当前 `DRAM_DEPEN` 偏离基线的根源之一。

### 4.2 `prt_pipebuf_t`

定义位置：

- `prt_types.h:180-214`

字段说明：

- `buffer_id`
  与 artifact 中的 `PIPE` binding 对应。
- `tensor_id`
  这个 buffer 属于哪个 tensor。
- `stage_idx`
  属于哪个 stage。
- `segment_idx`
  属于哪个 segment。
- `is_entry`
  `1` 表示入口 buffer，`0` 表示出口 buffer。
- `kind`
  当前被分到 `C1` 到 `C8` 哪一类执行路径。
- `with_double_buffer`
  是否双缓冲。
- `in_use_idx` / `no_use_idx`
  当前正在给层执行用的是哪一块 buffer，另一块是否可并行搬运。
- `slot_pages`
  自己持有的本地 SPM 页集合。最多两份，对应单缓冲或双缓冲。
- `dram_base_addr`
  若该路径走 DRAM，则这里保存入口 fetch 或出口 flush 的 DRAM 基地址。
- `full`
  当前 slot 是否已经有完整数据可供计算或发送。
- `cmd_running`
  当前是否还有 DMA 或 send/flush/fetch 命令在飞。
- `cmd_count`
  当前未完成命令计数。
- `cmd_acc`
  当前命令用的是哪个 manager。
- `cmd_vrange`
  当前命令绑定了哪个虚拟页区间。
- `ring_cmd_vrange`
  当前 ring 相关命令使用的页区间。
- `subbatch_offset`
  当前这个 buffer 已经推进到哪个 subbatch。
- `tag`
  主要给 `ALL_RINGBUFFER` 这类路径用，用来表示“是否已经绑定到 ring slot”。
- `fanout_total`
  一个出口要发给多少个后继。
- `fanout_pending`
  当前这一份数据还剩多少个后继没拿走。
- `ring`
  若有 ring，则指向共享的 `prt_ringbuf_t`。

### 4.3 `prt_tensor_binding_t`

定义位置：

- `prt_types.h:239-242`

字段说明：

- `tensor_type`
  运行时 tensor 类型字符串，例如 `DRAM`、`ISOLATE_SPM`、`DRAM_DEPEN`、`ALL_RINGBUFFER`。
- `double_buffer`
  是否双缓冲。
- `buffer_id`
  对应哪条 buffer binding。

它是 stage 层面的“逻辑 tensor 角色描述”。

### 4.4 `prt_buffer_binding_t`

定义位置：

- `prt_types.h:244-260`

字段说明：

- `buffer_id`
  全局 buffer 编号。
- `tensor_id`
  这个 binding 服务的 tensor。
- `stage_local_id`
  属于哪个 stage-local 索引；若是全局 ring，常见值为 `0xFFFFFFFF`。
- `is_entry`
  是否 stage 入口。
- `kind`
  `WEIGHT`、`PIPE` 或 `RING`。
- `slot_count`
  slot 数量。
- `pages_per_slot`
  每个 slot 的页数。
- `alias_group_id`
  共享同一物理页集合的 alias group 编号，主要用于 `SHARED_SPM`。

### 4.5 `prt_stage_map_t`

定义位置：

- `prt_types.h:262-304`

这是一份 stage 的静态执行合同。下面逐字段解释。

- `stage_id`
  stage 的全局编号。
- `layer_id`
  这个 stage 对应模型中的哪一层。
- `layer_count`
  这个 stage 里有多少层。当前主路径通常是一层。
- `acc_util`
  这个 stage 计划使用多少个加速器切分执行。
- `acc_util_present`
  `acc_util` 是否在 artifact 中显式给出。
- `num_virtual_acc_ids`
  逻辑上可见的虚拟加速器编号个数。
- `virtual_acc_ids`
  虚拟加速器编号列表。
- `virtual_acc_ids_present`
  虚拟加速器编号是否显式给出。
- `num_physical_acc_ids`
  物理加速器编号个数。
- `physical_acc_ids`
  物理加速器编号列表。
- `physical_acc_ids_present`
  物理加速器编号是否显式给出。
- `split_kind`
  当前 stage 的切分方式，取值见 `PRT_LAYER_SPLIT_*`。
- `num_entry`
  入口 tensor 个数。
- `entry`
  入口 tensor binding 数组。
- `num_export`
  出口 tensor 个数。
- `exports`
  出口 tensor binding 数组。
- `tensor_id_count`
  这个 stage 视角下总共有多少个 tensor 槽位。
- `tensor_ids`
  这些槽位分别对应哪个 tensor id。
- `fix_tensor_count`
  固定 tensor 数量，通常是权重、偏置等。
- `fix_tensor_ids`
  固定 tensor id 列表。
- `inner_isolate_count`
  stage 内部的独立片上 tensor 数量。
- `inner_isolate_ids`
  这些内部独立 tensor 的 id。
- `inner_shared_count`
  stage 内部共享 tensor 数量。
- `inner_shared_ids`
  这些内部共享 tensor 的 id。
- `dram_bypass_count`
  有多少个 tensor 槽位提供了 `dram_bypass` 标志。
- `dram_bypass`
  每个 tensor 槽位是否绕过 DRAM 常规路径。
- `spm_bypass_count`
  有多少个 tensor 槽位提供了 `spm_bypass` 标志。
- `spm_bypass`
  每个 tensor 槽位是否绕过 stage 本地片上执行视图。
- `tensor_usage_count_present`
  是否显式提供使用次数信息。
- `tensor_usage_count_count`
  `tensor_usage_count` 的有效长度。
- `tensor_usage_count`
  每个 tensor 槽位还会被用几次，常用于共享 tensor 生命周期。
- `tensor_lazy_fetch_present`
  是否显式提供 lazy fetch 标志。
- `tensor_lazy_fetch_count`
  `tensor_lazy_fetch` 的有效长度。
- `tensor_lazy_fetch`
  固定 tensor 是否允许延迟首次载入。
- `local_spm_tensor_count`
  stage 本地执行视图中有多少个 tensor 记录了 SPM 布局。
- `local_spm_tensor_addr`
  每个 tensor 在 stage 本地别名窗口中的字节偏移。
- `local_spm_first_vpage`
  每个 tensor 在 stage 执行视图中的首虚拟页编号。
- `local_spm_page_count`
  每个 tensor 的页数。
- `local_spm_tensor_bytes`
  每个 tensor 的精确字节数。
- `local_spm_page_span`
  这个 stage 的本地执行视图一共跨了多少页。
- `exec_base_vpage`
  这个 stage 的执行视图在 action 别名窗口中的页基址。

### 4.6 `prt_segment_desc_t`

定义位置：

- `prt_types.h:313-329`

字段说明：

- `segment_idx`
  segment 编号。
- `subbatch_size`
  这个 segment 每次推进多少 batch。
- `num_stages`
  stage 数量。
- `stages`
  stage map 数组。
- `num_ring_cfg`
  ring 配置条目数。
- `ring_cfgs`
  每个 tensor 的 ring 数量、每 slot 页数、use-count。
- `segment_spm_page_span`
  这个 segment 总共需要的逻辑页跨度。
- `buffer_binding_count`
  buffer binding 数量。
- `buffer_bindings`
  `PIPE`、`WEIGHT`、`RING` 绑定描述。
- `tensor_spm_util_in_stage`
  每个 stage 中各 tensor 的片上页占用。
- `shared_tensor_is_read_first`
  共享 tensor 的访问顺序提示。
- `tensor_spm_util_shared`
  共享 tensor 的总占用。
- `tensor_spm_util_in_ringbuffer`
  ring 占用的总页数。
- `tensor_spm_util_weight`
  权重占用的页数。

## 5. 八类 pipe buffer 是怎么分流的

当前 runtime 用 `classify_kind()` 把逻辑 tensor type 映射到八类执行路径：

- `C1`：入口 `DRAM` / `DRAM_DEPEN`
- `C2`：出口 `DRAM` / `DRAM_DEPEN`
- `C3`：无 ring 的 `ISOLATE_SPM` 前后配对
- `C4`：`SHARED_SPM`
- `C5`：带 ring 的入口 `ISOLATE_SPM`
- `C6`：带 ring 的出口 `ISOLATE_SPM`
- `C7`：入口 `ALL_RINGBUFFER`
- `C8`：出口 `ALL_RINGBUFFER`

源码锚点：

- `prt_types.h:25-34`
- `prt_runtime.c:716-738`

下面按执行逻辑解释。

### 5.1 `C1` 入口 `DRAM` / `DRAM_DEPEN`

当前逻辑：

- 若有 ring 且 `dram_base_addr == 0`，先等待 ring ready。
- 真正搬运时：
  - 若 `dram_base_addr != 0`，从 DRAM 搬到本地 `slot_pages`
  - 若 `dram_base_addr == 0`，从 ring 的 `slot_pages` 搬到本地 `slot_pages`

源码锚点：

- `prt_scheduler.c:226-260`
- `prt_runtime.c:3007-3013`

当前实现含义：

- `DRAM_DEPEN` 一旦有 ring，就会先把 `dram_base_addr` 清零，然后被当成“从 SPM ring 拿数据”。

### 5.2 `C2` 出口 `DRAM` / `DRAM_DEPEN`

当前逻辑：

- 若没有 ring，直接把本地 `slot_pages` flush 到 `dram_base_addr`
- 若有 ring 且 `dram_base_addr == 0`，把本地 `slot_pages` 拷到 ring 的 `slot_pages`

源码锚点：

- `prt_scheduler.c:374-452`
- `prt_runtime.c:3050-3056`

当前实现含义：

- `DRAM_DEPEN` 一旦有 ring，也会被当成“把数据写进 SPM ring”，而不是“把 DRAM 地址发布进地址环”。

### 5.3 `C3` 无 ring 的 `ISOLATE_SPM`

当前逻辑：

- 等待前一 stage 出口 full、后一 stage 入口 empty
- 再调用 `prt_dma_copy_spm_pages()` 从前者本地页直接拷到后者本地页

源码锚点：

- `prt_scheduler.c:455-523`

当前限制：

- `prt_dma_copy_spm_pages()` 强制要求源页数和目的页数完全相等。

源码锚点：

- `prt_dma.c:970-999`

### 5.4 `C4` `SHARED_SPM`

当前逻辑：

- 通过 alias group 让多个 `PIPE` binding 共享同一组页
- 不通过显式 send 拷贝把一份数据搬来搬去

相关分配逻辑锚点：

- `create-pipeline-runtime-artifacts.py:726-737`
- `prt_schedule_action.c:794-845`

### 5.5 `C5/C6` 带 ring 的 `ISOLATE_SPM`

当前真实实现中，这一类仍然基于 SPM 页环：

- ring slot 在 action 资源分配阶段统一分配成 `RING` 页集合
- 入口和出口通过 ring slot 与本地 `slot_pages` 之间做数据拷贝

相关锚点：

- `create-pipeline-runtime-artifacts.py:779-801`
- `prt_schedule_action.c:758-774`

### 5.6 `C7/C8` `ALL_RINGBUFFER`

这两类路径的当前关键特点是：

- 若本地 `slot_pages` 为空，`stage_tensor_current_pages()` 直接返回 ring slot 的页集合
- `C7` 入口通过 `page_list_copy()` 把 ring slot 绑定到本地视图，并在消费后推进 `head`
- `C8` 出口在 ring 有空位时，把 ring slot 先映射进来，层执行直接写进去，再发布给消费者

源码锚点：

- `prt_runtime.c:1846-1879`
- `prt_scheduler.c:701-810`

当前实现含义：

- `ALL_RINGBUFFER` 在 runtime 中确实保留了“纯 ring transport”的大方向。
- 但它的边界约束和 export alias materialization 还需要单独核查。

## 6. 执行视图和地址是怎么来的

### 6.1 `stage_tensor_current_pages()`

作用：

- 给定一个 stage 和 tensor id，返回当前该 tensor 真正应该使用的页集合。

当前规则：

- 若是固定 tensor，则返回 weight 页
- 若是普通 entry/export，则返回 `pipebuf->slot_pages[in_use_idx]`
- 若是 `ALL_RINGBUFFER` 且本地没有独立页，则直接返回 ring slot 页

源码锚点：

- `prt_runtime.c:1846-1879`

### 6.2 `stage_prepare_exec_views()`

作用：

- 把 stage 的本地执行视图绑定到 action 的别名窗口或 host shadow 上。

当前规则：

- 若 `local_spm_tensor_bytes == 0`，回退成 `local_spm_page_count * page_size`
- Linux 设备路径下，会把需要的页集合绑定到 `exec_base_vpage + local_spm_first_vpage`
- 绑定页数总是用 `local_spm_page_count`

源码锚点：

- `prt_runtime.c:1891-2042`

直接含义：

- stage 执行视图真正依赖的是 `local_spm_tensor_addr`、`local_spm_first_vpage`、`local_spm_page_count`、`local_spm_tensor_bytes` 四元组。

### 6.3 `stage_tensor_exec_addr()`

作用：

- 返回 Gemmini 或 CPU fallback 真正拿来读写的执行地址。

当前规则：

- 如果这个 tensor 在本 stage 有本地 SPM 执行视图，返回 stage-local 执行地址
- 否则若某个 pipe buffer 上有 `dram_base_addr`，返回对应 DRAM 地址

源码锚点：

- `prt_runtime.c:2045-2075`

## 7. Gemmini 描述符是怎么构造的

### 7.1 `conv`

`build_stage_conv_desc()` 先做三件事：

1. 从 layer 参数取卷积形状和 stride。
2. 调 `stage_prepare_exec_views()` 绑定执行视图。
3. 调 `stage_tensor_exec_addr()` 取 bias、weight、input、output 地址。

然后把这些信息写进 `prt_gemmini_conv_desc_t`。

源码锚点：

- `prt_runtime.c:3313-3419`

当前很重要的一点：

- 即便 stage 被切分了，地址仍然来自 runtime 自己构造的执行视图，而不是来自外部提前写好的 host alias 指针。

### 7.2 `resadd`

`build_stage_resadd_desc()` 的模式相同：

1. 绑定执行视图
2. 取 A、B、C 地址
3. 从 layer 参数和 tensor size 推导 `I`、`J`
4. 检查三路 stride 是否一致

源码锚点：

- `prt_runtime.c:3422-3495`

## 8. 当前实现与基线语义的主要差异

这一节只列“当前实现事实”，不代表后续一定这样保留。

### 8.1 `DRAM_DEPEN` 当前被实现成 SPM 数据环

当前 runtime 在构建拓扑时，只要看到：

- `tensor_type == "DRAM_DEPEN"`
- 且 `has_ring`

就直接把 `dram_base_addr[0/1]` 清零。

源码锚点：

- `prt_runtime.c:3007-3009`
- `prt_runtime.c:3050-3052`

再结合 `C1/C2` 的逻辑就得到：

- 入口不再从 DRAM 地址读，而是从 ring 的 `slot_pages` 读
- 出口不再往 DRAM 地址写，而是往 ring 的 `slot_pages` 写

这与 `MudnacSim` 基线里的“DRAM 地址环”不同。

### 8.2 当前跨 stage 传输默认按整页全拷

当前几个关键搬运路径都默认按整页大小：

- `pages_bytes_rt()` 直接用 `page_count * page_size`
- `prt_dma_copy_spm_pages()` 要求源页数和目的页数完全相等
- `C2` 的请求字节数直接取 `pages_bytes_rt(slot_pages)`
- `C3` 直接调用 `prt_dma_copy_spm_pages()`

源码锚点：

- `prt_scheduler.c:106-110`
- `prt_scheduler.c:395-410`
- `prt_scheduler.c:481-487`
- `prt_dma.c:970-999`

这意味着：

- 当前 runtime 还没有实现“只搬共同有效前缀字节”的 transport 语义。

### 8.3 `ALL_RINGBUFFER` 已经接近 pure ring transport

这一点要单独说清楚：

- 当前 `ALL_RINGBUFFER` 不再强制拥有独立 stage-local `PIPE` 页
- 运行时也能直接把 ring slot 当执行页返回给 stage

源码锚点：

- `create-pipeline-runtime-artifacts.py:738-740`
- `prt_runtime.c:1859-1865`
- `prt_runtime.c:1870-1876`

所以：

- `ALL_RINGBUFFER` 的主问题不是“它没有 ring 语义”，而是边界约束、导出 alias 行为和部分配套规则仍待收紧。

## 9. 待确认问题

接下来建议围绕这些问题继续讨论：

1. 当前 `pipeline-runtime` 里，哪些字段用户最需要先看懂，是否需要把某些结构体单独拆成附录。
2. `C5/C6` 的文档是否还需要再细分成“ring 自身推进逻辑”和“与 Gemmini 执行交错逻辑”两层。
3. `ALL_RINGBUFFER` 的 export alias materialization，在说明文档里要不要单独成节，而不是只在差异里提一句。
4. 是否需要再追加一份“从 YAML 字段到运行时对象”的映射表。

