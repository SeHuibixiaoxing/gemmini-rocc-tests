# Pipeline Runtime Project Guide

更新时间：`2026-04-14 14:40 UTC`

## 1. 项目需求

`pipeline-runtime` 的当前目标不是重新做一套抽象调度器，而是把
`HybridMapper` 导出的 pre-orchestrated mapping
稳定落到
`Gemmini + ReRoCC + CoupledDMA`
执行路径上。

当前主线需求分成 4 层：

1. 语义对齐：
   以 `HybridMapper + MudnacSim` 源码为基线，校正 runtime 的 buffer、地址、transport 与执行语义。
2. 固定 workflow：
   对 `12-pair sbus128` dummy-model 硬件建立不可随意漂移的构建、freshness、FireSim、日志链路。
3. 低扰动调试：
   在 guest 文件日志、breadcrumb、watchdog 约束下继续推进 `bertmini` Linux/F2 主线。
4. 文档与记录：
   把架构、约束、计划、调试、修改分层管理，避免状态漂移和旧文档误导。

当前范围：

- 算子：
  `conv`、`resadd`
- 当前动态主线：
  `pairdummy/sbus128/bertmini batch8 file-only`
- 当前硬件：
  `12 Gemmini + 12 DMA + pair-manager mode`

当前不在本轮主线：

- `pooling` 等未支持算子
- 真正多 active action 并发验证
- `pair-wrapper manager` 硬件路线实现
- real-model correctness closure

## 2. 对标项目与对齐约束

当前 authoritative 语义基线是：

- `HybridMapper`
  决定 tensor 类型、ring 与 slot 需求、stage 视图、artifact 字段
- `MudnacSim`
  定义执行语义、SPM/DRAM 物理分配与 transport 语义

当前必须遵守的对齐结论：

- runtime 只消费 artifact，不在执行期重新推导层内 mapping。
- 当前支持范围只包括 `conv + resadd`。
- `tensor_id` 是 transport 与 buffer 语义的主键；同一 logical tensor 的不同入口/出口一般应共享同一 buffer 语义。
- `ALL_RINGBUFFER` 必须按 pure ring transport 理解，不能在内部 transport 后又被无条件 materialize 回 model alias。
- `DRAM_DEPEN` 不能退化成 “SPM ring” 语义；它仍应保持 `本地 SPM + DRAM 依赖地址` 的基线含义。
- 对当前 `conv + resadd` 范围，尺寸不匹配按编排约束中的 `max/min` 规则处理即可；DMA 只需要正确模拟对应大小的地址拷贝，不承担真实布局变换。
- `resadd` 的双输入大小由模型声明保证一致；runtime 不应再引入额外语义分支。
- 当前不考虑 `pooling` 介入路径。

对应文档：

- [`architecture/alignment_constraints.md`](architecture/alignment_constraints.md)
- [`plans/runtime_alignment_plan.md`](plans/runtime_alignment_plan.md)
- [`plans/hybridmapper_alignment_plan.md`](plans/hybridmapper_alignment_plan.md)

## 3. 现有实现关键机制

### 3.1 Artifact 与执行模型

- runtime 输入合同来自模型 YAML、pipeline YAML、layer mapping YAML。
- 一个 `segment` 当前对应一个 `action`。
- 一个 `action` 内部持有自己的 topology、页表、alias window、stage worker 状态。
- 一个 `stage` 按 `subbatch` 推进，执行固定张量装载、输入 transport、层执行、输出 export。

### 3.2 共享 SPM 存储与地址空间

- 当前 shared SPM 管理以 action-local 视角组织：
  `pipebuf`、`ringbuf`、`isolate/shared pair`、stage local view。
- 每个 action 拥有独立 alias window，不再共享 runtime-global VA 切片。
- 每个 action 还拥有独立的 shared-spad 页表 backing 与安装上下文。
- `spm_xlate` 硬件查表仍依赖连续 alias 范围，因此 alias window 与页表 backing 不能随意漂移。

### 3.3 页表、hugetlb 与锁页

- 只要 page-table backing 依赖“多页物理连续”，就不能退回普通匿名页。
- 当前 page-table backing 必须保留 hugetlb / physically contiguous 语义。
- 当前固定运行策略是
  `PIPELINE_RUNTIME_MLOCKALL_MODE=2`：
  先 prefault，再对关键长期缓冲做 targeted `mlock`。
- 这条策略用于保护会被 DMA、页表翻译或长期 alias 依赖的 host buffer；
  当前不依赖 `MCL_FUTURE` 全局锁页语义。

### 3.4 Manager 分配与执行接口

- 当前硬件把 Gemmini 与 DMA 明确拆成两类 manager：
  - `custom3`：Gemmini / shared-spad xlate 控制
  - `custom2`：Coupled DMA
- 当前固定 profile：
  - `NUM_CORES=4`
  - `NUM_GEMMINI=12`
  - `NUM_DMA=12`
  - `PAIR_MANAGER_MODE=1`
  - `GEMMINI_BASE_ID=0`
  - `DMA_BASE_ID=0`
- stage 只消费已经分配好的 manager，不在运行时重新做复杂布局搜索。

### 3.5 DMA / Gemmini 同步

- `spm_xlate` 打开时，当前真实运行模式会收敛到 blocking fence 路线。
- 当前不允许重新引入 “轮询 doneflag 判 DMA 完成” 作为主语义。
- Linux host buffer 与 SPM 的 DMA 仍要遵守分 chunk、bounce、`virt_to_phys`、completion flag PA 等 guardrail。

### 3.6 日志、breadcrumb 与 watchdog

- 主观测面是 guest 文件系统，不是 `uartlog`。
- 当前日志层次：
  - `bertmini-batch8.log`
  - `bertmini-batch8.deep.log`
  - `bertmini-batch8.status`
  - stage / runner / wrapper crumbs
  - `bertmini-batch8.breadcrumb.bin`
  - `bertmini-batch8.trigger.log`
- wrapper 通过周期性 `sync` 刷盘；runtime 内不应自己做前台阻塞刷盘。
- 热路径优先使用 breadcrumb，而不是高频文本 `write(O_APPEND)`。
- 若 breadcrumb / sparse 仍不能分开相邻边界，
  下一层观测面是 trigger-gated 短日志，
  只在命中目标 family / stage / manager / tensor / page / token 后激活。
- host watchdog 负责抓取 `heartbeat + guest files + breadcrumb` 并自动收口。

### 3.7 测试与调试机制

- 静态校验先于昂贵动态实验。
- 遇到卡点/报错时，默认顺序固定为：
  1. artifact 静态审计
  2. 代码路径静态阅读
  3. breadcrumb / sparse capture 分诊
  4. 只有边界仍不够细时，才加极窄条件日志
- FireMarshal / FireSim / freshness 已被固定脚本链路包住。
- 当前推荐的两个低成本入口：
  - `scripts/audit_pipeline_runtime_artifact.py`
  - `scripts/triage_prt_capture.py`
- `TracerV` 接入 / bring-up 不再临场拼接步骤；
  统一遵守：
  [`testing/tracerv_integration_sop.md`](testing/tracerv_integration_sop.md)
- 当前 rerun 前的固定预检入口：
  - `scripts/pairdummy_sbus128_workflow.sh debug-preflight`
- 当前新的 blocker 排查计划：
  - `docs/plans/blocker_debug_sop_plan.md`
- 每一轮调试必须写 `debug_records/`；
  每一轮实际修改必须写 `change_records/`。

更细机制说明见：

- [`architecture/runtime_mechanisms.md`](architecture/runtime_mechanisms.md)
- [`testing/observability.md`](testing/observability.md)
- [`reference/linux_dma_guardrails.md`](reference/linux_dma_guardrails.md)

## 4. 关键文档地图

### 4.1 Authoritative

- [`project_guide.md`](project_guide.md)
- [`CURRENT_STATUS.md`](CURRENT_STATUS.md)
- [`constraints/hard_constraints.md`](constraints/hard_constraints.md)
- [`workflows/pairdummy_sbus128.md`](workflows/pairdummy_sbus128.md)
- [`../DECISIONS.md`](../DECISIONS.md)
- [`../TESTPLAN.md`](../TESTPLAN.md)
- [`../debug_records/`](../debug_records/)
- [`../change_records/`](../change_records/)

### 4.2 Architecture / Semantics

- [`architecture/runtime_mechanisms.md`](architecture/runtime_mechanisms.md)
- [`architecture/alignment_constraints.md`](architecture/alignment_constraints.md)
- [`../PAPER_SOFTWARE_RUNTIME_ARCHITECTURE.md`](../PAPER_SOFTWARE_RUNTIME_ARCHITECTURE.md)
- [`../PAPER_HARDWARE_ARCHITECTURE.md`](../PAPER_HARDWARE_ARCHITECTURE.md)
- [`multi_action_runtime.md`](multi_action_runtime.md)
- [`pair_wrapper_manager_plan_20260405.md`](pair_wrapper_manager_plan_20260405.md)

### 4.3 Plans

- [`plans/roadmap.md`](plans/roadmap.md)
- [`plans/runtime_alignment_plan.md`](plans/runtime_alignment_plan.md)
- [`plans/hybridmapper_alignment_plan.md`](plans/hybridmapper_alignment_plan.md)
- [`plans/blocker_debug_sop_plan.md`](plans/blocker_debug_sop_plan.md)

### 4.4 Reference / Workflow

- [`workflows/document_workflow.md`](workflows/document_workflow.md)
- [`testing/observability.md`](testing/observability.md)
- [`testing/tracerv_integration_sop.md`](testing/tracerv_integration_sop.md)
- [`reference/linux_dma_guardrails.md`](reference/linux_dma_guardrails.md)

### 4.5 Archive

- [`archive/reorg_20260413/`](archive/reorg_20260413/)
- [`archive/2026Q1_history.md`](archive/2026Q1_history.md)

## 5. 项目现有硬约束

### 5.1 执行流程

- FireMarshal 只走 `scripts/firemarshal-tmux-run.sh`。
- FireSim manager 只走 `scripts/firesim-tmux-run.sh`。
- 任何编译、FireMarshal、FireSim 前先 source FireSim manager 环境。
- `pairdummy/sbus128` 主线默认只认 fixed profile + fixed workflow。
- `infrasetup` / `runworkload` 前必须完成 local / remote freshness。

### 5.2 内存与硬件语义

- 不能用普通匿名页替代需要物理连续的 page-table backing。
- 保留 `PIPELINE_RUNTIME_MLOCKALL_MODE=2` 的 prefault + targeted lock 策略。
- 不要把 `doneflag` polling 重新当成 DMA 完成逻辑。

### 5.3 日志与观测

- 主观测面是 guest 文件日志与 breadcrumb。
- `uartlog` 只作 boot 活性辅助。
- Linux boot 早期只要没有明确报错、panic、crash，
  且 `heartbeat` 仍在推进，
  就先继续等；
  不要把早期静默窗口直接记成新的 blocker。
- 不要在 runtime 热路径上继续堆高频文本探针。
- 仅允许对
  `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
  做受控 overlay，
  用于单变量 trigger rerun；
  其他 fixed-profile 语义参数仍不允许临时漂移。
- 不要在 runtime 内做前台阻塞 `sync`。

### 5.4 记录与代码规范

- 每轮调试单独记录到 `debug_records/<timestamp>.md`。
- 每轮修改单独记录到 `change_records/<timestamp>.md`。
- 架构类文档只在架构变化时更新；
  普通调试不允许把长时间线继续堆回主入口文档。

完整版本见：

- [`constraints/hard_constraints.md`](constraints/hard_constraints.md)

## 6. 当前开发进度

截至 `2026-04-13 16:36 UTC`，当前已经确认：

- stale image、错误入口、boot 静默、hugetlb 前沿、runner 前台 `sync`、doneflag 语义误判、`shared-fence`、`page36->37`、`page42->43`
  都不再是当前 authoritative 稳定 blocker。
- `pairdummy/sbus128` 这条主线已经固化出 fixed profile、fixed workflow、freshness 双闭环、guest-file-first 观测链。
- 最新 capture 说明：
  `v12` 已越过 export `page42/43`，前沿推进到
  `segment=0 / stage=0 / subbatch=5`
  的 pointwise fallback 区域，`mgr=3` 附近出现新的晚期候选卡点。
- 同一 capture 也说明：
  热路径文本探针会让前沿偏移，必须继续坚持低扰动 breadcrumb + 窄条件日志。

最新 authoritative 记录：

- debug：
  [`../debug_records/20260413T152610Z.md`](../debug_records/20260413T152610Z.md)
- debug：
  [`../debug_records/20260413T153040Z.md`](../debug_records/20260413T153040Z.md)
- debug：
  [`../debug_records/20260413T163651Z.md`](../debug_records/20260413T163651Z.md)
- change：
  [`../change_records/20260413T163651Z.md`](../change_records/20260413T163651Z.md)

当前主任务：

1. 用低扰动观测确认 late pointwise / Gemmini 同步区间的真实 blocker。
2. 继续 runtime 与 `HybridMapper + MudnacSim` 的语义对齐。
3. 完成 `bertmini` dummy-model 主线，再回到更深 correctness 问题。

## 7. 文档工作流

当前文档按职责分层：

- `docs/architecture/`
  稳定机制与语义，不写 run-by-run 时间线
- `docs/constraints/`
  强约束与踩坑经验
- `docs/workflows/`
  固定执行流程与文档维护流程
- `docs/testing/`
  测试策略、观测机制、日志策略
- `docs/plans/`
  未完成改造计划
- `docs/reference/`
  守则、工具、辅助参考
- `docs/archive/`
  superseded 文档与旧长时间线
- `debug_records/`
  每轮调试
- `change_records/`
  每轮修改

具体更新规则见：

- [`workflows/document_workflow.md`](workflows/document_workflow.md)
