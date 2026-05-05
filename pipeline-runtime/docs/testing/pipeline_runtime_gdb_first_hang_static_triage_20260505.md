# Pipeline Runtime 首轮 GDB 卡点静态分流

更新时间：`2026-05-05 18:36 UTC`

本文记录等待 `12p4c128sbus32cfg + optimized DMA + current NIC` bitstream 时的静态排查结论。目标不是证明硬件正确，而是把新 AGFI 首次 gdbserver attach 后该看什么、如何按线程栈分流说清楚。

## 当前构建状态

- 主线 cfg32 NIC bitstream `pairdummy-cfg32-nic-mainline-20260505T132956Z` 仍在远端 Vivado。最新状态已经完成大综合任务的 `Finished Renaming Generated Ports`，尚未进入 placement，也尚无 AGFI/AFI。
- no-TraceIO fallback `pairdummy-cfg32-nic-notrace-20260505T171453Z` 仍在本机 GoldenGate。最新中间文件已到 `post-autocounter.fir/json`，`FireSim-generated.sv` 尚未生成。
- 当前没有 F2 runfarm；只有一个 z1d build host `i-0479b74dd4de8e428` / `192.168.0.60`。

## 静态结论

当前 `spm_xlate_enable=1` 会把 runtime 强制到保守同步路径：

- `sync_mode = blocking_debug`
- `dma_backend = blocking_fence`
- `gemmini_mode = blocking_fence`

因此首轮 F2 现场默认不应该先假设复杂 overlap bug。worker 内的普通执行顺序更接近：

1. entry buffer 准备和等待；
2. `build_stage_task_desc()` 触发 `stage_prepare_exec_views()`；
3. fixed/lazy tensor 通过 DMA copy 到 SPM pages；
4. stage 的 SPM xlate vpage 绑定和 flush；
5. `prt_gemm_conv_run()` / `prt_gemm_fence()`；
6. `sync_stage_export_aliases()` 通过 DMA 把 SPM export 拷回 host/model alias；
7. pipe/ring/full/empty 状态推进。

这意味着首轮 GDB 分类应优先看线程 PC 是否落在以下边界：

- fixed-load DMA：`prt_dma_copy_dram_to_spm_pages()` -> `dma_blocking_wait()` -> `hw_dma_fence()`
- SPM xlate：`stage_prepare_exec_views()` -> `runtime_flush_stage_spm_xlate()` -> `prt_gemmini_spm_xlate_flush()`
- Gemmini：`prt_gemm_conv_run()` -> `gemm_issue_task()` / `conv_call_for_manager_*()`，或 `prt_gemm_fence()` -> `fence_task_managers()`
- export DMA：`sync_stage_export_aliases()` -> `copy_tensor_pages_to_model_aliases()` -> `dma_blocking_wait()`
- software wait：`prt_pipebuf_wait_full/empty()`、`prt_ring_wait_ready/idle()`、`stage_wait_exports_ready()`
- shutdown hang：main thread 在 `pthread_join()`，worker 在上述硬件 wait 内

## 首轮 GDB 命令

在 guest `gdbserver --once` 已经打印 listening 后，只让真正的 GDB 第一个连接：

```gdb
target remote :1234
set pagination off
info threads
thread apply all bt
thread apply all p/x $pc
thread apply all x/6i $pc
```

若程序已经卡住，先不要 `next`。先按栈分类：

- worker 在 `hw_dma_fence()`：先查 DMA manager idle/TL/SPM xlate/manager scope，再查 completion flag PA。
- worker 在 `prt_gemmini_spm_xlate_flush()` 或 `prt_rr_*`：先查 cfg31 xlate scope、opcode 3 绑定恢复、release readback。
- worker 在 `gemmini_fence()` 或 `fence_task_managers()`：先查 Gemmini command drain、split helper 是否已 fence、manager scope ownership。
- worker 在 pipe/ring cond wait：优先查 producer/consumer subbatch、buffer full/empty、fanout pending。
- main 在 `pthread_join()` 且 worker 在硬件 wait：说明 watchdog/stop 标志不能中断自定义指令 wait，后续需要用硬件状态或更细粒度 workload 二分。

## no-DMA 二分状态

当前 runtime 没有现成 `--no-dma-compute` 开关。已有 host/CPU dry-run 和 artifact audit 只能证明调度合同、SPM bounds、manager 数量域等软件前置条件，不能把 live F2 卡点从 DMA/completion 和 Gemmini/SPM xlate 中二分出来。

建议等待新 AGFI 能 attach 后再决定是否实现该开关：

- 如果首轮 GDB 显示卡在 fixed-load/export DMA，优先加最小 no-DMA compute profile 或 forced-bounce/direct 对照。
- 如果首轮 GDB 显示卡在 Gemmini fence 或 SPM xlate flush，no-DMA compute 价值较低，应先缩小到单 stage/单 manager 的 Gemmini/SPM xlate 用例。
- 如果首轮 GDB 显示纯软件 cond wait，先修 scheduler/buffer 状态，不应先改硬件。

## 对当前代码有效性的判断

- `blocking_debug` 路径下，普通 worker 不走 `stage_overlap_prefetch_entries()`，因此目前不应把同 stage DMA/Gemmini overlap 作为首要嫌疑。
- fixed tensor 的 DMA load 发生在 `stage_prepare_exec_views()` 内，随后才绑定/flush stage xlate view。也就是说，若卡在 fixed-load DMA，优先看 DMA 的物理 page copy 和 manager scope；若卡在 xlate flush，说明已经过了 fixed-load DMA。
- `hw_dma_fence()`、`gemmini_fence()`、`prt_rr_fence_scope()` 仍没有软件可打断的 timeout。GDB 的主要价值是抓住 PC 和 token/manager/stage 变量，而不是依赖 runtime 自己退出。

