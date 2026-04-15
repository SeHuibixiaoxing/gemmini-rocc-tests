# Pipeline Runtime Runtime Mechanisms

更新时间：`2026-04-13 16:36 UTC`

## 1. 文档边界

本文档解释当前 `pipeline-runtime` 的真实实现机制，不替它自动补成“理想语义”。

重点覆盖：

- artifact 输入合同
- action / stage / subbatch 执行模型
- shared SPM 与页表机制
- 锁页、alias window、地址空间分配
- manager 分配
- DMA / Gemmini 执行与同步
- 当前测试与观测钩子

## 2. Artifact 输入合同

runtime 当前直接消费：

- 模型 YAML
- pipeline YAML
- layer mapping YAML

编排期至少需要提供：

- segment / stage 拓扑
- stage local SPM 视图
- buffer binding kind / slot / pages
- tensor 地址与大小
- entry / export tensor 列表

runtime 的职责是兑现这些布局，不是重新搜索一套 mapping。

## 3. Action / Stage / Subbatch

- 当前一个 `segment` 对应一个 `action`
- 一个 `action` 持有自己的 topology、页表、alias window、stage 执行态
- 一个 `stage` worker 按 `subbatch` 推进：
  - fixed tensor load
  - entry transport
  - layer execute
  - export / publish

当前虽然已经有 multi-action-ready 的资源分离，但主线仍按单 active action 理解。

## 4. Shared SPM 与页表

当前 shared SPM 相关机制包括：

- stage local SPM view
- `pipebuf`
- `ringbuf`
- `isolate/shared pair`
- action-local shared-spad page-table backing

关键事实：

- 每个 action 拥有独立 alias window
- 每个 action 拥有独立页表 backing
- `spm_xlate` 查表仍依赖连续 alias 范围
- page-table backing 若要求物理连续，不能退回普通匿名页

## 5. 锁页与 host 内存

当前固定策略是：

- 先 prefault
- 再对关键长期缓冲做 targeted `mlock`

这条策略当前通过
`PIPELINE_RUNTIME_MLOCKALL_MODE=2`
固化。

它服务的对象包括：

- 会被 DMA 依赖的 host buffer
- alias / bounce / model blob 一类长期缓冲
- 会影响 `virt_to_phys` 与稳定页驻留的内存

当前主线不把 `MCL_FUTURE` 当作必需前提，也不依赖全进程全地址空间 global lock。

## 6. 地址空间分配

### 6.1 Alias window

- 每个 action 拥有独立 alias VA window
- runtime 负责分配实际 base VA
- 编排期只约束逻辑页与逻辑 view，不直接决定最终 host VA

### 6.2 Page-table backing

- backing 与 alias window 一起服务 `spm_xlate`
- 需要保持 page-table 与 installed manager view 一致

### 6.3 Stage local view

- stage 执行描述符最终使用 action-local 实际地址
- entry / export / buffer transport 都依赖这套 view

## 7. Manager 分配机制

当前硬件侧接口边界：

- Gemmini 走 `custom3`
- Coupled DMA 走 `custom2`

当前固定 profile：

- `NUM_CORES=4`
- `NUM_GEMMINI=12`
- `NUM_DMA=12`
- `PAIR_MANAGER_MODE=1`
- `GEMMINI_BASE_ID=0`
- `DMA_BASE_ID=0`

runtime 当前做的是：

- 根据 action / stage 需求拿到可用 manager
- 把本 action 的 alias / page-table 上下文装到对应 manager 上
- 在 stage 执行时按已经分配好的 manager 发送 Gemmini / DMA 指令

## 8. DMA / Gemmini 执行

当前共享页表打开时，真实路径收敛到 blocking fence 语义。

关键点：

- Linux host buffer 与 SPM 之间 DMA 要按 host page 分 chunk
- 需要时走 bounce
- `completion flag` 也必须走可被硬件访问的物理地址
- 不能把 doneflag polling 当主完成逻辑

Gemmini 当前主线只覆盖：

- `conv`
- `resadd`

## 9. 测试与观测机制

当前主线围绕以下组件组织：

- fixed workflow 脚本
- local / remote freshness
- guest 文件日志
- breadcrumb
- host watchdog

测试与观测的目标不是“日志越多越好”，而是：

- 让 blocker 能被定位
- 同时尽量不改变原始时序

更细节请看：

- [`../testing/observability.md`](../testing/observability.md)
- [`../reference/linux_dma_guardrails.md`](../reference/linux_dma_guardrails.md)
- [`../constraints/hard_constraints.md`](../constraints/hard_constraints.md)
