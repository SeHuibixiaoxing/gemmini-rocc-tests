# Multi-Action Runtime

## 1. 目标

最终目标不是“action 排队”，而是：

- 每个 action 独立 VA window
- 每个 action 独立 shared-spad 页表
- 每个 action 独立 topology / exec state
- 后续允许多个 active action 并发推进

## 2. 已经完成的部分

当前已经落地：

- `prt_schedule_action_t` 持有 action 私有 `exec`
- `prt_action_exec_t` 承载 pipebuf / ringbuf / pair / stage-thread / shadow / bounce 等执行态
- `build_topology_from_pipeline()` 构建到 `action->exec`
- `prt_action_bind_topology()` 绑定到 `action->exec`
- DMA bounce / stage shadow / lazy-load 状态改为 action 私有
- 每个 action 独立 alias window
- 每个 action 独立 shared-spad 页表和 PTBR backing

## 3. 当前真实能力

当前可以真实声称的是：

- 多个 action 之间已经不再共享同一份 runtime-global topology/execution state
- 不同 action 可以拥有彼此独立的 alias/PT/exec 资源
- 这为后续多 active action 并发打下了结构基础

## 4. 当前还不能声称的能力

当前还不能声称：

- `prt_runtime_run()` 已支持多个 active action 并发推进
- runtime-global trace / fatal / watchdog 已全部 action 化
- hart 到 action 的并发生命周期已经完成
- ReRoCC cfg/opcode 竞争已经解决

所以当前结论只能是：

- `multi-action-ready resource ownership` 已完成
- `multi-active-action execution` 还没有完成

## 5. 为什么现在先不做多 action 验证

因为当前最高优先级 blocker 仍然是：

- `bertmini` 单 action Linux/F2 卡点

如果这个单 action 主线都还没恢复，就不应该先扩展到：

- 多 action worker pool
- 多 action scheduler
- cfg/opcode 竞争策略验证

## 6. 恢复多 action 主线的顺序

建议顺序：

1. 先恢复 `bertmini` 单 action Linux/F2
2. 关掉当前单 action blocker
3. 再把 scheduler 从单 active action 扩到多 active action
4. 最后单独处理 ReRoCC cfg/opcode 竞争
