# Pipeline Runtime 后续优化扩展草案

更新时间：`2026-05-07 UTC`

本文记录
[`pipeline_runtime_optimization_actions_20260505_draft.md`](pipeline_runtime_optimization_actions_20260505_draft.md)
中的正确性、可调试性和基础 P0/P1 问题修复后，才适合继续推进的优化扩展。

原则：

- 正确性修复优先于 overlap 性能优化。
- 默认路径必须保守、可调试、可复现；实验路径必须显式开关。
- 每个扩展点都要写清依赖、当前限制、建议方案和验收标准。
- 后续新增问题按 `EXT-XXX` 追加，不覆盖旧结论；如果结论变化，在条目末尾追加“更新记录”。

状态标签：

- `draft`：问题已记录，方案未定。
- `blocked`：依赖前置修复或硬件能力。
- `ready`：方案清楚，可进入实现。
- `in-progress`：正在实现或验证。
- `validated`：已通过指定验证。
- `deferred`：已记录，但当前明确不做，等待前置证据或窗口。
- `rejected`：明确不做，并写清原因。

## 追加模板

```text
## EXT-XXX：标题

状态：`draft`
来源：
- 原始问题/讨论：
- 关联文档：

目标：
- ...

当前限制：
- ...

建议方向：
- ...

前置条件：
- ...

验收标准：
- ...

更新记录：
- YYYY-MM-DD：...
```

## EXT-001：同一 manager 上 DMA/Gemmini overlap 与分别 fence

状态：`draft`

来源：

- 关联原草案：P1 `action 级资源所有权`
- 原始问题：如果层间流水中两个后台线程分别控制同一个 manager，一个线程监控当前 batch
  的 Gemmini 计算，另一个线程传输上一/下一 batch 的 DMA，当前是否能并行并分别 fence。

目标：

- 在基础正确性稳定后，评估是否支持同一 manager 上 DMA 与 Gemmini 的受控 overlap。
- 若支持，需要给出明确的软件所有权模型、fence 语义和硬件状态/完成语义。
- 若不支持，runtime 应 fail-fast 或回退到串行，避免隐藏的数据竞争和不可解释卡死。

当前结论：

- 当前 runtime 不应把“同一 manager 上 DMA/Gemmini overlap 且由两个 helper/线程分别 fence”
  当成安全执行模型。
- 这不是证明硬件物理上绝对不能 overlap，而是当前软件抽象无法给出安全边界。
- 当前更稳妥的做法是：同一 manager 默认串行；需要 overlap 时优先使用不同 manager。

当前限制：

- Gemmini 和 DMA 都经由 ReRoCC manager 路由，底层 helper 会 acquire/release scope。
- Gemmini fence、ReRoCC fence、DMA fence 都更接近 manager/scope 级 drain，
  不是完整的 per-thread 或 per-batch 隔离。
- DMA completion flag 是 per token 观测点，但当前 `hw_dma_fence()` 等的是 DMA manager idle；
  这不能直接支持同 manager 多 outstanding token 的独立完成语义。
- 两个线程若独立 acquire/release/fence 同一个 manager，可能互相影响 opcode binding、
  ReRoCC cfg scope、SPM xlate 状态和 manager drain 顺序。
- 当前 `async_experimental` 的 issue/prefetch/fence 路径是同一个 stage worker 内的受控顺序，
  不能等价为两个后台线程同时控制同一 manager。

建议方向：

- 首选方案：DMA 与 Gemmini 使用不同 manager，并由 artifact 显式声明 manager、SPM slot/page、
  ring/double-buffer 的冲突关系。
- 保守同 manager 方案：实现 action/stage 级 manager owner。只有 owner 持有 scope，
  后台 helper 不能直接 acquire/release/fence，只能提交请求给 owner 统一排序、issue、drain。
- 如果要支持真正同 manager overlap，需要定义 shared scope：
  - action/stage 开始时绑定 manager ownership；
  - opcode binding 和 SPM xlate 在 action/stage 生命周期内稳定；
  - DMA/Gemmini 请求进入同一个调度器；
  - fence API 明确区分 `dma token done`、`gemmini queue done`、`rr scope drained`、
    `manager idle`；
  - release 只允许在 action/stage drain 后统一执行。
- 硬件/CSR 方向需要补充：
  - per-manager DMA status/idle/error CSR；
  - per-token 或至少 bounded outstanding DMA 标识；
  - Gemmini queue/busy 状态可观测；
  - 能区分 DMA engine idle 与整个 manager idle。

前置条件：

- P1 action 级 ownership 已收敛，所有 DMA/Gemmini manager 使用都经过统一授权。
- SPM binding 已稳定化，stage 期间不再通过重绑物理页实现 buffer 轮转。
- DMA completion 策略已明确，能解释 completion flag、DMA idle 和 manager idle 的关系。
- no-DMA compute 二分和基础 DMA 路径均已有可复现的 F2/metasim 结果。

验收标准：

- 默认构建中，同一 manager 的 DMA/Gemmini overlap 未显式声明时 fail-fast 或强制串行。
- 日志能输出 action/stage manager ownership 表、overlap 冲突表和实际执行模式。
- 两个 helper/线程不能直接对同一 manager 独立 acquire/release scope。
- 若启用实验 overlap，必须能分别证明：
  - DMA token completion 不被 Gemmini fence 误判；
  - Gemmini completion 不被 DMA fence 误判；
  - action release 前 manager 已统一 drain；
  - SPM slot/page 无读写冲突或冲突由 artifact 显式声明。
- 至少覆盖以下回归：
  - same-manager serialized baseline；
  - different-manager DMA/Gemmini overlap；
  - same-manager overlap 实验负载；
  - F2 或 metasim 现场证据，包含 breadcrumb/trace 和 completion/fence 分流信息。

更新记录：

- 2026-05-07：首次记录。当前判断为未来扩展，不作为当前 P1 修复默认目标。

## EXT-002：DMA completion 中长期替代策略

状态：`deferred`

来源：

- 关联原草案：P1 `DMA completion 策略`
- 原始问题：是否可以抛弃 `rerocc_coupleddma_set_dst` 中 completion flag 地址，
  改为让 pipeline-runtime 等 DMA 模块空闲或状态完成。

当前决策：

- 当前先不做中长期改造。
- 短期仍保留 completion flag，并把它作为与 `hw_dma_fence()`、DMA monitor/idle
  交叉验证的观测点。
- 不在当前 P1 修复中删除 completion flag，也不默认切换到 wait-by-idle。

目标：

- 在基础 DMA/Gemmini 正确性稳定后，评估 completion flag 是否可以被更直接的
  DMA status/idle/token 语义替代。
- 降低 Linux/F2 下 host PA completion flag 可见性、cache/coherence、TL Put
  观测不清带来的调试复杂度。
- 为未来 DMA overlap 或 bounded outstanding DMA 提供更明确的完成语义。

中期方向：

- 增加 per-manager DMA status / idle / error CSR。
- 限制同一 DMA manager outstanding token 数，或引入 token id。
- 增加实验 backend：通过 DMA idle/status 等待完成，而不是只依赖 completion flag。
- wait path 同时记录：
  - `hw_dma_fence()` 返回；
  - completion flag 是否置位；
  - DMA status/idle/error CSR；
  - DMA monitor counters。

长期方向：

- 只有在 F2、baremetal、metasim 都证明 DMA idle/status 足够精确后，才考虑删除
  completion flag。
- 如果支持多 outstanding DMA，必须有 token 级完成或严格的提交/完成顺序约束；
  否则 wait-by-idle 只能用于单 outstanding 保守路径。
- 长期目标不是简单删 flag，而是把 completion 语义从“host memory flag 侧效应”
  升级为“硬件可诊断状态 + 软件合同”。

前置条件：

- 当前 blocking DMA 路径在 F2 上有可复现通过/失败证据。
- completion flag、`hw_dma_fence()`、DMA monitor 三者的分流关系已记录清楚。
- no-DMA compute 和基础 DMA copy 二分完成，能区分 Gemmini/SPM/xlate 与 DMA completion 问题。
- 如果涉及 overlap，EXT-001 的 ownership/shared-scope 约束已完成或明确实验隔离。

验收标准：

- 默认路径仍保持 completion flag + fence 的保守行为，直到新 backend 被显式启用。
- 实验 wait-by-idle backend 必须能在单 outstanding DMA 下通过 baremetal、metasim、F2
  对照测试。
- 对任何 flag/status 不一致情况，日志必须能说明是 flag 未见、DMA 未 idle、DMA error
  还是 manager/scope 未 drain。
- 删除 completion flag 之前，必须有跨平台证据和回滚开关。

更新记录：

- 2026-05-07：首次记录。当前明确 deferred，不进入当前修复范围。
