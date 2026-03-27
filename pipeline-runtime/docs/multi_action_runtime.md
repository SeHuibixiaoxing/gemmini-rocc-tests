# Multi-Action Runtime

## 1. 目标

目标不是简单把多个 action 放进队列，而是做到：

- 每个 action 独立地址空间
- 每个 action 独立 shared-spad 页表
- 每个 action 独立拓扑与执行态
- 最终可以让多个 active action 并发推进

## 2. 已完成的部分

当前已落地：

- `prt_schedule_action_t` 新增 `exec`
- `prt_action_exec_t` 承载 action 私有执行态
- `prt_runtime_current_exec()` / `prt_runtime_current_exec_const()`
- `build_topology_from_pipeline()` 构建到 `action->exec`
- `prt_action_bind_topology()` 绑定到 `action->exec`
- `runtime_release_topology()` 只释放当前 action 的 exec 内容
- DMA stage bounce 改为 action 私有
- resadd fallback / scheduler shared-pair / stage worker 都改为走当前 action exec
- `runtime_stage_map()` 优先使用 `action->pipeline_segment_ref`

## 3. 当前真实能力

当前真实能力是：

- 多个 action 可以拥有彼此独立的 xlate/PT/exec 资源
- 运行线程能通过 TLS/current-action 解析到自己服务的 action
- runtime 不再依赖全局唯一 pipebuf/ringbuf/stage shadow

## 4. 还没完成的部分

还没有完成：

- `prt_runtime_run()` 同时推进多个 active action
- action 级 trace/fatal/watchdog
- 多 action worker pool / 生命周期
- hart 到 action 的完整并发调度
- cfg/opcode 竞争管理

所以现在不能声称已经支持“多个 active action 并发 runtime”。

## 5. 为什么这轮重构仍然必要

因为旧实现里：

- xlate/PT 已经 action 私有
- 但 topology/worker/shadow 仍是 runtime 全局

这会导致：

- action 间资源污染
- Linux/F2 卡点定位被旧全局状态干扰
- 后续 scheduler 无法安全扩展到多 active action

本轮已经把这个结构性障碍去掉。

## 6. 下一步建议

1. 先把 scheduler 从“单 active action”改成“多 active action”
2. 再把 trace/fatal/watchdog 变成 action 级
3. 最后单独处理 ReRoCC cfg/opcode 竞争
