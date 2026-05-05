# CoupledDMA completion static note

更新时间：`2026-05-05 17:45 UTC`

本文记录等待 `cfg32_nic` bitstream 时对 DMA completion 路径的静态复核。目标是给后续
remote gdbserver live attach 一个明确分流表。

## 1. 软件 completion pool

`src/prt_dma.c` 当前不是把 token 栈上字段传给硬件做 completion flag，而是在 runtime 级分配
稳定 completion pool：

- `posix_memalign()` 按 host page 对齐；
- `memset()` 清零；
- `dma_prefault_and_lock_buffer()` 先逐页触碰，再 `mlock()`；
- 每个 slot 通过 `prt_host_virt_to_phys()` 取得 PA；
- submit 前 `dma_completion_flag_acquire()` 取空闲 slot，写 0，并执行 CPU fence；
- wait 路径先读一次 flag，再执行 `hw_dma_fence()`，再刷新 flag。

这说明 2026-04 中提到的“completion flag 是临时虚拟地址/未锁页”的旧坑在当前代码里已经被修到
更合理的形态；但它仍然依赖 Linux v2p、页锁定、cache/一致性和硬件最后写 flag 的正确性。

## 2. 硬件 completion 语义

`generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala` 的当前语义：

- `FUNCT_DEST_INFO` 保存 `dstAddrReg` 和 `completionAddrReg`；
- `FUNCT_SRC_INFO` 把 `src/dst/len/completion` 入队；
- copy FSM 逐 beat 读源、写目的；
- `curRemaining` 归零后进入 `sIssueFlag` / `sWaitFlag`；
- flag write 是一个 TileLink Put，把 value `1` 写到 `curCompletionAddr`；
- `FUNCT_CHECK_COMPLETION` 只有在 `!dmaBusy` 且 response 通道可用时才 ready；
- `hw_dma_fence()` 对应 `FUNCT_CHECK_COMPLETION`，返回时代表该 DMA 实例 `state == sIdle`
  且 copy queue 当前 deq 侧不再 valid。

重要细节：

- completion flag write 使用的是 `curCompletionAddr`，也就是软件传入的 host PA；它不是 SPM
  alias 地址，也不会经过 shared-SPM xlate。
- `hw_dma_fence()` 不是按 token id 返回；当前语义更接近“这个 DMA manager 已经 idle”。
  在 blocking path 中同一 manager 同时只有一个 outstanding token，因此可用作主完成条件。
- 如果以后恢复同一 DMA manager 多 outstanding token，就不能只把 idle fence 当 per-token completion。

## 3. 后续 gdbserver 分流

若首轮 `cfg32_nic` live gdbserver 看到线程卡在 DMA wait，按以下顺序判断：

1. 停在 `hw_dma_fence()` 之前：看 submit 侧是否完成 `set_dst/set_src`，以及 manager ownership guard
   是否通过。
2. 停在 `hw_dma_fence()` 内或 custom 指令后不返回：优先怀疑 DMA FSM 未 idle、TileLink request/response
   卡住、SPM xlate PTW 等待或 ReRoCC scope 未正确完成。
3. `hw_dma_fence()` 返回但 completion flag 仍为 0：优先怀疑 flag write PA、cache/一致性或 flag write
   TileLink Put 语义，而不是 copy 数据路径本身。
4. `hw_dma_fence()` 返回且 flag 为 1，但之后卡住：转向 release/fence scope、pipe/ring 或 stage
   dependency。

建议在 GDB 中优先查看：

```gdb
thread apply all bt
frame <prt_dma_wait/dma_blocking_wait frame>
p *tok
p tok->debug_src_addr
p tok->debug_dst_addr
p tok->debug_bytes
p tok->debug_done_flag_pa
p tok->hw_done_flag
p tok->rr_manager_id
p tok->rr_cfg_id
```

## 4. no-DMA 二分现状

当前 CLI 没有现成 `--no-dma-compute` 开关。能立即执行的是配置/合同级 dry-run 和
`--hw-validate-only`；真正绕过 fixed-load/export DMA 的 compute-only 二分还需要新增一个窄 profile：

- 预置或伪造 input/weight/output 在 SPM；
- 禁用 fixed-load DMA 和 export DMA；
- 只构造一个最小 Gemmini task 并执行 issue/fence；
- 先在 host/CPU 调度层校验 task 构造，再进入 baremetal/metasim/F2。

在新 AGFI 可运行前，不建议直接改动主线 runtime 去加入大范围 no-DMA 路径。优先等 gdbserver 首轮
backtrace 判断卡点是否真的在 DMA。
