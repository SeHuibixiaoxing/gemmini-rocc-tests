# Segment3 Stage0 Tensor6 TraceV DMA Window Plan 2026-04-21

## 目标

在不回到“大量热路径文本日志碰运气”的前提下，
用已经跑通的 `TraceV selector=3` 指令触发链路，
精确定位 `segment3 stage0 tensor6 export DMA/RR` 走廊里到底是哪一段指令没有退休。

当前希望回答的问题不是“程序大概卡在 submit/wait 附近”，
而是把范围进一步压缩成下面几类具体指令之一：

- DMA program 前后的 CPU `fence`
- `hw_dma_set_dst`
- `hw_dma_set_src`
- `hw_dma_fence()`
- 外层共享 RR scope 的 `rr_fence`
- 批末 `rr_release + RRCFG readback`

## 背景

当前主线 authoritative 结论已经更新为：

- 旧的 `segment0 tensor2 export direct-path` 歧义走廊已被越过
- 当前 plateau 落在 `segment3` 的 compute / pipe interaction 区域
- 其中最强静态怀疑，仍然是
  `segment3 stage0 tensor6`
  的 export alias 同步路径

相关代码路径：

1. `worker`
2. `sync_stage_export_aliases()`
3. `copy_tensor_pages_to_model_aliases()`
4. `copy_tensor_pages_to_model_alias_target()`
5. `prt_dma_copy_spm_pages_to_dram()`
6. `dma_copy_spm_pages_to_host_linux()`
7. `dma_submit_wait_annotated_scoped()`
8. `dma_blocking_submit()`
9. `dma_blocking_wait()`
10. 批末 `dma_batch_scope_release()`

重要静态事实：

- 这条 export 路径是共享 RR scope：
  整个 export 批次先 `dma_batch_scope_acquire()`，
  每个 page/chunk 只做 submit/wait/fence，
  最后统一 `dma_batch_scope_release()`
- 因而：
  - 每页都会遇到 `hw_dma_fence()` 与共享 `rr_fence`
  - 但真正的 `rr_release + readback`
    在批末才发生

## 本轮新增观测能力

### 1. 新增窄窗口 raw markers

位置：

- [`prt_dma.c`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c)

编译开关：

- `PIPELINE_RUNTIME_FIRESIM_TRACERV_DMA_WINDOW_MARKERS=1`

运行时过滤：

- `stage_idx == 0`
- `tensor_id == 6`
- `copied_bytes == 0`
- page range 可选：
  `PIPELINE_RUNTIME_DMA_TRACERV_PAGE_START/END`

也就是说，这批 marker 不会全局轰炸所有 DMA，
只会命中当前最可疑的 `segment3 stage0 tensor6` 第一块 page chunk。

补充：

- 这套 scope 现在已同时覆盖 export host-copy 的
  `direct-path` 与 `bounce-path`
- 因而下一轮如果目标页因为
  `dma_chunk_needs_bounce(src, dst, bytes)`
  判定而进入 bounce，
  `TraceV` 二分结果仍然有效

### 2. marker 编码表

这些值都采用与现有 worker marker 相同的 raw `.word` 方式，
避免汇编器改写：

| phase | insn |
| --- | --- |
| `program_begin` | `0x00018013` |
| `program_post_fence` | `0x00020013` |
| `program_post_dst` | `0x00028013` |
| `program_post_src` | `0x00030013` |
| `wait_before_fence` | `0x00038013` |
| `wait_after_fence` | `0x00040013` |
| `wait_before_shared_fence` | `0x00048013` |
| `wait_after_shared_fence` | `0x00050013` |
| `batch_release_begin` | `0x00058013` |
| `batch_release_end` | `0x00060013` |

### 3. workflow 基础设施补齐

位置：

- [`pairdummy_sbus128_tracerv_inst_workflow.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_tracerv_inst_workflow.sh)

新增能力：

- `PAIRDUMMY_TRACERV_START_INST`
- `PAIRDUMMY_TRACERV_END_INST`

workflow 会基于固定 runtime config 生成一份临时 effective config，
只改 `tracing.start/end`，
避免下一轮再手工改 yaml。

### 4. freshness 校验补齐

位置：

- [`verify_pairdummy_firemarshal_image_freshness.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh)

新增检查：

- 旧 worker marker 仍校验
- 新 DMA-window markers 也校验

因此下一轮 rerun 之前，可以先在本地 image 上确认：

- binary 已经包含这些 marker
- image 中的 runtime binary 也确实带上这些 marker

## 推荐二分顺序

### 阶段 A：先复用历史页签名

先收窄到旧 freeze 曾出现过的页：

```bash
export PIPELINE_RUNTIME_DMA_TRACERV_PAGE_START=124
export PIPELINE_RUNTIME_DMA_TRACERV_PAGE_END=124
```

理由：

- 历史 long log 中，旧 freeze 签名是
  `segment=3 stage=0 tensor=6 page=124 ... submit-begin`
- 如果当前 plateau 仍复现到同一页，
  先锁这个页能显著减少 trace 噪声

如果这组 page 根本拿不到 trigger，
再把 page range 放宽或取消。

### 阶段 B：先问“submit 路径是否完整退休”

推荐第一对：

- `start = ffffffff00030013`
- `end = ffffffff00038013`

含义：

- 从 `program_post_src`
  到 `wait_before_fence`

解释：

- 这对如果能稳定打出 trace，
  说明：
  - `hw_dma_submit_fence()`
  - `hw_dma_set_dst`
  - `hw_dma_set_src`
  都已经退休
  - 当前 stall 不在 submit 编程段
- 如果这对完全不触发，
  再往前移，优先检查 submit 编程段

### 阶段 C：若 B 不触发，回退到 submit 编程段

推荐顺序：

1. `program_begin -> program_post_fence`
2. `program_post_fence -> program_post_dst`
3. `program_post_dst -> program_post_src`

判据：

- 第一对不触发：
  说明 page 已命中前提下，
  stall 可能在 `hw_dma_submit_fence()` 之前
- 第一对触发、第二对不触发：
  说明更可能停在 `hw_dma_set_dst`
- 第二对触发、第三对不触发：
  说明更可能停在 `hw_dma_set_src`

### 阶段 D：若 B 能触发，直接看 DMA fence

推荐对：

- `wait_before_fence -> wait_after_fence`

判据：

- 这对不触发：
  最强候选就是 `hw_dma_fence()`
- 这对能触发：
  说明 DMA fence 已退休，继续往后看共享 RR fence

### 阶段 E：再看共享 RR fence

推荐对：

- `wait_before_shared_fence -> wait_after_shared_fence`

判据：

- 不触发：
  最强候选变成共享 scope 的 `rr_fence`
- 能触发：
  说明每页 wait/fence 已经退休，继续看批末 release

### 阶段 F：最后看批末 release

推荐对：

- `batch_release_begin -> batch_release_end`

判据：

- 不触发：
  最强候选变成外层 `dma_batch_scope_release()`，
  也就是 `rr_release + RRCFG readback`
- 能触发：
  则说明 shared RR release 也已退休，需要把怀疑转回更外层软件同步或 compute/pipe 语义

## 执行步骤

### 0. 不启动机器前的本地准备

1. `image-closure`
2. `local-freshness`
3. `show`

必须确认：

- `runtime_dma_window_markers=1`
- `runtime_dma_page_start/end` 已按本轮目标设置
- `tracing_start_inst / tracing_end_inst` 已切到对应窗口

### 1. 得到批准后，再走 FireSim 正规流

固定顺序：

1. `launchrunfarm`
2. `infrasetup`
3. `runworkload`

### 2. 观察判据

优先级：

1. `metasim_stderr.out` / FPGA host stderr 中是否出现
   `first nonzero host pull`
2. `TRACEFILE-C*` 是否非零
3. trace 起止是否与选定 marker 对齐
4. guest `uartlog` / sparse log 是否仍推进到 `segment3`

### 3. 停机策略

只有在满足下面条件后才停：

- 确认本轮选定窗口已经有明确“触发 / 未触发”结论
- 或 run 再次进入长期 plateau，且该窗口结论已经足以指导下一次二分

## 风险与解释

### 1. 为什么仍然可能看到“卡点移动”

这批 marker 只能解决“到底哪段指令退休了”这个问题，
不能单独证明“系统完全没有时序敏感性”。

但与之前主要依赖文本日志相比，
它有两个优势：

- 直接围绕具体退休指令触发
- 可以通过“窗口是否被命中”做严格二分，
  而不是只看日志尾巴停在哪

### 2. 为什么先用 page=124

不是因为已经证明当前 stall 一定还是 page124，
而是因为：

- 历史 freeze 对这个页已有强签名
- 先用它可以降低 trace 噪声
- 若拿不到 trigger，再放宽 page range，逻辑仍然清楚

### 3. 为什么先看 `program_post_src -> wait_before_fence`

因为它正好回答“submit 编程段是不是已经完整退休”。

这一步一旦回答清楚，
后面的二分就会从“submit 还是 wait 还是 release”
缩成更窄的单段问题。

## 预期产出

理想情况下，下一轮只需要 2 到 4 次 selector=3 rerun，
就能把当前 `segment3 stage0 tensor6 export` 的 stall
收缩到下面三类之一：

- DMA 编程段
- DMA fence / shared RR fence
- 批末 shared RR release

一旦收缩到其中一类，
再决定是否需要：

- 更细的 baremetal repro
- 更小范围的可综合硬件信号抓取
- 或转回硬件静态审计
