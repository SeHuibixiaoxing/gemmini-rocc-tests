# Pipeline Runtime Static Findings - 2026-05-05

本文记录等待 `12p4c128sbus32cfg + optimized DMA + current NIC` bitstream 期间的静态深挖结论。
重点是判断当前 gdbserver workflow 是否会触发高风险 overlap，以及后续真正要改的 runtime 结构性问题。

## 1. 当前 cfg32 workflow 实际走保守同步路径

`main.c` 的默认值是：

- `cfg.dma_backend = PRT_DMA_BACKEND_POLL_PROGRESS_THREAD`
- `cfg.gemmini_mode = PRT_GEMMINI_MODE_ASYNC_EXPERIMENTAL`
- `cfg.sync_mode = PRT_SYNC_MODE_ASYNC`

但 `runtime_init` 中有强制降级逻辑：

```text
if sync_mode == ASYNC && spm_xlate_enable:
  sync_mode = BLOCKING_DEBUG
if sync_mode == BLOCKING_DEBUG:
  dma_backend = BLOCKING_FENCE
  gemmini_mode = BLOCKING_FENCE
```

也就是说，在当前 FireMarshal/cfg32 workflow 默认 `spm_xlate_enable=1` 的情况下，实际 F2 路径不是
poll-progress DMA overlap，也不是 Gemmini async overlap，而是：

- DMA: `blocking_fence`
- Gemmini: `blocking_fence`
- 每条 page-granular DMA 在 wait path 中执行 `hw_dma_fence()`；
- 每批 DMA 复用同一个 ReRoCC scope，但每个 token wait 会做 shared fence，最后 batch release。

结论：当前要先跑通的新 cfg32 gdbserver workload，原则上不会主动触发 DMA/Gemmini overlap 竞争。
如果仍卡死，优先看 fixed-load DMA、Gemmini blocking issue/fence、SPM xlate flush、ReRoCC acquire/release，
而不是先怀疑 poll-progress overlap 线程。

## 2. DMA completion flag 在当前 blocking path 不是主完成条件

DMA token 仍然分配 completion flag：

- completion pool 初始化时会 prefault/lock；
- 每个 slot 记录 VA/PA；
- `hw_dma_set_dst(dst, done_flag_pa)` 仍把 done flag PA 交给硬件。

但在 `blocking_fence` wait path 中，主要完成条件是：

1. `dma_completion_flag_refresh(tok)` 读取一次 flag，作为日志/交叉观测；
2. 调 `hw_dma_fence()`；
3. 再 refresh flag；
4. 对 external scope 调 `dma_token_fence_scope(tok)`；
5. release 或由 batch scope 统一 release；
6. `dma_token_complete(tok, PRT_OK)`。

这意味着当前 blocking path 不依赖“轮询 completion flag 变成 1”才能返回。completion flag 在这一路径上更像
诊断信号和硬件接口遗留参数。如果 F2 卡在 DMA wait，关键要在 gdbserver 中看线程是否停在 `hw_dma_fence()`、
`prt_rr_fence_scope()`、`prt_rr_release_scope()`，还是停在 host v2p/prefault 阶段。

风险仍然存在：若以后重新打开 poll-progress backend，completion flag 会重新成为异步进度判断的重要信号。
那时必须重新验证 Linux VA->PA、mlock、cache 一致性和 token 生命周期。

## 3. 当前 workflow 正在强制 direct DMA

`local-freshness` 中渲染出的 guest env 包含：

```text
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1
PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=0
```

代码里 `dma_chunk_needs_bounce()` 在 `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1` 时直接返回 false。
因此当前 workflow 实际不会走软件 bounce path；`DMA_BOUNCE_BYPASS_ENABLE=0` 在 forced-direct 打开时没有机会生效。

这与“构建 optimized DMA 硬件后验证 direct path”的目标一致，但它也是一个重要二分开关：

- 新 cfg32 AGFI 如果卡在 fixed-load/export DMA，可先用 gdbserver 定位是否在 DMA wait；
- 若定位到 DMA data movement 或 fence，可临时把 `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=0` 作为二分实验；
- 若关 direct 后卡点消失，问题偏 optimized DMA/misaligned direct；
- 若关 direct 后仍卡，问题偏 ReRoCC/DMA fence、SPM xlate、manager ownership 或 tensor/page 绑定。

## 4. SPM 页分配已经是 action 级，PTE 绑定仍是运行期重绑

当前 `prt_action_alloc_spm()` 已经按 action 分配 SPM 物理页：

- ring slot pages；
- fixed weight pages；
- pipe/double-buffer slot pages；
- alias group pages；
- action-local alias window；
- private `spm_xlate` context。

`prt_action_bind_topology()` 再把这些 page list clone 到 runtime pipe/ring/weight binding。
这部分已经接近“action 开始前预留物理页”的目标。

但 `stage_prepare_exec_views()` 仍在每次构建 conv/resadd task 时执行运行期 PTE 绑定：

- 根据 `buf->in_use_idx` 或 ring slot 找当前物理 page list；
- 用 `exec_vpage = stage->exec_base_vpage + local_spm_first_vpage[slot]`；
- 调 `prt_spm_bind_vpages_ctx()`；
- 对当前 stage 的 Gemmini managers 调 `runtime_flush_stage_spm_xlate()`。

因此当前行为是“物理页 action 级预留，虚拟地址到物理页的绑定运行期反复更新”。这可以解释为什么现有实现能用同一个
stage-local tensor virtual address 指向当前 double-buffer slot，但它和需求中的最终模型不一致。

长期目标应改成：

- action 开始前为每个 slot 分配稳定虚拟区间；
- double buffer/ring/fixed lazy tensor 的每个 slot 都有稳定 vaddr->ppn；
- stage/subbatch 只改变发给 DMA/Gemmini 的虚拟地址；
- 运行期不再对同一 tensor vaddr 反复 `bind_vpages + flush`。

短期判断：因为当前 cfg32 workflow 被强制到 blocking mode，且每个 stage worker 在同一 subbatch 内先完成
entry DMA，再构建 task、issue/fence Gemmini，再 export，动态重绑不一定是当前首要卡死原因。
但如果后续打开 async overlap 或跨 stage 共享 alias，运行期重绑会变成高风险结构问题。

## 5. Manager ownership 的当前边界

`prt_action_alloc_acc()` / `prt_action_bind_topology()` 已经建立 action/stage 级 manager 分配：

- `exec->stage_acc_ids[stage]`
- `exec->stage_dma_ids[stage]`
- `exec->stage_mgr_ids[stage][tile]`

在 pair manager mode 下，DMA manager id 与 Gemmini local id 对齐；同一 stage 很容易出现 Gemmini 与 DMA
使用同一个 manager id。当前 blocking path 的时序大致是：

1. fixed-load DMA 使用 stage DMA manager；
2. fixed-load DMA batch scope release；
3. stage task 使用 stage Gemmini manager(s)；
4. Gemmini blocking fence；
5. export DMA 使用 stage DMA manager。

这使同一 stage 的 DMA/Gemmini overlap 风险暂时被保守同步压住。

仍建议补的 fail-fast 校验：

- 每条 DMA 的 manager 必须在当前 stage/action 分配集合内；
- 每条 Gemmini task 的 manager list 必须等于或属于 `stage_mgr_ids`；
- 若某 stage 的 DMA manager 与任一 Gemmini manager 相同，且 runtime 处于 async/overlap mode，应直接拒绝运行，
  除非 artifact 明确声明 shared scope/串行段。

## 6. 当前 gdbserver 首轮应看什么

新 cfg32 AGFI 出来后，首轮 gdbserver attach 的目标不是调性能，而是定位卡点相位：

- `thread apply all bt` 看是否有 worker 卡在 `prt_pipebuf_wait_full` / `prt_ring_wait_ready`；
- 若卡在 DMA，看是否停在 `hw_dma_fence`、`dma_token_fence_scope`、`prt_rr_release_scope` 或 v2p；
- 若卡在 Gemmini，看是否停在 `gemm_blocking_conv_run` / `gemm_blocking_fence`；
- 若卡在 SPM xlate，看是否停在 `runtime_flush_stage_spm_xlate` / `prt_gemmini_spm_xlate_flush`；
- 读取 `rt->cfg.sync_mode`、`rt->cfg.dma_backend`、`rt->cfg.gemmini_mode`，确认 F2 上确实是 blocking path；
- 读取当前 `ctx->stage_id`、`progress_sbatch`、`exec->stage_acc_ids[]`、`exec->stage_dma_ids[]`、`task.manager_ids[]`。

如果首轮 backtrace 显示所有 worker 都在 pipe/ring wait，则优先查 pipeline dependency / buffer state；
如果有 worker 卡在 `hw_dma_fence`，优先查 optimized DMA direct path、done flag PA、src/dst PA、manager id；
如果卡在 ReRoCC acquire/release，则优先查 cfg id、manager id、release readback 和硬件 cfg32 行为。

## 7. 当前不建议立刻改代码的原因

现在 bitstream 构建已经启动，且首要目标是得到一个可 attach 的 cfg32 NIC AGFI。此时改 runtime 结构会引入两个变量：

- 新 bitstream 还没验证，无法区分硬件问题和软件新改动；
- 运行期 PTE 重绑改成 slot-stable vaddr 是跨 `stage_tensor_exec_addr`、pipe/ring slot 地址、Gemmini desc、DMA path
  的系统性改动，不适合在 F2 bring-up 前半改。

建议顺序：

1. 用当前保守 blocking path 跑通新 cfg32 AGFI 的 gdbserver attach；
2. 用 gdbserver 定位 pipeline-runtime 当前真实卡点；
3. 若卡点不是动态 PTE 重绑，先修直接 blocker；
4. 再做 slot-stable vaddr 改造，并用 CPU dry-run、artifact audit、gdbserver/F2 回归逐步验证。
