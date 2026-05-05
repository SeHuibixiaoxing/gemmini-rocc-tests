# Pipeline Runtime Hardware Wait Static Blockers

更新时间：`2026-05-05 18:20 UTC`

状态说明：本文保留 18:20 UTC 静态分析原文；构建状态已过期。当前 cfg32 NIC
build 状态和 noTrace fallback 预案见
[`cfg32_nic_gdbserver_build_status_and_contingency_20260505_temp.md`](cfg32_nic_gdbserver_build_status_and_contingency_20260505_temp.md)。

本文记录等待 `cfg32_nic` bitstream 期间对 pipeline-runtime 卡死路径的静态复查。重点不是重新解释所有同步机制，
而是明确哪些等待有软件 timeout，哪些等待一旦硬件不前进就只能靠 gdbserver/硬件观测定位。

## 1. 分层结论

当前 blocking debug 路线下，软件线程等待可分成三类：

- pipe/ring/condition-variable 等待：有 10ms 级别的短 timeout/retry，外层 segment watchdog 能看到 sink
  progress 长时间不前进。
- ReRoCC acquire：有 `PRT_RR_ACQUIRE_MAX_RETRIES=1000000` 的 retry 上限，理论上可返回
  `PRT_ERR_TIMEOUT`。
- 硬件 fence/custom instruction 等待：没有软件级 timeout。若硬件不接受或不完成对应指令，worker 线程会停在
  custom instruction、`rr_fence()` 或 `gemmini_fence()` 内，外层 watchdog 只能设置 `stop_requested`，
  之后仍可能卡在 `pthread_join()`。

因此，F2 live hang 时，`watchdog_ms` 不能保证程序会带错误码退出。remote gdbserver 的第一价值是确认 PC
到底停在 pipe/ring 逻辑，还是已经进入硬件 fence/custom instruction。

## 2. 关键代码证据

### Pipe/ring 等待

`src/prt_scheduler.c` 的 `cond_wait_pred()` 支持 `pthread_cond_timedwait()`。stage worker 在
`src/prt_runtime.c` 中把 `wait_timeout_ns` 收敛到 10ms：

```text
if (wait_timeout_ns == 0 || wait_timeout_ns > 10000000ULL) {
  wait_timeout_ns = 10000000ULL;
}
```

`entry-full`、`entry-c1-process`、`entry-c5-process`、`entry-c7-ring-ready`、`export-empty`、
`export-ring-idle`、`export-c4-drain` 和 `export-dma-retire` 都会在 timeout 后记录 wait phase，并回到
worker loop 继续观察 `stop_requested` / `fatal_error`。

### 外层 segment watchdog

`prt_runtime_run()` 的 threaded backend 按 sink progress 轮询：

```text
if (now - start_ms > rt->cfg.watchdog_timeout_ms) {
  rt->fatal_error = PRT_ERR_TIMEOUT;
  rt->stop_requested = 1;
  break;
}
```

但随后会对所有 worker 执行 `pthread_join()`。如果某个 worker 已经停在不可返回的硬件 custom instruction，
这个 join 本身也不会返回。

### DMA blocking fence

`dma_blocking_wait()` 调用：

```text
fence_status = hw_dma_fence();
```

`timeout_ns` 只进入日志/面包屑，不包住 `hw_dma_fence()`。`hw_dma_fence()` 最终是
`rerocc_coupleddma_wait()`，也就是 `FUNCT_CHECK_COMPLETION` 的 RoCC 指令。

硬件 `GemminiCoupledDMA` 中：

```text
readyFence = isFence && canAcceptRespCmd && canCompleteFence
canCompleteFence = !dmaBusy
```

所以如果 DMA FSM、TileLink、SPM xlate、completion flag write 或 ReRoCC 路由导致 `dmaBusy` 不清零，
CPU 可能直接停在这条 wait 指令上。此时 completion flag 是否为 1 是次级观察点；首先要确认 PC 是否卡在
`hw_dma_fence()`。

### ReRoCC fence / Gemmini fence

`prt_rr_fence_scope()` 调用 `rr_fence(scope->cfg_id)`；`rr_fence()` 写 `CSR_RRBAR` 后执行普通
`asm volatile("fence")`。它没有 runtime timeout。

`gemm_blocking_fence()` 调用 `fence_task_managers()`，对 task 的 unique manager 逐个：

- `prt_rr_acquire_scope(... opcode=3 ...)`
- `prt_rr_fence_scope(&scope)`
- `gemmini_fence()`
- scope drain/release

其中 acquire 有 retry 上限，但 `rr_fence_scope()` 和 `gemmini_fence()` 没有软件 timeout。

## 3. 对首轮 GDB 的影响

新 AGFI 跑起来后，首轮 attach 不应该先单步大量业务逻辑，而应先做卡点分流：

```gdb
info threads
thread apply all bt
thread apply all p/x $pc
thread apply all x/6i $pc
```

优先看是否停在以下符号/邻近调用：

- `dma_blocking_wait`
- `hw_dma_fence`
- `prt_rr_fence_scope`
- `fence_task_managers`
- `gemm_blocking_fence`
- `gemmini_fence`
- `stage_wait_exports_ready`
- `prt_pipebuf_wait_full`
- `prt_pipebuf_wait_empty`
- `prt_ring_wait_ready`
- `prt_ring_wait_idle`
- `pthread_join`

判读：

- PC 在 `pthread_cond_timedwait` 或 pipe/ring helper：偏软件调度、buffer 状态或上游 worker 未推进。
- PC 在 `pthread_join`，且某 worker PC 在 DMA/Gemmini/ReRoCC fence：外层 watchdog 已触发或准备退出，但被硬件等待线程拖住。
- PC 在 `hw_dma_fence` 或 RoCC wait 指令附近：优先查 DMA FSM/TL/SPM xlate/ReRoCC scope，而不是 completion flag
  VA/PA 本身。
- PC 在 `gemmini_fence` 或 `rr_fence_scope`：优先查 Gemmini command drain、RRCFG/RROPC 绑定和 manager ownership。

## 4. 当前有效改动与未解决点

已经有效的改动：

- `spm_xlate_enable=1` 会强制 `sync_mode=blocking_debug`、`dma_backend=blocking_fence`、
  `gemmini_mode=blocking_fence`，降低 async token/overlap 变量。
- DMA completion flag 已改成 runtime 级 page-aligned/prefault/mlock pool，并记录 VA->PA。
- action/stage manager 和 SPM bounds 已有 fail-fast 审计，当前 `bertmini` 三组 mapping 没有明显 12-manager
  oversubscription。

仍未解决：

- blocking fence 本身没有可恢复 timeout。
- `timeout_ns` 不能包住 RoCC custom instruction 或 `gemmini_fence()`。
- 主线程 watchdog 不能可靠杀掉已停在硬件 fence 内的 worker。
- 动态 overlap 的 manager/page 冲突证明还没完成；当前首轮目标仍应保持 blocking debug。

## 5. 后续调试策略

短期不建议为了“让 watchdog 能退出”大改硬件 fence 语义。更稳妥的顺序是：

1. 先用新 `cfg32_nic` bitstream 打通 remote gdbserver attach。
2. 在 hang 现场用 GDB 区分 pipe/ring wait、DMA fence、SPM xlate、Gemmini fence、pthread join。
3. 如果卡在 DMA fence，再结合 DMA monitor CSR、completion flag、SPM xlate fault CSR 和 manager busy CSR 缩小范围。
4. 如果卡在 Gemmini/ReRoCC fence，再看 RRCFG/RROPC 绑定、manager id 和 stage ownership。
5. 只有确认是“硬件等待缺少软件可恢复路径”阻碍后续自动化测试时，再设计可 poll 的 status/fence ABI。

## 6. 构建状态

本文写入时：

- `pairdummy-cfg32-nic-mainline-20260505T132956Z` 仍在远端 Vivado，尚未进入可验证 AGFI。
- `pairdummy-cfg32-nic-notrace-20260505T171453Z` 仍在 GoldenGate，尚未生成 `FireSim-generated.sv`。
