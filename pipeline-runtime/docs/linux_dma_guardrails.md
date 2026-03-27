# Linux DMA Guardrails

本文件记录 Linux userspace + ReRoCC/CoupledDMA + FireSim F2 路径里已经踩过的坑，以及当前
`pipeline-runtime` 代码对这些坑的防线。

适用范围：

- Linux on RISC-V
- host DRAM <-> shared-SPM DMA
- `pipeline-runtime`
- `rerocc-linux-tests-coupleddma` 小回归

不适用范围：

- host-mode smoke path
- 纯 SPM <-> SPM 连续页单请求路径
- 不经过 CoupledDMA 的软件 memcpy

## 1. 已踩过的坑

### 1.1 不要把 Linux 用户态虚拟连续误判成物理连续

错误假设：

- `src`/`dst` 在虚拟地址上连续，就可以把整段一次性提交给 DMA

真实情况：

- Linux 用户态跨页缓冲区通常只保证虚拟连续，不保证物理连续
- 如果直接把跨页 host buffer 当成单个连续 PA 区间提交，硬件看到的后半段地址可能就是错的

典型症状：

- 第一笔或第一类大块 DMA 就 `timeout waiting done flag`
- 小块、单页、纯 SPM 路径正常
- `dma_dram_to_shared_misaligned_fullpage` 这类 case 卡住或失败

结论：

- 只要一端是 Linux host buffer，就必须按 host page 分 chunk
- 每个 chunk 都单独 `virt_to_phys`
- 不能把“首页 PA + 总长度”当成整个用户态缓冲区的物理描述

### 1.2 `bytes >= 64` 且 `src_mod64 != dst_mod64` 时，直接提交并不安全

错误假设：

- 只要 PA 对了，任意对齐关系的大块 DRAM <-> SPM DMA 都能直接发

真实情况：

- 在当前 CoupledDMA 路径里，大于等于 64B 且 `src_mod64 != dst_mod64` 的请求形状曾经稳定触发挂起
- 典型形状是：
  - host offset `0x10`
  - shared-SPM offset `0x00`
  - bytes `1024`

典型症状：

- `set_dst`/`set_src` 后没有完成
- bertmini 第一笔 DMA 曾经卡在这种形状
- Linux coupleddma coverage 的 `dma_dram_to_shared_misaligned_fullpage` 能稳定覆盖这类请求

结论：

- 对 host <-> SPM DMA：
  - 若 `bytes >= 64`
  - 且 `src_mod64 != dst_mod64`
  - 不要直接提交原始 host pointer
- 应改为：
  - 先进入 stage-local bounce page
  - 让 bounce offset 的 `mod64` 对齐到目标端地址
  - 再提交 bounce 对应的 PA

### 1.3 completion/done flag 也必须走 `virt_to_phys`

错误假设：

- 只需要把 payload 的 `src/dst` 转成 PA，完成标志可以继续传 host VA

真实情况：

- CoupledDMA 硬件写回的是物理地址
- 如果 done flag 仍是 VA，软件会一直等不到完成

结论：

- `done/completion` flag 必须和 payload 一样，先做 `virt_to_phys`
- 不能把 host VA 直接喂给 `set_dst(..., done_flag_addr)`

### 1.4 overlap fast path 不能绕过 Linux DMA 防线

错误假设：

- 只要 overlap/nonblocking 想提升性能，就可以把 DRAM 路径也退化回“单请求直接 submit”

真实情况：

- 一旦异步捷径绕过“按页 chunk + 每 chunk virt_to_phys + 必要时 bounce”，前面修过的 Linux DMA 问题会重新回到 runtime

结论：

- overlap 快路径只能用于已经证明安全的路径
- 当前最保守规则是：
  - 纯 SPM <-> SPM
  - 且页列表物理连续
  - 才允许 single-request overlap submit
- Linux host DRAM 路径不应绕过 chunk helper

### 1.5 timing-based nonblocking 失败不一定是 DMA 协议 bug

错误假设：

- `conv_dma_parallel_nonblocking` 失败，就一定是 DMA/并发协议坏了

真实情况：

- 这类测试依赖“long job 真比 short job 长”
- 如果 FPGA 上标成 long 的 job 实际更快，就会出现：
  - `short_before_long=0`
  - `pass=0`
- 这是测试标定问题，不等价于 runtime/DMA 卡死

这次的修正：

- Linux 和 baremetal nonblocking 小回归里的 `REROCC_LONG_CONV_ITERS` 已从 `4` 调到 `16`

结论：

- timing-based nonblocking 结果必须结合绝对时间字段看
- 先判断是“协议挂死”还是“长短任务标定反了”，再决定往软件协议还是测试参数排查

## 2. 当前 pipeline-runtime 审计结果

审计时间：

- 2026-03-20

结论：

- 当前 `pipeline-runtime` Linux/F2 主路径里，没有再发现上述同类问题

已确认的防线：

- `src/prt_dma.c`
  - `dma_copy_host_to_spm_pages_linux()`
    - 按 host page 分 chunk
    - 每 chunk 单独 `prt_host_virt_to_phys()`
    - `src_mod64 != dst_mod64` 时走 stage-local bounce page
- `src/prt_dma.c`
  - `dma_copy_spm_pages_to_host_linux()`
    - 同样按 host page 分 chunk
    - 每 chunk 单独 `prt_host_virt_to_phys()`
    - 需要时走 bounce page 再回拷
- `src/prt_dma.c`
  - `dma_debug_capture_done_flag()`
    - 对 `tok->hw_done_flag` 做 `prt_host_virt_to_phys()`
    - 没有再把 completion flag 当 VA 直接下发
- `src/prt_scheduler.c`
  - `can_submit_overlap_single_req()`
    - 只有 `to_ring` 的 contiguous SPM <-> SPM 路径才允许 overlap single-request
    - DRAM export path 这里直接返回 `0`
- `src/prt_scheduler.c`
  - `C2` 的 DRAM export 在 Linux 下不会走 direct `prt_dma_submit()` overlap shortcut
  - `C6` 的 direct `prt_dma_submit()` 路径只用于 SPM -> ring

因此当前判断是：

- `pipeline-runtime` 已经吸收了这次 Linux coupleddma 小回归暴露的同类经验
- 这次在小回归里重新出现的 `dma_dram_to_shared_misaligned_fullpage` 卡住，根因是在 regression helper 本身，而不是 `pipeline-runtime`

2026-03-25 补充审计：

- 对文档里此前 baremetal 闭环过的问题再次对照后，当前 `pipeline-runtime`
  Linux/F2 主路径没有重新落回这些旧根因：
  - `mvin2` accumulator dirty-row 语义问题
  - 缺少 `rr_fence(cfg_id)` 的 manager-visible completion 链
  - pointwise `J=128` 的 chunk-bias VA/PTE overlap
- 其中 pointwise 旧 VA/PTE overlap 在 runtime 里被新的地址空间合同结构性规避：
  - action 级独占 alias VA window
  - 独立 vpage 分配与绑定
- 同时，`pipeline-runtime/src/main.c` 入口已经显式关闭 `stdout/stderr` 缓冲。
  因此后续 Linux/F2 stall 取证应优先依赖低噪声 build 和少量边界日志，
  而不是继续增加 hot/raw `printf`。
- 2026-03-25 的低噪声 live replay 也支持这个判断：
  - 关闭大部分 progress log 后，guest 仍能稳定 boot 到 bertmini wrapper
  - 但在 `ours2` method 启动后再次进入 silent window
  - 所以下一步需要的是更少但更强判别力的 runtime 边界 marker，
    而不是恢复整套重日志

## 3. 回归与取证建议

只要改动涉及下面任一类代码：

- `pipeline-runtime/src/prt_dma.c`
- `pipeline-runtime/src/prt_scheduler.c`
- `pipeline-runtime/src/prt_runtime.c`
- `rerocc-linux-tests-coupleddma/*dma*`
- `GemminiCoupledDMA.scala`
- `Controller.scala`

都建议按这个顺序验证：

1. 先看静态语义
   - 有没有把 host pointer 当连续 PA 用
   - 有没有绕过 `virt_to_phys`
   - 有没有新的 direct single-request DRAM overlap path
2. 先跑小 Linux coupleddma regression
   - 先于 bertmini / 大 workload
3. 重点看这些 marker
   - `DMA_MATRIX_RESULT`
   - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage`
   - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking`
   - `NONBLOCKING_SUMMARY`
4. 只有小回归过了，再跑 bertmini / pipeline-runtime

取证规则：

- `uartlog` 才是最终 verdict
- `heartbeat.csv` 只说明 guest 是否还在推进
- 不要只看 manager exit code

## 4. 当前可采信证据

2026-03-20 的小 Linux coupleddma FPGA 回归结果：

- 结果目录：
  - `sims/firesim/deploy/results-workload/2026-03-20--06-16-29-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles`
- 关键 marker：
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`
  - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS`
  - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1`
  - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
  - `ALL_TESTS_PASS`

如果未来又出现同类问题，先回到这份结果对照，不要重新从“是不是 FireSim infra 坏了”开始猜。
