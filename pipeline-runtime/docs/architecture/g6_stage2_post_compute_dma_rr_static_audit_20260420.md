# g6 Stage2 Post-Compute DMA/RR Static Audit 2026-04-20

## 范围

- 本文只基于现有源码和既有 live snapshot 做静态审计。
- 目标是回答：
  `g6 bertmini`
  当前为什么稳定卡在
  `worker stage=2 subbatch=0 compute-done`
  之后，
  以及这条路径上**到底哪些具体指令会阻塞**。

相关 live snapshot：

- `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T105143Z-g6-stage2-compute-done-stall`

## 先钉死当前 stage / manager / cfg

### 1. 当前卡住的 stage 是谁

从 mapping：

- `pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g6_d6_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
  可见：
  - `globalStageId: 5`
  - `layerIdList: [5]`
  - `entryTensorIdList: [7]`
  - `entryTensorTypeList: [SHARED_SPM]`
  - `exportTensorIdList: [8]`
  - `exportTensorTypeList: [DRAM]`
  - `vAccIdxList: [[0, 1]]`
  - `entryBufferIdList: [9]`
  - `exportBufferIdList: [10]`

从 live log：

- `action=4 stage=2 layer=5 acc_util=2 split=2 gm0=4 dm0=4 explicit=0 virt=0,1`
- `worker stage=2 subbatch=0 begin op=1 acc=4 dma=4 tiles=2`

因此当前主线卡点对应的是：

- `segment=3`
- `local stage=2`
- `globalStageId=5`
- compute manager 集合起点是 `mgr4`
- export DMA manager 是 `mgr4`

### 2. 这个 stage 实际会用哪个 RR cfg

`prt_rerocc.c` 中：

- `rr_cfg_id_for_stage(stage_id, opcode_id)`：
  `((stage_id * 2) + lane) % RR_MAX_CFGS`
- `opcode_id == 2` 时 `lane = 0`
- `opcode_id == 3` 时 `lane = 1`

所以对当前 `stage_id = 2`：

- DMA / opcode2 用 `cfg4`
- Gemmini / opcode3 用 `cfg5`
- SPM xlate 另占保留槽 `cfg15`

这点很关键：

- 当前 stage2 的 export 不在用 `cfg15`
- 当前 stage2 的 compute/export 也没有互相复用同一个 cfg

## compute-done 之后的软件真实路径

`prt_runtime.c` 的 worker 主循环里，
`worker ... compute-done`
打印发生在：

1. `prt_gemm_conv_run(...)`
2. 可选 `prt_gemm_fence(...)`
3. `sync_stage_export_aliases(...)`

之后。

然后才进入：

1. entry cleanup loop
2. export loop
3. `worker ... done`

对当前 stage2：

- entry 是 `C4 shared`
- export 是 `C2 dram export`

其中：

- entry cleanup 只是改 `b->full[idx]` /
  `state_epoch` /
  `pthread_cond_broadcast`
- **没有新的硬件指令**

真正带硬件握手的是 export loop：

- `prt_process_c2(rt, b, idx, timeout_ns)`

## 当前最关键的 export 调用链

当前 stage2 的 `C2` export 会走：

1. `prt_process_c2()`
2. `prt_dma_copy_spm_pages_to_dram_prefix()`
3. `dma_copy_spm_pages_to_host_linux()`
4. `dma_submit_wait_annotated_scoped()`
5. `dma_blocking_wait()`

这里有两个非常重要的静态结论。

### 1. 它不是 overlap 路径

`prt_process_c2()` 中：

- 当前 export 目标是 DRAM，不是 ring
- `to_ring = 0`
- `can_submit_overlap_single_req()` 对 `to_ring == 0` 直接不成立

所以当前 stage2 export 是：

- **同步**
- **阻塞**
- **串行 page/chunk 提交**

### 2. 它复用了一个外层共享 RR scope

Linux host-copy 路径：

- 先 `dma_batch_scope_acquire(rt, stage=2, mgr=4, opcode=2, &scope)`
- 每个 chunk 的 submit/wait 都复用这一个 `scope`
- 所有 chunk/page 完成后才 `dma_batch_scope_release(&scope)`

所以 stage2 export 的 RR 行为不是：

- 每一页 acquire/release 一次

而是：

- 整个 export 批次共用 `cfg4 -> mgr4`
- 每个 chunk 里做 wait/fence
- **最终只在最外层 release 一次**

## 哪些具体指令会真的把 CPU 卡住

这条路径里真正可能把 guest CPU 卡死在原地的，
不是“某个抽象函数名”，
而是下面三类具体指令。

### 1. `hw_dma_fence()` 里的 DMA completion 指令

位置：

- `prt_dma.c`
- `hw_dma_fence()`

核心语句：

- `ROCC_INSTRUCTION_R_R_R(XCUSTOM_DMA, status, 0, 0, 3);`

调用位置：

- `dma_blocking_wait()`
- 在 `dma_completion_flag_refresh()` 之后、
  `dma_token_fence_scope()` 之前

它为什么会阻塞：

- `GemminiCoupledDMA.scala` 里：
  - `readyFence = isFence && canAcceptRespCmd && canCompleteFence`
  - `canAcceptRespCmd = !respValid`
  - `canCompleteFence = !dmaBusy`
  - `io.busy := dmaBusy || respValid`

也就是说这条 DMA fence/custom 指令只有在：

- DMA 内部 state 已回 idle
- copy queue 空
- 没有未消费 response

时才会被接受并返回。

因此如果 guest 真卡在**一条 DMA 指令**上，
最直接的候选就是它。

### 2. `prt_rr_fence_scope()` 对应的 `rr_fence(cfg4)`

位置：

- `prt_rerocc.c`
- `prt_rr_fence_scope()`
- `rerocc_control.h`

helper 语义：

1. `RRBAR <- cfg`
2. 紧跟一条普通 `fence`

它为什么会阻塞：

- ReRoCC client 把该 cfg 的 `cfg_fence_state` 置成 `f_req`
- 之后发 `mUnbusy`
- manager 只有在：
  - `inst_q` 空
  - `io.busy == 0`
  才回 `sUnbusyAck`
- client 收到 `sUnbusyAck` 前，
  本地 `io.busy` 维持为真

而 manager 侧的 `io.busy`
在 pair wrapper 下不是 DMA 单独的 busy，
而是：

- `gemmini.busy || dma.busy`

所以对当前 `cfg4 -> mgr4` 的 DMA export 来说，
`rr_fence(cfg4)` 等的其实是：

- `pair manager 4` 整体不 busy，
  不是只等 DMA 子模块。

### 3. `prt_rr_release_scope()` 里的 `RRCFG` 读回

这是本轮静态审计里最容易被忽略、但最重要的一点。

裸 helper：

- `rr_release(cfg)` 只是 `RRCFG[cfg] <- 0`
- 它本身不是 completion barrier

但 runtime wrapper 不是这样写的。

`prt_rr_release_scope()` 实际做的是：

1. `rr_release(scope->cfg_id)`
2. **立刻** `rr_read_csr(CSR_RRCFG0 + scope->cfg_id)`
3. 再把 `scope->valid = 0`

而 `ReRoCCClient.scala` 里：

- `csr_cfg_io(i).stall := cfg_acq_state =/= s_idle`

这意味着：

- release 写出后，
  client 进入 `s_rel / s_rel_ack`
- 紧跟着的那条 `csrr RRCFGx`
  会被 CSR stall 住
- 直到 manager 回来 `sRelResp`
  把 `cfg_acq_state` 拉回 `s_idle`

因此：

- **`rr_release()` helper 自身不是屏障**
- **但 `prt_rr_release_scope()` 这个 wrapper 实际上是阻塞的**
- 真正会卡住 CPU 的，
  是它后面的那条 `RRCFG` 读回指令

如果后面要做“精确看到卡在哪条指令”，
这里就是一个明确的静态答案。

## 为什么 pair-manager busy 仍然必须看

`GemminiCoupledDMAPairWrapper.scala` 中：

- `io.busy := outer.gemmini.module.io.busy || outer.dma.module.io.busy`

`ReRoCCManager.scala` 中：

- `sRelResp` 只在 `state === s_rel_wait && !io.busy && inst_q.empty`
- `sUnbusyAck` 只在 `state === s_unbusy && !io.busy && inst_q.empty`

所以：

- 对 `cfg4 -> mgr4` 的 DMA export，
  无论是 `rr_fence(cfg4)` 还是 `rr_release_scope(cfg4)`，
  都会被 **Gemmini4 或 DMA4 任一侧的残余 busy** 卡住。

## 但 compute 侧 residual Gemmini busy 不是首要嫌疑

当前静态审计也排掉了一层误判：

- stage2 的 compute 不是非阻塞 fire-and-forget 后立刻打印 `compute-done`

对 `split=oc / tiles=2`，
`run_conv_oc_split()` 实际会：

1. 对 `mgr4` 调 `conv_call_for_manager_sync_strided()`
2. 对 `mgr5` 调 `conv_call_for_manager_sync_strided()`

而这个 `sync_strided` 路径内部明确做了：

1. `prt_rr_acquire_scope(... opcode3/cfg5 ...)`
2. issue pointwise/conv
3. `prt_rr_fence_scope(&scope)`
4. `gemmini_fence()`
5. `flush_scope_after_drain(&scope)`
   - 里面还会再次
     `gemmini_flush(0)`
     `prt_rr_fence_scope(scope)`
     `gemmini_fence()`
6. `prt_rr_release_scope(&scope)`

因此，
到 `worker stage=2 subbatch=0 compute-done`
这条日志打印时：

- compute 路径上的 `mgr4/mgr5`
  已经走过同步 drain/release

这不能 100% 证明 `gemmini4.busy` 绝不残留，
但静态上已经说明：

- 当前更应该优先怀疑
  **DMA export wait corridor**
- 而不是“compute 根本没 fence”

## 当前最合理的嫌疑顺序

基于上述源码路径，
当前 stage2 post-compute 窗口里，
优先级最高的嫌疑顺序是：

1. `dma_blocking_wait()` 中的 `hw_dma_fence()`
2. 紧随其后的 `prt_rr_fence_scope(cfg4)`
3. 所有 chunk/page 完成后的外层 `prt_rr_release_scope(cfg4)` 读回

相对地：

- entry cleanup 本身不是硬件阻塞点
- `cfg15` 也不是这条主链的首要解释

## 为什么看起来“卡点在移动”

过去看起来像是在碰运气，
本质上是因为同一条 stage2 export corridor 里，
存在多个不同粒度的阻塞点：

1. DMA custom fence 指令
2. RR unbusy fence
3. RR release 后的 cfg readback
4. 另外还有与之交错的 xlate cleanup / restore marker

如果日志只覆盖到这些边界前后的粗粒度 marker，
最后能看到的“最后一条日志”
就可能落在：

- `compute-done`
- `rrf-b`
- `rr_release_begin`
- `spm_xlate_restore_end`

但它们并不一定表示“根因在移动”。

更准确的说法是：

- **同一个 pair-manager handoff corridor 里，
  多条不同指令都可能阻塞，
  而旧日志的分辨率不足。**

## 下一轮最低扰动 probe 建议

如果后面要做新一轮定向 rerun，
最值得加、且扰动最低的不是再铺一层大日志，
而是只对 `stage=2 tensor=8 mgr=4 cfg=4`
加 3 组窄 probe：

1. `hw_dma_fence()` 前后
   - 只打
     `before/after`
   - 需要时顺手读
     manager control：
     - `0x000 mgr_busy`
     - `0x008 rocc_busy`
2. `prt_rr_fence_scope()` 前后
   - 同样只打
     `before/after`
   - 同点读
     `mgr_busy/rocc_busy`
3. `prt_rr_release_scope()` 里
   - `rr_release(cfg4)` 写之前
   - `rr_read_csr(RRCFG4)` 读之前
   - `rr_read_csr(RRCFG4)` 返回之后

这样可以直接回答：

- guest 是卡在 DMA completion 指令
- 还是卡在 RR unbusy
- 还是卡在 release readback

而不需要继续靠“最后一个大 marker”猜。
