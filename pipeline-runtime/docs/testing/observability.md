# Pipeline Runtime Observability

更新时间：`2026-04-20 13:47 UTC`

## 1. 原则

- 主观测面是 guest 文件系统，不是 `uartlog`。
- 热路径优先 breadcrumb，避免高频文本日志。
- 细日志必须按条件窄开，默认只用 coarse log。
- wrapper 周期性 `sync` 负责刷盘；runtime 不主动做前台阻塞刷盘。
- `PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START/END` 留空不等于关闭 export 页级 sparse log；
  这会回落到代码内建的默认页模式。
  低扰动 baseline 必须把这两个变量钉到一个不命中真实页的 sentinel，
  只有专门的 export 观测轮次才允许改回真实页窗口。

## 2. 当前 guest 侧文件

主要文件：

- `bertmini-batch8.status`
- `bertmini-batch8.log`
- `bertmini-batch8.deep.log`
- `bertmini-batch8.audit.log`
- `bertmini-batch8.checkpoint.log`
- `bertmini-batch8.breadcrumb.bin`
- `bertmini-batch8.trigger.log`
- `bertmini-batch8.runner-early.stage`
- `bertmini-batch8.runner.stage`
- `bertmini-batch8.runner-proc.stage`
- `bertmini-batch8.wrapper.stage`

## 3. 当前推荐观测链

1. manager / pane / tmux 日志看 host 控制面是否仍活着
2. heartbeat 看 guest 是否继续推进
3. guest image + `debugfs` 读取 `status/log/breadcrumb`
4. 先跑 `triage_prt_capture.py` 汇总 frontier / claim / 推荐 probe
5. 只有 coarse 证据不足时，再开 trigger-gated 短日志或更窄 deep log

补充要求：

- 先静态读相关代码路径，再解释 capture。
- 不再默认人工 `grep + tail + decode` 多轮往返；优先先跑 capture 分诊脚本。
- Linux boot 早期若没有明确 boot error / panic / crash，
  且 `heartbeat` 继续前进，
  就继续等；
  不要只因为 `uartlog` 静默就把它记成新的异常。

推荐命令：

- `python3 scripts/triage_prt_capture.py <capture-dir>`
- `python3 scripts/triage_prt_capture.py <capture-dir> --emit-trigger-env`
- `python3 scripts/decode_prt_breadcrumb.py <guest-breadcrumb.bin>`
- `scripts/pairdummy_sbus128_workflow.sh debug-preflight`

## 4. Breadcrumb

breadcrumb 的目标是替代最容易扰动时序的热路径文本日志。

当前做法：

- runtime 热路径写固定大小 mmap 二进制文件
- wrapper 周期性 `sync`
- host 用 `debugfs` 拉回二进制，再用 `decode_prt_breadcrumb.py` 解码
- 当 trigger-gated 观测证明会明显前移 frontier 时，
  优先改用 breadcrumb-only 收窄：
  只覆盖少量 `PIPELINE_RUNTIME_BREADCRUMB_*` 条件，
  不在热路径上增加新的字符串日志或 trigger 过滤开销

适用场景：

- DMA submit / wait
- RR acquire
- pointwise fallback caller / inner matmul 边界
- SPM xlate flush / release
- 其他易受字符串日志扰动的热路径

对 `rr_release()` / `busy` 语义有歧义时，优先用
“`rr_release(cfg)` 后立即读回同一 `RRCFGx`” 的低扰动哨兵法：

- `rr_release(cfg)` 本身只负责发起 release，不保证 handshake 已完成
- `pipeline-runtime` 当前主线 release wrapper
  `prt_rr_release_scope()`
  已默认采用这条哨兵：
  `rr_release(cfg)` 后紧跟同 `cfg` 的 `rr_read_csr(RRCFGx)`
- 紧随其后的 `rr_read_csr(CSR_RRCFG0 + cfg)` 会被 `csr_cfg_io.stall`
  阻塞到 `cfg_acq_state` 回到 `s_idle`
- 因而它能把 frontier 固定到
  “release ack 是否回来 / 是否仍被 busy 挡住”，
  比继续叠热路径字符串日志更适合定位卡在具体 CSR 指令

当前 host workflow 允许临时覆盖的 breadcrumb 变量：

- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE`
- `PIPELINE_RUNTIME_BREADCRUMB_PATH`
- `PIPELINE_RUNTIME_BREADCRUMB_SEGMENT`
- `PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE`
- `PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE`
- `PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH`
- `PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS`
- `PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS`

当前 pointwise 相关 breadcrumb 约定：

- `kind=gemmini phase=gemmini_pointwise_call_begin`
  表示 `conv-sync -> pointwise subcall` 入口已到达
- `kind=gemmini phase=gemmini_pointwise_matmul_begin`
  表示已经进入 `tiled_matmul_nn_stride_auto()`
- `kind=gemmini phase=gemmini_pointwise_matmul_return`
  表示 `tiled_matmul_nn_stride_auto()` 已返回
- `kind=gemmini phase=gemmini_pointwise_call_return`
  表示 pointwise fallback 整体已返回 caller

解码时：

- `src` = pointwise input 指针
- `dst` = pointwise output 指针
- `aux0` = weight 指针
- `aux1 high32` = `dim_J`
- `aux1 low32` = `dim_K`
- `tok` = fallback type 枚举（`OS/WS/CPU`）

配合 `triage_prt_capture.py` 时，优先看：

- 最后 phase 是否停在 `gemmini_pointwise_call_begin`
- 是否已经进入 `gemmini_pointwise_matmul_begin`
- 是否已经出现 `gemmini_pointwise_matmul_return`
- 若最后只到 `rr_acquire_after_call`，不要直接下结论为 RR acquire hang

## 5. Deep Log 何时开启

只在以下情况开启：

- breadcrumb 无法区分相邻子阶段
- coarse log 无法区分 caller-return / subcall / fence 区间
- 已经有明确 segment/stage/subbatch/page 窗口

当前对 late pointwise 的优先策略：

- 先看 breadcrumb 是否停在 `gemmini_pointwise_call_begin`
  与 `gemmini_pointwise_matmul_begin` 之间
- 先结合 `triage_prt_capture.py` 汇总 sparse frontier，再决定要读哪段代码
- 只有 breadcrumb 仍不足以区分 inner 边界时，才开受控 deep log

禁止事项：

- 不要对整条主线长期打开 deep log
- 不要在 pointwise inner hot path 直接加长字符串日志
- 不要为了“看得更细”把所有 probe 常开

## 6. Trigger-Gated 短日志

这是 breadcrumb 之后、deep log 之前的中间层。

原则：

- 默认关闭
- 每行尽量短
- 只有命中目标 window 后才写
- 默认只保留少量 pre-ring 和有限 post-budget
- 先定位 frontier，再看局部上下文

当前可选 family：

- `runtime`
- `dma-fixed-load`
- `dma-export`
- `rr`
- `spm-xlate`
- `gemmini-pointwise`

当前选择器：

- `segment`
- `global_stage`
- `local_stage`
- `subbatch`
- `manager`
- `tensor_id`
- `page`
- `token`

当前推荐用法：

1. 先跑：
   `python3 scripts/triage_prt_capture.py <capture> --emit-trigger-env`
2. 在单独 subshell 中 `eval` 生成的
   `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
3. 跑：
   `scripts/pairdummy_sbus128_workflow.sh debug-preflight`
4. 通过后再做单变量 rerun

注意：

- 这组 env 是当前 fixed profile 里唯一允许临时 overlay 的参数族
- 其余 fixed profile 语义参数仍不允许 ad hoc 改动
- 若 rerun 只伴随 observability 变化，
  不能直接记成“穿过 blocker”，
  必须补 control rerun

## 7. Fixed-Load Probe 选择器

- 当前 fixed-load sparse probe 不再只能写死追
  `stage0/tensor1000001`。
- 现在可以用这两个 env 显式指定目标：
  - `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_STAGE_ID`
  - `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TENSOR_ID`
- 若不设置，代码默认仍保持旧语义：
  - `stage_id=0`
  - `tensor_id=1000001`
- `page` 窗口仍继续由下面两项控制：
  - `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START`
  - `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END`
- `submit/wait` 文本 probe 与 checkpoint probe
  现在都跟随同一组
  `stage/tensor/token/page`
  过滤条件，
  这样可以把 fixed-load 观测面直接压到
  `C1 tensor0 page0`
  这类新前沿，
  而不用再改代码换目标 tensor。

## 8. 已确认的扰动风险

- 热路径字符串探针会改变前沿。
- 同一窗口同时打开多类 probe，
  会破坏 frontier 归因。
- 文件尾 `NUL` 与半行日志可能说明文件追加路径也在阻塞。
- 当前 `v12` capture 已再次证明：
  export `page42/43` 文本 probe 区间已经通过，但前沿被推进到更后的 pointwise / RR 区域。

## 9. `uartlog` 的角色

- 仅用于：
  - Linux boot 活性
  - panic / crash / reboot loop
  - manager completion verdict
- Linux boot 早期的慢启动或静默窗口，
  不能单靠 `uartlog` 判异常；
  需要联看 `heartbeat` 和是否已经进入用户态 workload。
- 不用于：
  - runtime 内部细粒度卡点判断

## 10. `TracerV` Bring-Up

- `TracerV` bring-up 的 authoritative SOP 见：
  [`tracerv_integration_sop.md`](tracerv_integration_sop.md)
- 当前推荐顺序固定为：
  1. 最小 baremetal smoke
  2. 最小 `selector=3` marker 样例
  3. workload-shaped 轻量 variant
  4. full-size / FPGA
- `TracerV` 判据优先级固定为：
  1. `metasim_stderr.out` 中的 `first nonzero host pull`
  2. `TRACEFILE-C*` 是否非零
  3. trace 起止是否与 marker / trigger 一致
  4. `uartlog` 中的 arm / completion 信息
- 如果 `selector=3` 路线长时间拿不到 trace，
  先证明 target 当前 PC 是否还在 marker 之前，
  不要继续靠扩大日志面碰运气。
