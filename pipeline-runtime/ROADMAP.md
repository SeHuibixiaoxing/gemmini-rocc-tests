# Pipeline Runtime Roadmap

当前阶段：`action-private runtime refactor completed, but immediate priority is to bring back the previous bertmini path`

当前优先级：

- 暂时先不做多 action 验证
- 先跑通之前的 `bertmini` 单 action 路径
- 先确认旧的 `bertmini` baremetal / host / Linux / F2 主线恢复
- 之后再回到 true multi-active-action execution

## Phase 1. 文档收敛

状态：已完成

- 历史协作文档归档到 `conference/mudnac_hybridmapper_collab_docs/archive/2026Q1/`
- `pipeline-runtime/*.md` 和 `pipeline-runtime/docs/*.md` 成为当前规范文档

## Phase 2. Action 私有地址空间

状态：已完成

- 每个 action 独立 alias window
- 每个 action 独立 shared-spad 页表
- manager 安装按 action 粒度进行

## Phase 3. Action 私有执行态

状态：已完成

- `pipebuf/ringbuf/pairs`
- stage shadow/bounce/lazy state
- stage mapping 和 worker ctx
- weight bindings / topology alloc keys

已经从 `prt_runtime_t` 全局状态迁移到 `action->exec`。

## Phase 4. 先恢复 bertmini 主线

状态：当前最高优先级

需要完成：

- 在新 action-private runtime 上重跑之前的 `bertmini` 路径
- 优先确认旧 `segment0/stage0/pointwise` 卡点是否还在
- 先恢复单 action 的 baremetal / host / Linux / F2 闭环

当前最新验证状态：

- 已在新 runtime 的 Linux/F2 live run 上越过旧的 `matmul-os-biascfg-post-ld` 边界
- 当前新的最深边界是 `matmul-os-biascfg-state` 长日志的半行写出 / 其紧邻位置
- 下一轮单 action 验证前，需要先把 deepest-path 日志拆短，再继续往 `bias mvin3` 和后续 compute path 钻

## Phase 5. 多 active action 调度器

状态：下一步

需要完成：

- 同时存在多个 active action 的 scheduler
- 多 action worker 生命周期
- runtime-global fatal/trace/accounting 的 action 化
- 不同 hart 与 action 的稳定绑定

## Phase 6. ReRoCC 竞争管理

状态：下一步

需要完成：

- cfg/opcode 竞争策略
- Gemmini/DMA lane 占用策略
- 多 hart 并发时的 route management

这部分直接关系到你最终要的“64 核、6 action、6 CPU 并发管理”是否可持续。

## Phase 7. 历史卡死回归

状态：下一步

在新的 action-private runtime 上重新验证：

- 之前 Linux/F2 `segment0 stage0 pointwise` 卡死是否还存在
- 是否能在 baremetal 上更快复现
- 是否是 bias/config_ld/Gemmini path 本身问题，而不是旧的全局状态污染

## Phase 8. 真正的多 CPU/多 action 负载

状态：未开始

进入条件：

- Phase 5 完成
- Phase 6 有可行竞争策略
- Phase 7 证明旧卡点不再被新 runtime 引入
