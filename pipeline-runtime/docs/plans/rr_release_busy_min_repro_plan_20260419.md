# RR Release / Busy Minimal Baremetal Repro Plan

更新时间：`2026-04-19 09:46 UTC`

## 1. 目标

- 设计一个**极小、可快速本机 metasim** 的 baremetal repro，
  专门验证当前主线怀疑的这条风险链：
  - `rr_release(cfg)` 软件返回过早
  - 普通指令可继续前进
  - 下一条 RoCC CSR / `fence` 会被 `io.rocc.busy` 挡住
  - 真正 frontier 会落在 release ack / 后继 CSR 之间
- 避免再用 `pipeline-runtime` 主线或 `segment3_repro` 这种重负载去“等数小时才走到可疑窗口”。

## 2. 为什么选 baremetal + DMA

优先选：

- **baremetal**
  - 没有 Linux / pthread / 文件系统干扰
  - 没有 guest-side 日志路径自身的阻塞风险
  - 更适合把 frontier 固定到几条 ReRoCC / DMA 指令
- **CoupledDMA**
  - 生成 `manager busy` 的方式最简单
  - 一条 `set_dst + set_src` 就能制造 outstanding work
  - 不需要引入 Gemmini matmul / shared-spad / page-table 等额外变量
- **small globalnoc/coupleddma 硬件**
  - 仓库已存在可用 build recipe：
    `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
  - 比 pair-manager / Linux 主线更轻

## 3. 设计原则

1. **只保留验证 release/busy 所需的最小指令**
2. **默认单 hart**
   - hart0 执行
   - 其它 hart 直接 `wfi`
3. **默认不用 TraceV**
   - 先用极少量 UART marker + few-cycle bounded workload
   - 只有当 UART 判据还不够时，再加可选 `TraceV`
4. **不要把“主线拟真”与“协议证明”混在一轮**
   - 先做协议强证明模式
   - 再做更接近主线的 `fence -> release -> restore` 模式

## 4. 建议新增文件

- baremetal 程序：
  - `generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_rr_release_busy_min_repro.c`
- baremetal workload host-init：
  - `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/host-init-rr-release-busy-min-repro.sh`
- FireSim workload：
  - `sims/firesim/deploy/workloads/rerocc-lc-baremetal-rr-release-busy-min-repro.json`
- 本机 metasim runtime config：
  - `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_rr_release_busy_min_repro.yaml`

可选第二阶段再加：

- `TraceV selector=3` 版本 runtime config
- `TraceV` marker 编译开关

## 5. 硬件与运行底座

### 5.1 默认硬件

- `target_config.default_hw_config`：
  `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`

### 5.2 本机 metasim 运行形态

- `run_farm.base_recipe`：
  `run-farm-recipes/externally_provisioned.yaml`
- run host：
  `localhost`
- `num_metasims: 1`

### 5.3 metasim plusargs

本 repro 默认应比 `segment3_repro` 短很多，
因此 runtime config 建议把 metasim cycle cap 调低，例如：

- `+fesvr-step-size=128`
- `+max-cycles=20000000`

这样即使卡在阻塞指令上，
也不会像大工作负载那样浪费很久才 timeout。

## 6. 程序结构

### 6.1 基本宏与缺省值

建议默认：

- `DMA_CFG_ID = 1`
- `DMA_OPCODE_ID = 2`
- `REROCC_DMA_BASE_ID = REROCC_NUM_GEMMINI`
- `DMA_MANAGER_ID = REROCC_DMA_BASE_ID + 0`
- `REROCC_RR_REPRO_DMA_BYTES = 4096`
- `REROCC_RR_REPRO_WAIT_SPINS = 2000000`
- `REROCC_RR_REPRO_MODE = 0`
- `REROCC_RR_REPRO_TRACE_MARKERS = 1`
- `REROCC_RR_REPRO_TRACERV = 0`

说明：

- `4096 B` 足够小，不会像 `segment3_repro` 那样进入小时级 prefill
- 但比 `64 B` 更容易把 DMA `busy` 窗口拉开到可观测

### 6.2 基本数据

- `src[DMA_BYTES]`
- `dst[DMA_BYTES]`
- `completion_flag`
- 全部 `aligned(64)`
- hart0 启动前先初始化 `src`

### 6.3 基本 helper

建议只保留以下几个 helper：

- `rr_acquire_cfg_with_retry(cfg, manager_id)`
- `dma_issue_only(src, dst, &completion_flag, bytes)`
  - 只做：
    - `completion = 0`
    - `rerocc_coupleddma_set_dst(dst, completion_addr)`
    - `rerocc_coupleddma_set_src(src, bytes)`
- `dma_wait_completion(&completion_flag, spins)`
- `trace_marker(code)`
  - 默认只打 very sparse UART 行：
    `RRR code=<n> cycle=<mcycle>`
- 可选：
  `tracerv_start_marker()` / `tracerv_end_marker()`

## 7. 建议的 4 个模式

### Mode 0：baseline blocking release

目标：

- 先确认最小 plumbing 正常，不直接构造 hang。

序列：

1. acquire DMA cfg
2. `rr_set_opc(DMA_OPCODE_ID, DMA_CFG_ID)`
3. issue 一条 DMA copy
4. wait completion
5. `rr_fence(DMA_CFG_ID)`
6. **blocking release**：
   - `rr_release(DMA_CFG_ID)`
   - `rr_read_csr(CSR_RRCFG0 + DMA_CFG_ID)`
7. 检查 `dst == src`
8. PASS

判据：

- 这是“程序健康基线”
- 若它都失败，就先不要进入后续模式

### Mode 1：raw release + readback sentinel

目标：

- 直接证明：
  `rr_release()` 软件返回早于 release handshake completion

序列：

1. acquire
2. `rr_set_opc`
3. issue DMA
4. **不 wait**
5. **不 fence**
6. `rr_release(DMA_CFG_ID)`
7. marker：`after-raw-release`
8. `rr_read_csr(CSR_RRCFG0 + DMA_CFG_ID)`
9. marker：`after-readback`
10. PASS / END

为什么故意不 `rr_fence`：

- 这是协议强证明模式
- 要故意让 manager 仍有 outstanding work，
  这样 `sRelResp` 才会被 `busy` 明显推迟

预期：

- `after-raw-release` 应该先出现
- `after-readback` 会显著更晚，
  或在真正异常时卡住不出现

结论含义：

- 若 `after-raw-release` 能出现，
  说明普通指令确实能越过 raw release 返回点
- 若随后卡在 readback，
  说明 release ack / manager drain 没回来

### Mode 2：raw release + immediate next CSR

目标：

- 验证“下一条 RoCC CSR 会不会被 busy 挡住”

序列：

1. acquire
2. 记录 `prev_opc = rr_read_csr(CSR_RROPC2)`
3. `rr_set_opc(DMA_OPCODE_ID, DMA_CFG_ID)`
4. issue DMA
5. **不 wait**
6. **不 fence**
7. `rr_release(DMA_CFG_ID)`
8. marker：`after-raw-release`
9. `rr_write_csr(CSR_RROPC2, prev_opc)`
10. marker：`after-restore-opc`

预期：

- 如果当前静态判断正确，
  很可能会停在：
  `after-raw-release` 之后，
  `after-restore-opc` 之前

意义：

- 这是最接近
  “`restore-begin` 已打印，但后面的 `RROPCx csrrw` 不退休”
  的协议证明模式

### Mode 3：fence -> raw release -> restore

目标：

- 尽量贴近主线 `pipeline-runtime` 的 release/restore 形态

序列：

1. acquire
2. 记录 `prev_opc`
3. `rr_set_opc(DMA_OPCODE_ID, DMA_CFG_ID)`
4. issue DMA
5. wait completion
6. `rr_fence(DMA_CFG_ID)`
7. `rr_release(DMA_CFG_ID)`
8. marker：`restore-begin-like`
9. `rr_write_csr(CSR_RROPC2, prev_opc)`
10. marker：`restore-end-like`

预期：

- 若这一步仍会在 8 -> 9 之间卡住，
  就非常接近当前主线怀疑路径
- 若它快速通过，
  也不否定主线，
  只说明 tiny DMA 负载下 release ack 窗口太短

## 8. Marker / 输出设计

### 8.1 默认 UART marker

建议只保留个位数 marker：

- `10`：before-acquire
- `11`：after-acquire
- `12`：after-set-opc
- `13`：after-issue
- `14`：after-raw-release
- `15`：after-readback
- `16`：after-restore-opc
- `17`：after-fence
- `99`：PASS

每个 marker 只打印一行：

- `RRR code=14 cycle=123456`

不要在热循环里打印。

### 8.2 可选 TraceV 窗口

若 UART 还不够，
再加可选 `TraceV`：

- start marker：
  放在 `rr_release()` 前一条
- end marker：
  放在 `rr_read_csr(...)` 或 `rr_write_csr(RROPC2, ...)` 后一条

这样可以直接看到：

- `rr_release` 是否退休
- 最后退休 PC 是不是停在 readback / restore CSR

## 9. 判据与分流

### 9.1 Mode 1

- **看到 `after-raw-release`，看不到 `after-readback`**
  - 说明卡在 release ack / manager drain
- **两个都看到**
  - 说明 raw release gap 存在，
    但在该 tiny case 中 ack 回来够快

### 9.2 Mode 2

- **看到 `after-raw-release`，看不到 `after-restore-opc`**
  - 说明“下一条 RoCC CSR 被 busy 挡住”成立
- **两个都看到**
  - 说明该 case 不足以把 stall 窗口拉大

### 9.3 Mode 3

- **停在 `restore-begin-like` 之后**
  - 与主线 `restore-begin -> next CSR` 风险高度一致
- **快速通过**
  - tiny DMA 太轻，
    后续应保留主线结论但不要过度外推

## 10. 为什么不用 pair-manager / Linux 主线

本设计刻意**不**直接用：

- pair-manager
- `pipeline-runtime`
- Linux baremetal overlay

原因：

- 当前第一目标不是重现所有主线现象，
  而是把
  `release-return / next-rocc-op stall`
  这条协议链单独钉死
- 一旦最小 repro 把机制证明清楚，
  再回头解释主线会更干净

## 11. 预期速度

与现有 `segment3_repro` 相比：

- 不再有
  `512 KiB + 64 KiB + 按字节 fill_pattern()`
  的长 prefill
- 不再需要等待数小时才走到 marker

目标是：

- **工作负载本身** 应收敛到秒级到低分钟级
- **整轮本机 metasim** 目标收敛到几分钟量级，
  而不是小时级

这里不对精确时长做硬保证，
但它应当明显快于 full-size `segment3_repro`

## 12. 实施顺序

1. 先实现 `Mode 0`
2. 再实现 `Mode 1`
3. 若 `Mode 1` 结论清晰，再实现 `Mode 2`
4. 最后才做 `Mode 3`
5. 只有 UART 判据不够时，再补 `TraceV`

## 13. 本设计要回答的核心问题

这份最小 repro 最终要回答的不是
“能不能再看到一个 hang”，
而是下面两句：

1. `rr_release()` 返回时，release handshake 是否真的已经完成？
2. 若没有完成，下一条 RoCC CSR 是否会像主线怀疑的那样被 `busy` 挡住？

只要这两个问题回答清楚，
主线 `pipeline-runtime` 里的
`restore-begin` 风险就会从“猜测”变成“有最小协议证据支撑”。
