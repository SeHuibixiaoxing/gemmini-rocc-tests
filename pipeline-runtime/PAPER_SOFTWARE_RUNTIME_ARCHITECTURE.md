# 面向任务编排的 Pipeline Runtime 软件系统架构

## 1. 文档边界

本文档整理当前 `pipeline-runtime` 的软件实现事实，用作论文“智能计算芯片任务编排方法原型系统”章节的软件侧底稿。本文档**以当前 C 代码实现为主**，现有 `README.md`、`ARCHITECTURE.md` 和历史协作文档只作为命名和背景参考，不作为最终真实性来源。

本文档关注的核心问题是：

- runtime 到底消费什么编排期产物
- runtime 如何组织 action、stage、buffer 和线程
- runtime 如何在 Linux 用户态管理 shared-spad
- runtime 如何把 Gemmini / ReRoCC / Coupled DMA 接成一条可执行链路

## 2. 代码事实来源

本文档主要依据以下源码整理：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_types.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_runtime.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_page_table.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_schedule_action.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_rerocc.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_yaml_loader.h`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/main.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_action_queue.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_action_exec.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_stage_worker_multi_action.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_async_dma.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c`

## 3. Runtime 的定位与输入合同

### 3.1 Runtime 的基本定位

从 `main.c` 和 `prt_runtime.c` 可以直接确认，当前 runtime 的定位不是做“运行时重新编排”，而是执行以下流程：

1. 读取模型描述 YAML
2. 读取 pipeline 描述 YAML
3. 验证 Gemmini layer mapping artifacts
4. 按 segment 生成 action
5. 为 action 分配物理 manager 和 shared-spad 页
6. 构建执行拓扑
7. 启动 stage worker 执行

因此，当前 runtime 的核心职责是：

- 消费编排期已经导出的 `pre-orchestrated mapping`
- 在执行期兑现这些逻辑布局和资源需求

### 3.2 输入数据结构

`prt_types.h` 中定义了 runtime 的核心输入合同：

- `prt_model_desc_t`
  描述模型层、张量、张量地址和张量尺寸
- `prt_pipeline_desc_t`
  描述整个 pipeline 由多少个 segment 组成
- `prt_segment_desc_t`
  描述一个 segment 的 stage 集合、ring 配置、SPM 页跨度和 buffer binding
- `prt_stage_map_t`
  描述某个 stage 的逻辑执行视图
- `prt_buffer_binding_t`
  描述某个 buffer 的 kind、slot 数、每 slot 页数和 alias group
- `prt_ring_cfg_t`
  描述某个 tensor 的 ring buffer 长度和 use-count

这些结构中的关键信息包括：

- `segment_spm_page_span`
- `exec_base_vpage`
- `local_spm_first_vpage`
- `local_spm_page_count`
- `local_spm_tensor_addr`
- `spm_bypass`
- `dram_bypass`
- `tensor_lazy_fetch`
- `buffer_binding kind / slot_count / pages_per_slot / alias_group_id`

这意味着编排期输出给 runtime 的并不是模糊的“任务列表”，而是已经带有**逻辑页布局、stage 局部视图和 buffer 拓扑约束**的执行合同。

### 3.3 CLI 与运行时默认配置

`main.c` 给出了当前 runtime 的默认运行参数：

- `page_size_bytes = 1024`
- `spm_xlate_enable = 1`
- `pages_per_acc = 256`
- `dma_backend = poll_progress_thread`
- `gemmini_mode = async_experimental`
- `sync_mode = async`

但需要注意一个非常重要的代码事实：

- `prt_runtime_init()` 中，如果 `spm_xlate_enable = 1` 且 `sync_mode = async`，runtime 会强制切换到 `blocking_debug`
- 随后进一步把：
  - `dma_backend` 强制改成 `blocking_fence`
  - `gemmini_mode` 强制改成 `blocking_fence`

原因在代码里写得很明确：

> page-granular DMA translation is currently retired synchronously

也就是说，虽然枚举层面保留了异步后端和实验性异步 Gemmini 模式，但**当前开启 shared-spad 页表翻译时，真正运行的是同步/阻塞语义**。这个结论在论文里必须按事实写清楚。

## 4. 顶层对象模型

### 4.1 `prt_runtime_t`

`prt_runtime_t` 是 runtime 的顶层容器，内部同时持有四类状态：

1. 配置与后端选择
   - `cfg`
   - `dma_ops`
   - `gemm_ops`

2. 输入工件
   - `model`
   - `pipeline`
   - `model_blob`

3. 全局运行控制
   - `state_lock`
   - `stop_requested`
   - `fatal_error`
   - trace counters

4. 全局资源池
   - `page_used`
   - `tensor_allocs`
   - `spm_pt_chunks`
   - DMA pending queue
   - action queue

这表明 runtime 不是单纯的“调 Gemmini 的一组函数”，而是一个同时管理：

- 工件加载
- 内存分配
- 控制同步
- 追踪统计
- manager 生命周期

的完整执行时系统。

### 4.2 `prt_schedule_action_t`

当前一个 pipeline segment 对应一个 `prt_schedule_action_t`。它是 runtime 中最关键的中间对象，包含：

- `pipeline_segment_ref`
- `model_ref`
- `acc_source`
- `spm_source`
- `spm_xlate`
- `alias_base_va / alias_bytes`
- `alias_vpage_start / alias_page_count`
- `exec`
- `state`

它同时承载三类信息：

1. 该 action 对应哪个 segment
2. 该 action 被分到哪些物理 manager 和哪些 shared-spad 物理页
3. 该 action 当前的执行态和本地拓扑

### 4.3 `prt_action_exec_t`

`prt_action_exec_t` 是本轮 runtime 重构最核心的结构。它把原本容易写成 runtime-global 的执行态收敛到 action-private：

- `stage_threads`
- `stage_layer_ids / stage_acc_ids / stage_dma_ids`
- `stage_tile_counts / stage_split_kinds / stage_mgr_ids`
- `stage_spm_shadow`
- `stage_dma_bounce`
- `pipebufs`
- `ringbufs`
- `isolate_pairs`
- `shared_pairs`
- `topo_weight_pages`
- `topo_alloc_keys`

从代码上讲，这意味着当前 runtime 已经把“segment 私有的执行状态”封装进了 action，而不是保存在单一全局实例里。

## 5. 按 segment 执行的顶层运行流程

`prt_runtime_run()` 的主路径可以按以下顺序理解：

1. 加载 `model_yaml`
2. 收集模型输入输出 tensor id
3. 加载 `pipeline_yaml`
4. 验证 Gemmini artifacts
5. 对每个 segment：
   - `prt_action_generate()`
   - `prt_action_alloc_acc()`
   - `prt_action_alloc_spm()`
   - `build_topology_from_pipeline()`
   - `runtime_prepare_stage_spm_windows()`
   - `prt_action_bind_topology()`
   - `runtime_flush_spm_xlate()`
   - 启动 stage worker 线程
   - watchdog 轮询 sink buffer 进度
   - 结束后释放 topology 和 action

因此，当前 runtime 的真实执行模式是：

- **一次只推进一个 active action**
- 但这个 action 内部可以有多个 stage worker 并发执行

这个事实与“未来多 active action 并发”不同，论文里不能混写。

## 6. action 的资源分配与安装流程

### 6.1 `prt_action_generate()`

该步骤只做最小初始化：

- 分配 action 对象
- 分配唯一 `action_id`
- 绑定 segment/model 引用
- 确保 `action->exec` 存在

### 6.2 `prt_action_alloc_acc()`

该步骤依据 `prt_stage_map_t.acc_util` 和可选的 `physical_acc_ids` 分配 manager：

- 为每个 stage 计算 `acc_util`
- 为每个 stage 分配 `gemmini_mgr_ids`
- 为每个 stage 分配 `dma_mgr_ids`
- 记录 action 范围内唯一的 `all_gemmini_mgr_ids`
- 记录 action 范围内唯一的 `all_dma_mgr_ids`

代码中默认采用“同索引 Gemmini manager 与 DMA manager 成对”的策略，即某个 `gm_local` 默认对应同局部编号的 `dm_local`。

因此，当前软件调度的一个基本假设是：

- 计算资源和搬运资源是按 stage 成对分配和使用的

### 6.3 `prt_action_alloc_spm()`

这一步负责分配 action 所需的 shared-spad 物理页和页表上下文，主要做了三件事：

1. 计算该 segment 需要的总页数
2. 若开启 `spm_xlate_enable`：
   - 调用 `alloc_action_alias_window()` 为 action 申请一段连续 alias VA window
   - 调用 `prt_spm_xlate_ctx_alloc()` 为 action 分配私有 PTE backing 与 PTBR
3. 按 `buffer_binding` 为：
   - weight
   - pipe
   - ring
   分配真实物理页列表

这里最重要的一个代码事实是：

- `alias_vpage_start` 被强制设为 `0`

源码注释解释得很清楚：

- 当前硬件根据 `(vaddr - range_base)` 直接计算 PTE index
- 因此 action-local alias window 内的有效页必须从 `vpage 0` 开始
- 旧的“软件全局 offset”对当前 controller 是不可见的

这正是软件运行时要为每个 action 独立分配 alias window 的根本原因。

### 6.4 `prt_action_bind_topology()`

该步骤把 action 已分配的资源灌入 `action->exec`：

- 把 stage 对应的 manager 填到 `stage_acc_ids / stage_dma_ids / stage_mgr_ids`
- 把 ring/pipe/weight 绑定复制到 exec 内部
- 检查 shared aliasing 是否一致
- 调用 `configure_action_spm_xlate()` 把该 action 的 PTBR/PTE/range 下发到所有 Gemmini manager

因此，`alloc_spm()` 解决的是“分到哪些页”，`bind_topology()` 解决的是“这些页如何成为可执行拓扑的一部分”。

## 7. 并发框架与线程模型

### 7.1 当前并发框架

从代码实现上看，runtime 当前采用的是标准的 POSIX 线程并发模型：

- `pthread_create`
- `pthread_mutex_t`
- `pthread_cond_t`
- `_Thread_local`

这不是协程式或事件驱动式 runtime，而是显式线程 + 共享状态 + 条件变量同步的实现。

### 7.2 stage worker 线程

每个 stage 对应一个 `prt_stage_thread_ctx_t`，其中保存：

- `stage_id`
- `rt`
- `action`
- `op_kind`
- `conv_desc / resadd_desc`
- `stop`

`prt_runtime_run()` 在绑定完 topology 之后，为每个 stage 创建一个 worker 线程。当前并发的主粒度就是 stage。

### 7.3 TLS 与 hart/action 绑定

`prt_action_queue.c` 中定义了两个 `_Thread_local` 变量：

- `g_prt_tls_runtime`
- `g_prt_tls_action`

并提供：

- `prt_runtime_current_action()`
- `prt_runtime_set_thread_action()`
- `prt_runtime_clear_thread_action()`

`prt_stage_worker_multi_action.c` 还会结合：

- `sched_getcpu()`
- `prt_action_queue_get_for_hart()`

去推断当前线程服务的 action。

这说明当前 runtime 已经具备“线程上下文感知当前 action”的基础设施，即使顶层 `runtime_run()` 还没有真正同时推进多个 active action。

### 7.4 condition variable 驱动的 buffer 同步

`prt_pipebuf_t` 和 `prt_ringbuf_t` 都带有：

- `pthread_mutex_t lock`
- `pthread_cond_t cv`

`prt_scheduler.c` 通过以下等待原语实现调度：

- `prt_pipebuf_wait_full()`
- `prt_pipebuf_wait_empty()`
- `prt_ring_wait_ready()`
- `prt_ring_wait_idle()`

因此，当前 stage 之间的依赖传递不是通过全局调度器消息队列实现，而是通过 buffer 状态和条件变量直接同步。

## 8. Buffer 拓扑与数据流组织

### 8.1 C1 到 C8 的统一物化

runtime 当前支持 `C1..C8` 八类 buffer 语义，它们最终统一物化为四类运行时对象：

- `prt_pipebuf_t`
- `prt_ringbuf_t`
- `prt_isolate_pair_t`
- `prt_shared_pair_t`

`build_topology_from_pipeline()` 会根据：

- stage 的 `entry`
- stage 的 `exports`
- ring 配置
- tensor type

构造出整张执行图。

### 8.2 `prt_pipebuf_t`

`prt_pipebuf_t` 是最常用的 buffer 单元，内部包含：

- `slot_pages[2]`
- `dram_base_addr[2]`
- `full[2]`
- `cmd_running[2]`
- `dma_tokens[2]`
- `cmd_acc[2]`
- `cmd_vrange[2]`
- `ring_cmd_vrange[2]`
- `ring`
- `fanout_total / fanout_pending`

这说明 pipebuf 同时承担了：

- 双缓冲
- DMA token 生命周期
- DRAM/SPM 地址
- ring 关联
- fanout 统计

### 8.3 `prt_ringbuf_t`

`prt_ringbuf_t` 包含：

- `slot_pages`
- `head`
- `tail`
- `out_degree`
- `use_count`

这里的核心设计不是单纯 FIFO，而是“带引用计数的 ring slot 生命周期”，对应代码中的：

- `ring_fill_locked()`
- `ring_use_locked()`

这非常适合论文中描述多 stage 之间基于 ring 的流式衔接。

### 8.4 isolate / shared pair

`build_topology_from_pipeline()` 会在遍历完所有 pipebuf 后，再构造：

- `isolate_pairs`
- `shared_pairs`

这意味着 runtime 不是把 shared/isolate 语义散落在每个 stage 内部，而是显式物化成成对关系。这样后续 scheduler 可以按 pair 执行 shared 或 isolate 的数据推进逻辑。

## 9. 用户态 shared-spad 管理机制

这是当前 runtime 最具有论文价值的部分之一。

### 9.1 alias window 的用户态分配

`alloc_action_alias_window()` 在 Linux 路径下使用：

- `mmap(NULL, alloc_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`

为每个 action 分配一段连续的用户态虚拟地址窗口，并记录：

- `alias_base_va`
- `alias_bytes`
- `alias_alloc`
- `alias_alloc_bytes`

这里的关键点是：

- runtime 并不真正映射 host DRAM 到这段地址
- 这段地址窗口的作用是给 Gemmini shared-spad xlate 提供一个连续的逻辑别名区间

因此，alias window 的本质不是普通 mmap 内存，而是**用户态为加速器建立的逻辑虚拟地址空间**。

### 9.2 PTE backing 与 PTBR 的分配

`prt_spm_xlate_ctx_alloc()` 不直接 `malloc` 一段页表，而是从 `spm_pt_chunks` 池中切片分配。

这个池支持：

- hugetlb anonymous 分配
- hugetlbfs 分配
- 普通 anonymous fallback

并通过：

- `/proc/self/pagemap`
- `prt_host_virt_to_phys()`

把 host 虚拟地址转换为真实物理地址，进而得到：

- `ptbr_pa`

这一步非常关键，因为 Gemmini PTW 读取的是**物理地址可达的 PTE backing**，而不是普通用户态指针。

### 9.3 PTE 的组织方式

`prt_spm_xlate_ctx_t` 保存：

- `pte`
- `pte_count`
- `free_vpages`
- `tensor_maps`
- `ptbr_pa`
- `fault_count`
- `last_fault_vaddr`
- `last_fault_cause`

PTE 写入格式由 `pack_spm_pte()` 完成，核心语义是：

- 用 `page_shift` 将 shared-spad 物理页地址编码进 PTE
- bit0 作为 valid bit

因此，runtime 内部维护的是一个真正可被硬件 PTW 消费的线性页表，而不是仅供软件使用的映射表。

### 9.4 vpage 的保留、绑定和释放

`prt_page_table.c` 为 action-private xlate context 提供了完整的生命周期接口：

- `prt_spm_reserve_vpages_ctx()`
- `prt_spm_release_vpages_ctx()`
- `prt_spm_bind_vpages_ctx()`
- `prt_spm_unbind_vpages_ctx()`
- `prt_spm_map_tensor_ctx()`
- `prt_spm_unmap_tensor_ctx()`
- `prt_spm_translate_range_ctx()`

这意味着 runtime 并不是“每次重建整张页表”，而是支持：

- 预留逻辑虚拟页
- 将逻辑虚拟页绑定到真实 shared-spad 页列表
- 按 tensor 管理映射
- 按需释放

从方法论上看，这就是一套运行时私有的、用户态可编程的 shared-spad 虚拟内存管理器。

### 9.5 stage 执行视图的安装

`stage_prepare_exec_views()` 是 shared-spad 管理落到 stage 执行的关键函数。它做了两件事：

1. 对固定 tensor，必要时从模型数据源把内容搬到当前 stage 对应的 shared-spad 页
2. 根据：
   - `stage->exec_base_vpage`
   - `stage->local_spm_first_vpage`
   - `stage->local_spm_page_count`
   把当前 tensor 的页列表绑定到 action 的页表中对应的执行视图位置

这一绑定完成后，`stage_tensor_exec_addr()` 在 RISC-V 路径下会返回：

- `alias_base_va + exec_base_vpage * page_size + local_spm_tensor_addr`

也就是说，stage 里的 Gemmini kernel 看到的是一个普通的用户态地址，而 runtime 已经保证：

- 这段地址落在 action 的 alias window 中
- 它对应的 vpage 已经绑定到正确的 shared-spad 物理页

这正是“如何在用户态管理 shared-spad”的核心答案。

## 10. Runtime 与硬件接口适配层

### 10.1 ReRoCC scope 封装

`prt_rerocc.c` 对底层 `rerocc_control.h` 进行了 runtime 级包装，提供：

- `prt_rr_acquire_scope()`
- `prt_rr_fence_scope()`
- `prt_rr_release_scope()`

并且为 shared-spad xlate 单独封装了：

- `prt_gemmini_spm_xlate_cfg()`
- `prt_gemmini_spm_xlate_range()`
- `prt_gemmini_spm_xlate_flush()`
- `prt_gemmini_spm_xlate_fault_read()`

一个很重要的实现细节是：

- xlate 指令会临时 acquire 固定 cfg，并把 opcode 3 绑定到目标 Gemmini manager
- 指令执行后再恢复原来的 opcode 绑定

因此，runtime 可以把 xlate 编程写成“manager-scoped 操作”，而不是全局静态写寄存器。

### 10.2 Gemmini adapter

`prt_gemmini_adapter.c` 负责把 runtime 的高层 task 变成 Gemmini 调用。其职责包括：

- 选择 blocking 或 async Gemmini 后端
- 将 `prt_conv_task_t` 转成具体的 conv / matmul / resadd 调用
- 根据 split kind 执行 OC split、spatial split 等路径
- 在关键位置缓存并打印当前 action 的 shared-spad xlate 配置

它还会判断某个 conv 描述中的：

- input
- weights
- bias
- output

是否落在 shared-spad alias range 中。这说明 shared-spad 地址已经被作为 Gemmini adapter 的一等输入语义，而不是运行时外部的附加状态。

### 10.3 DMA 适配层

`prt_dma.c` 提供两类 DMA backend：

- `blocking_fence`
- `poll_progress_thread`

但在 shared-spad xlate 开启时，当前实际使用的是同步阻塞路径。DMA 层还包含两个重要实现：

1. Linux 用户态物理地址发现
   - 通过 `prt_host_virt_to_phys()`
2. bounce buffer
   - 当 host 指针与 shared-spad 物理页之间的对齐模式不满足 wide transfer 要求时，使用 `stage_dma_bounce` 过渡

这说明 runtime 在用户态发 DMA 时，不仅要管“拷到哪里”，还要处理：

- host 虚拟地址到物理地址的解析
- DMA 对齐限制
- stage 局部 bounce buffer 生命周期

## 11. 当前支持的功能集合

从当前代码能明确确认的能力包括：

- 基于 YAML artifact 的模型和 pipeline 装载
- 以 segment 为粒度生成 action
- 以 stage 为粒度分配 Gemmini/DMA managers
- 为每个 action 建立独立 alias window
- 为每个 action 建立独立 shared-spad 页表上下文和 PTBR
- 按 `buffer_binding` 分配 weight / pipe / ring 三类 shared-spad 页
- 将 `C1..C8` 语义统一物化为 pipebuf/ringbuf/shared/isolate 拓扑
- 基于 `pthread` 的 stage worker 执行框架
- 基于 `_Thread_local` 的 action-local 上下文解析
- Gemmini/ReRoCC/Coupled DMA 的统一适配
- Linux host 路径与 RISC-V/FPGA 路径共享同一套 runtime 主框架

同时，也必须按代码事实说明：

- 当前 `prt_runtime_run()` 一次只推进一个 active action
- shared-spad xlate 开启时，runtime 当前强制走阻塞调试语义

这两点不应被误写成已经完成的多 action 全异步执行能力。

## 12. 对论文写作最有价值的软件侧归纳

从当前实现中，可以提炼出以下几条非常适合写入论文的结论：

- runtime 已经把编排期与执行期职责彻底拆开：编排期决定逻辑布局，执行期只兑现物理资源和地址翻译。
- `prt_schedule_action_t` 将 segment 执行封装为一个可分配、可安装、可释放的独立 action，是任务编排结果进入执行时系统的关键承载体。
- `prt_action_exec_t` 把拓扑、stage 状态、buffer 状态和 bounce buffer 从 runtime-global 迁移到 action-private，为后续真正的多 action 并发执行奠定了结构基础。
- 通过 `mmap + pagemap + PT slice pool + manager-scoped xlate install`，runtime 在 Linux 用户态实现了一套可被硬件 PTW 直接消费的 shared-spad 页表管理机制。
- 通过 `stage_prepare_exec_views()` 与 `stage_tensor_exec_addr()`，逻辑上属于 stage 局部 SPM 的 tensor 被投影到 action alias window 中，使 Gemmini kernel 可以像访问普通地址一样访问 shared-spad 内容。
- 通过统一的 ReRoCC/Gemmini/DMA 适配层，runtime 已经把“调度 manager”“安装页表”“搬运数据”“提交 Gemmini 任务”收束进一套完整的软件执行闭环。
