# Pipeline Runtime Roadmap

## 1. 当前阶段

当前阶段：

- `action-private runtime refactor` 已完成
- 当前最高优先级仍是恢复 `bertmini` 单 action Linux/F2 主线

## 2. 已完成

### Phase A. Action 私有地址空间

已完成：

- 每个 action 独立 alias window
- 每个 action 独立 shared-spad 页表
- manager 安装按 action 粒度进行

### Phase B. Action 私有执行态

已完成：

- `pipebuf / ringbuf / isolate/shared pair`
- stage shadow / bounce / lazy state
- action-local topology alloc keys
- worker 解析当前 action / current exec

## 3. 当前最高优先级

### Phase C. 先恢复 `bertmini` 单 action Linux/F2

当前必须先完成：

1. 先把两条边界分开分析：
   - 主线 file-heavy：
     `segment=31 / resadd_spatial / scope-drain`
   - mixed-log dual-log：
     `segment=2 / export-sync / DMA export`
2. 静态优先审查 export-side 路径：
   - `sync_stage_export_aliases()`
   - `copy_tensor_pages_to_model_aliases()`
   - `prt_dma_copy_spm_pages_to_dram()`
   - `dma_copy_spm_pages_to_host_linux()`
   - `dma_submit_wait_annotated()`
3. 对比 mixed-log
   和 file-heavy
   的日志/刷盘配置差异，
   确认哪些观测性改动可能提前暴露 export DMA 挂点
4. 再决定最便宜的验证方式：
   - 若能构造语义匹配的 export-DMA baremetal repro，
     优先用 baremetal/F2
   - 否则再做下一轮 Linux/F2
5. 如果再跑 Linux/F2，
   必须继续使用 host watchdog
   自动抓
   `uartlog + heartbeat + guest sparse/deep/status`
   并让它自动收口
6. 只有先解释清楚 mixed-log 早期边界之后，
   再回头继续追主线更深的
   `segment=31`
   blocker

## 4. 当前 blocker 关闭后再做的事

### Phase D. 多 active action 调度器

在单 action 主线恢复之后再做：

- 多 active action scheduler
- 多 action worker lifecycle
- runtime-global fatal / trace / watchdog 的 action 化
- hart 到 action 的绑定策略

### Phase E. ReRoCC 竞争管理

在多 active action 之前必须单独设计：

- cfg/opcode 竞争策略
- Gemmini / DMA lane 占用策略
- 多 hart 并发 issue 的冲突模型

## 5. 当前暂缓

以下内容当前暂缓：

- 多 action 动态并发验证
- 面向 64 core / 6 action 的负载优化实验
- 将当前单 action blocker 和未来负载扩展问题混在一起分析
