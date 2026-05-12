# g6 Segment0 Tensor2 Export Page28/Page29 Static Audit 2026-04-20

## 范围

- 本文只审计当前 `pipeline-runtime` 源码、`2026-04-20` 这轮 `g6 bertmini` fresh rerun 的 live artifacts，以及对应 breadcrumb 原始文件。
- 目标不是再复述“这轮看起来停在哪”，而是回答两个更窄的问题：
  1. `page28 after_accounting -> page29 before_v2p` 之间，源码里到底还有哪些真实可能卡住的点。
  2. 为什么之前把结论直接写成“早于 `page29 before_v2p`”并不严谨。

相关证据：

- live captures：
  - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T143126Z-g6-live-capture`
  - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T144843Z-g6-live-capture`
- breadcrumb 原始文件：
  - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T143126Z-g6-live-capture/bertmini-batch8.breadcrumb.bin`
  - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T144843Z-g6-live-capture/bertmini-batch8.breadcrumb.bin`
- 相关源码：
  - `src/prt_runtime.c`
  - `src/prt_dma.c`
  - `src/prt_page_table.c`
  - `src/prt_breadcrumb.c`
  - `src/prt_trigger_log.c`
  - `src/prt_action_queue.c`
  - `src/prt_stage_worker_multi_action.c`

## 先钉死这轮分析适用的执行语义

### 1. segment 之间是串行的

`prt_runtime_run()` 在 `seg_idx` 循环里逐个生成 action、启动该 segment 的 worker、等待该 segment 完成，然后才 release 当前 action 并进入下一个 segment。

所以：

- older run 里出现过的 `segment=3 stage=2 compute-done`
  明确比这轮 `segment=0` 的局部前沿更靠后；
- 但它不能覆盖这轮 fresh rerun 自己的冻结位置。

### 2. 同一个 segment 内，stage 是并行 worker 线程，不是串行复用同一个执行上下文

`stage_worker_main()` 为每个 local stage 启一个线程；线程启动后会：

- `stage_bind_current_thread()` 绑定 CPU affinity
- `prt_stage_worker_get_action()` 解析当前 action
- `prt_log_gate_set_context()` / `prt_log_gate_clear_context()` 设置线程局部上下文

`prt_log_gate` 和 breadcrumb 的 DMA transfer context 都是 `__thread` 变量。

对这轮局部前沿这意味着：

- 当前分析对象是 `segment=0 stage=0`
- `segment0` 本身是单 stage
- 所以“segment3 多 stage 并行”或“别的 stage 把 stage0 的 breadcrumb 覆盖掉”不是这轮局部前沿的主要解释

### 3. 但 breadcrumb 并不是时间 ring，而是哈希槽表

这是这轮最关键的修正点。

`prt_breadcrumb_slot_for_note()` 的 key 只混入：

- `kind`
- `segment_idx`
- `global_stage_id`
- `local_stage_id`
- `subbatch_id`
- `tensor_id`
- `token_id`
- `manager_id`
- `page_idx`

它**不混入**：

- `phase`
- alias `target_seq`

因此：

- 同一 page 的不同 phase 会复用同一个 slot
- 同一 page 的不同 alias target 也会复用同一个 slot
- 再加上总共只有 `63` 个可用哈希槽，天然存在碰撞

所以 breadcrumb 能告诉我们“最近一次稳定写入的某个等价类状态”，但不能把它当成无歧义的时间序列。

## 当前路径确实已经缩到哪里

从 `sync_stage_export_aliases()` 进入当前路径后，`stage0/tensor2` 的 SPM export 走的是：

1. `sync_stage_export_aliases()`
2. `copy_tensor_pages_to_model_aliases()`
3. `copy_tensor_pages_to_model_alias_target()`
4. `prt_dma_copy_spm_pages_to_dram()`

而这轮 live capture 的最后一个**稳定偶数序列** breadcrumb 是：

- `kind=dma`
- `phase=dma_page_after_accounting`
- `seg=0 gstage=0 lstage=0 sb=3`
- `tensor=2 mgr=0 page=28`
- `src=0x40005c00`
- `dst=0x103b31400`
- `aux0=0x400`

这能强力说明：

- `page28` 这一次 page copy 的
  - submit
  - wait
  - cleanup
  - `copied += chunk`
  - `remaining -= chunk`
  都已经完成

但是，它**只能**强力说明到这里。

## `page28 after_accounting -> page29 before_v2p` 的真实代码路径

`page28` 记账之后，下一轮 `while` 进入 `page29` 时，真正发生的事情很少。

direct path 下，`before_v2p` 之前依次只有：

1. 计算：
   - `dst_off`
   - `dst_ptr`
   - `host_page_off`
   - `host_page_room`
   - `src_addr`
   - `chunk`
2. `dma_trigger_export_host("pset", ...)`
3. `dma_chunk_needs_bounce(...)`
4. 如果不 bounce，才到：
   - `dma_trigger_export_host("v2p-b", ...)`
   - `dma_breadcrumb_export_loop_phase(... DMA_PAGE_BEFORE_V2P ...)`
   - `prt_host_virt_to_phys(dst_ptr, &dst_pa)`

换句话说：

- 在 `before_v2p(page29)` 之前，代码里没有新的 DMA 指令
- 也没有新的 RR fence/release
- 也没有新的 page wait

## 对当前 `page29`，这条走廊为什么静态上很难自己“正常卡死”

### 1. `dma_trigger_export_host("pset")` 在这轮配置下是快速返回

`dma_trigger_export_host()` 最终只是调用 `prt_trigger_log_note()`。

而 `prt_trigger_log_note()` 在：

- `PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=0`

时，会在 very early path 直接返回，不会做：

- format line
- ring push
- fd write

所以在当前 run config 下，`pset` 不是一个可信的阻塞点。

### 2. `page29` 不是 sparse page probe 点

`dma_should_sparse_export_page_probe()` 对当前目标要求：

- `stage_idx == 0`
- `tensor_id == 2`
- `copied_bytes == 0`

这些虽然满足，但默认 page 触发条件是：

- `page == 0`
- 或尾页
- 或 `page >= 48`
- 或 `page % 8 == 0`

`page29` 不满足这些条件。

因此当前 `page29` 本身不会因为 sparse page probe 而多出额外进度日志路径。

### 3. `page29` 也不是默认 chunk marker 点

`dma_should_log_export_chunk_marker()` 默认 stride 为 `32`。

所以默认 chunk marker 会打：

- `page0`
- 尾页
- `page32`
- `page64` ...

`page29` 也不在默认 chunk-marker 点上。

### 4. `page29` 当前静态上不该走 bounce path

从 `page28` 的已知地址直接顺推：

- `page28 src=0x40005c00 dst=0x103b31400`
- `page29 src=0x40006000 dst=0x103b31800`
- `chunk=0x400`

而 `dma_chunk_needs_bounce()` 的条件是：

- `bytes >= 64`
- 且 `src_mod64 != dst_mod64`

当前 `page29`：

- `src_mod64 == 0`
- `dst_mod64 == 0`

所以静态上应走 direct path，而不是 bounce path。

## 这轮之前把结论写成“早于 `page29 before_v2p`”为什么不够严谨

### 1. 解码脚本会隐藏 odd `seq`

`decode_prt_breadcrumb.py` 的 `slot_is_populated()` 只把：

- `seq != 0`
- 且 `seq` 为偶数

的 slot 视为稳定可见。

因此 `--all` 看不到 odd slot，不代表该 slot 在原始文件里真的是零。

### 2. 两份 live capture 里，`slot14` 都不是零，而是同一个 odd 半写状态

直接读原始 breadcrumb 文件，`slot14` 在两份 capture 里完全相同：

- `seq=503`
- `kind=dma`
- `phase=116`，即 `dma_submitwait_after_cleanup`
- `seg=0 gstage=0 lstage=0 sb=3`
- `tensor=2 mgr=0`
- `page=19`
- `tok=1176`

odd `seq` 的含义是：

- 采样发生在 writer 做完第一次 `seq++`、但还没做第二次 `seq++` 之间
- 也就是一个典型的 torn / in-flight snapshot

### 3. `slot14` 恰好还是 `page29/token0` 的哈希槽

按当前哈希公式计算：

- `page28 / token0 -> slot17`
- `page29 / token0 -> slot14`
- `page30 / token0 -> slot15`

同时：

- `page19 / token1176 -> slot14`

也就是说：

- `slot14` 既是我们想观察的 `page29` page-level phase 的槽
- 又被另一条 `page19 cleanup` breadcrumb 哈希到了同一个槽

再考虑 slot key 里没有 `target_seq`、没有 `phase`，就会得到一个重要结论：

- 当前 capture 中“`slot14` 没有稳定偶数 page29 记录”
  **不能严格推出**
  “控制流一定还没到 `page29 before_v2p`”

它同样可能是：

- `page29` 的写被碰撞覆盖
- 或者采样时正好处于 torn state

## 因此这轮能说多强

### 可以强说的

- 这轮最后一个**稳定偶数** breadcrumb front 确实是 `page28 after_accounting`
- `page29 before_v2p / after_v2p` 没有以稳定偶数形式出现在当前 ring
- `page29 before_v2p` 之前的正常软件路径非常短，且按当前配置几乎没有可信阻塞 helper

### 不能强说的

- 不能再把当前结论写成：
  “已经证明执行一定停在 `page29 before_v2p` 之前”
- 也不能把“`--all` 里没有 page29”直接当作 page29 未到达的充分证据

## 修正后的当前结论

这轮更准确的表述应当是：

- 当前 run 的**最后稳定 breadcrumb**停在
  `segment=0 stage=0 subbatch=3 tensor=2 page28 dma_page_after_accounting`
- 但 `page29` 相关证据目前是**模糊的**：
  - `slot14` 原始值是 odd torn state
  - `slot14` 还与别的 DMA note 哈希碰撞
  - breadcrumb slot key 又缺少 `phase` 和 alias `target_seq`
- 与此同时，源码静态审计表明：
  **如果执行真的还没到 `before_v2p(page29)`，那它必须卡在一段几乎没有正常阻塞调用的极窄走廊里；这在静态上并不好解释。**

所以当前最合理的综合判断不是“已经证明早于 `before_v2p`”，而是：

- **最后稳定 front 在 `page28 after_accounting`；**
- **`page29` 的 breadcrumb 证据不可信到足以定罪；**
- **正常控制流若真停在 `before_v2p` 之前，静态上缺少有说服力的阻塞点。**

## 对下一步低扰动验证的建议

如果后续要做新一轮低扰动验证，目标不应该再是“继续赌 `slot14` 会不会自己变清楚”，而应该是：

1. 在 `pset -> bounce-decision -> before_v2p` 中间补一个新信号
2. 这个新信号不能继续复用当前 `page29/token0` 的槽等价类

优先方向：

- 给 `page29 direct-path` 增加一个独立 breadcrumb phase，但同时让 key 也变化
  - 例如把 alias target 信息或一个非零 token 编进 note
- 或者改用 `TraceV / workerpc` 这类直接看 PC 的方法，
  避免继续依赖当前这个会碰撞、会 torn 的 breadcrumb slot

在用户尚未批准新 rerun 前，这轮静态审计的结论就到这里。
