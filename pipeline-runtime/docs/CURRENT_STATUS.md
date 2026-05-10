# Current Status

更新时间：`2026-05-10 05:33 UTC`

## 2026-05-10 no-DMA performance prep for ours2 vs gemini2

临时 no-DMA 性能实验已完成本地准备，尚未上 F2 采样：

- 用户口径 `ours` -> artifact 方法名 `ours2`。
- 用户口径 `gemmini` -> artifact 方法名 `gemini2`。
- 两套 dummy8x8/sbus64/cfg32 artifact 已重新生成到独立实验目录并审计通过：
  - `ours2`: `segments=15`
  - `gemini2`: `segments=19`
- 实验目录与当前 overlay pipeline YAML 哈希一致，因此不需要覆盖 overlay artifact：
  - `ours2`: `7daad3716f35098bee8462430dab6b94dc5c46910a8365a520b012d77f681727`
  - `gemini2`: `a817a9747b5f9fcff1303d21d386504fe343b85536dd8d04d48a5bfbefe29f5f`
- runtime trace 已新增模型执行窗口字段：
  - `model_exec_ns`
  - `model_compute_ns`
  - `preprocess_ns`
  - `postprocess_ns`
- 本地 CPU/no-DMA dry-run 两个方法均退出 0；`dma_submit_count=0`、
  `gemm_issue_count=320`、`trace_event_drop_count=0`。
- 新增独立 perf workflow，避免改动上一轮 gdbserver 配置：
  `scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh`
- `debug-preflight_status=pass`，profile 为 tier 1：
  `METHODS="ours2 gemini2"`、`PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`、
  `TRACE_ENABLE=1`、`PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`。

本地 dry-run 仅验证口径，不作为 F2 性能结论。下一步是 `image-closure`，然后只开一轮
F2 run farm 顺序跑 `ours2` 与 `gemini2`，copy-back 后比较
`/root/pipeline-runtime-debug/traces/{ours2,gemini2}.trace` 中的
`model_compute_ns` 与 `model_exec_ns`，最后立即 terminate run farm。

关联记录：
[`debug_records/20260510T053309Z_no_dma_perf_ours2_gemini2_prep.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260510T053309Z_no_dma_perf_ours2_gemini2_prep.md)

## 2026-05-09 cfg32/NIC/noTrace no-DMA compute full PASS

当前最新 authoritative 结论已经更新到 `2026-05-09 19:35 UTC`：

- AGFI：`agfi-077451484fe3b63c3`
- 配置：cfg32/NIC/noTrace、dummy8x8、sbus64、4 cores / 12 Gemmini managers /
  12 DMA managers、batch8、pair-manager mode。
- 运行方式：低噪声 gdbserver profile，`PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`。
- 结果：完整跑到 `Simulation complete` / `*** PASSED *** after 22734035102 cycles`。
- 关联 checkpoint commit：`9742924 Record cfg32 no-DMA compute pass`
- F2 run farm 已关闭；实例 `i-096d267497e53747f` 最后确认到 `shutting-down`。

### 当前结论

no-DMA compute 已经把当前 blocker 从 artifact 读取、no-DMA 路线、Gemmini/SPM xlate
和普通 pipebuf 控制流中排除。后续真实正确性主线应回到 DMA submit/completion、
`hw_dma_fence()` / blocking wait、host buffer/direct DMA，以及依赖真实 DMA 完成的
producer publish。`doneflag` 仍不能作为 DMA completion 证据。

### 当前临时实验目标

在暂不恢复真实 DMA 的前提下，比较 HybridMapper 两类编排方案的 no-DMA 执行时间：

- 用户口径 `ours` 对应当前 runtime artifact 方法名 `ours2`。
- 用户口径 `gemmini` 对应当前 HybridMapper/runtime artifact 方法名 `gemini2`。
- 先在本地重新生成/审计两套 artifact，确认 dry-run 可走通。
- F2 只用于最终 no-DMA 性能采样；计时应使用 runtime 内“模型执行窗口”，不包含
  HybridMapper 生成、YAML/artifact 读取校验、model/input/golden 预处理。

关联记录：
[`debug_records/20260509T193535Z_no_dma_compute_full_pass_cfg32_gdbserver.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260509T193535Z_no_dma_compute_full_pass_cfg32_gdbserver.md)

## 2026-04-27 05:50 UTC Direct-Only cfg32_nic Local-GDB Run Hit NIC Blocker

当前最新 authoritative 调试线程已切到：

- 现有带 NIC bitstream，不重新构建 bitstream；
- `cfg32_nic` hwdb，AGFI `agfi-02e18c6f7a7a95096`；
- `pipeline-runtime` 软件侧强制 direct DMA：
  `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1`；
- 禁用搬运跳过开关：
  `PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=0`；
- 用 target-side batch local-GDB 获取卡点处 `bt`、registers 和 `$pc` 指令窗口。

### 当前已完成

- fixed profile 已升到 `pairdummy-sbus128-fixed-v25`。
- cfg32_nic local-GDB workflow 已建立：
  `scripts/pairdummy_sbus128_local_gdb_cfg32_nic_workflow.sh`
- 对应 runtime config 的 `default_hw_config` 是：
  `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic`
- local `show` / `debug-preflight` 已通过：
  - `local_gdb_enable=1`
  - `dma_force_direct_enable=1`
  - `debug_preflight_status=pass`
- guest env render 已确认：
  - `PIPELINE_RUNTIME_PROFILE_ID='pairdummy-sbus128-fixed-v25'`
  - `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE='1'`
  - `PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE='0'`
  - `PIPELINE_RUNTIME_LOCAL_GDB_ENABLE='1'`
- `image-closure`、`launch`、`infrasetup`、`remote-freshness` 都已完成。
- remote image SHA 与 local image SHA 一致：
  `236477ec720fd22e8d13db32ebbe338ae805a79652eb26872620479284fddf47`

### 本轮结果

- runworkload session:
  `pairdummy-sbus128-local-gdb-cfg32-nic-runworkload-20260427-054907`
- results dir:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-27--05-49-08-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-local-gdb-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync-local-gdb-cfg32-nic/`
- `HW_CFG_SUMMARY` 确认：
  - `RuntimeHWConfig: firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic`
  - `AGFI: agfi-02e18c6f7a7a95096`
- guest 未进入 Linux，local-GDB 未产生输出。
- `uartlog` 在 driver 启动后立即失败：
  `ERR MISMATCH! on writing tokens in. actually wrote in 0 bytes, wanted 58560 bytes.`
- run farm 已 terminate：
  - instance `i-0881a2d8250b28d6f`
  - private IP `192.168.1.202`
  - AWS state after terminate: `shutting-down`

### 当前结论

本轮没有跑到 `pipeline-runtime` 用户态，因此 direct DMA 卡点尚未被 local-GDB 观测到。
当前 blocker 是 boot 前 SimpleNIC / host bridge 路径：

- 报错来自 host-side `simplenic_t::tick()` 的 from-host stream push。
- 当前源码已有 `push() == 0` 时 retry 的逻辑，但本轮 hwdb 的 `driver_tar`
  对应 `FireSim-f2` 不包含这些新 SimpleNIC debug/retry 字符串。
- 因而最可能状态是：
  `cfg32_nic` AGFI 是目标 bitstream，但随 hwdb 部署的 driver bundle 仍是旧
  SimpleNIC host driver。

### 当前约束

- 不构建新 bitstream。
- 不检查、不打断、不清理并行运行中的另一个 `buildbitstream` 任务。
- 只通过 local-GDB cfg32_nic wrapper 运行本轮 FireSim 调试。
- 若 Linux boot 前出现 SimpleNIC token mismatch / NIC bridge 错误，先归类为
  NIC/bridge blocker，而不是 `pipeline-runtime` 用户态 direct DMA 卡点。

### 下一步

不要重复跑同一个旧 `driver_tar`。下一步应在不构建 bitstream 的前提下，找到或生成
一个包含当前 SimpleNIC host-side retry/debug 修改的 cfg32_nic driver bundle，再重新
`infrasetup -> remote-freshness -> run`。只有 Linux/local-GDB 起来后，才继续定位
`pipeline-runtime` direct DMA 卡点。

关联记录：
[`debug_records/20260427T053456Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260427T053456Z.md)

## 2026-04-21 12:57 UTC Bounce-Bypass Rerun

下面顶部内容优先于后面的旧摘要；当前主线 latest authoritative 结论已经更新到
`2026-04-21 12:57 UTC`
这一轮基于同一套 `cfg32` bitstream、但额外开启
`PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=1`
的 `g6 bertmini` fresh rerun。

### 当前结论

- 本轮的核心目的，是验证：
  **当前主线 stall 是否真的发生在 bounce path。**
- 镜像内环境已经确认带上：
  `PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=1`
- 但本轮 guest sparse log **全量没有任何**
  `bounce-bypass`
  命中。
- 当前稳定 plateau 的最新前沿，明确落在：
  `segment0 stage0 tensor2 target_seq=1(address2)`
  的 direct export 路径上，且日志直接给出：
  `bounce=0`
- 因而这轮最强结论是：
  **当前这次主线 stall 不是“已经进入 bounce path 后的真实 DMA/搬运”导致的。**

### 本轮现场

- runworkload session:
  `pairdummy-sbus128-runworkload-20260421-123806`
- terminaterunfarm session:
  `pairdummy-sbus128-terminaterunfarm-20260421-125620`
- 远端实例:
  `i-0dd25d8d66655f2fb`
  / `192.168.1.156`
- 本地抓取:
  `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260421T125606Z-g6-bounce-bypass-nonbounce-stall-capture/`
- 关联完整记录:
  [`debug_records/20260421T125757Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260421T125757Z.md)

### 这轮确认了什么

- guest 确实已进入 `pipeline-runtime` 用户程序：
  - `bertmini-batch8.status` 为 `state=running`
  - `runner.stage` 已到
    `after-bin-spawn method=ours2 pid=205 kind=launcher`
- `bounce-bypass` 实验开关不是“没进镜像”的假阳性：
  - `image-freshness` 已打印
    `PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE='1'`
- 但真正运行时：
  - guest sparse log 全量 grep
    `bounce-bypass`
    为空
  - 说明当前 plateau 前沿没有命中任何 bypass 分支
- 当前稳定 log 尾部推进到：
  - `worker stage=0 subbatch=2 begin op=1 acc=0 dma=0 tiles=4`
  - 多次
    `conv-sync-strided stage=0 mgr=0/1/2/3 ... flushed use_pointwise=1`
  - `export-target-dispatch stage=0 tensor=2 target_seq=1 ... target=address2 ... phase=begin`
  - `dma-export-host ... phase=first-chunk-submitwait-begin ... bounce=0`
  - `dma-export-host ... phase=first-chunk-submitwait-end ... bounce=0`
- 同时：
  - `heartbeat.csv` 继续从
    `807`
    增长到
    `1041`
  - guest sparse log 的执行前沿连续多轮完全不变

### 当前对根因的收敛

- 这轮已经可以把
  **“当前主线卡在 bounce path”**
  从最高优先级怀疑中移开。
- 更准确地说，当前这次 plateau 位于：
  `target_seq=1/address2`
  的 **direct export** 内部。
- 现有日志分辨率下，只能把位置收敛到：
  - 晚于
    `first-chunk-submitwait-end bounce=0`
  - 早于
    `export-target-dispatch ... target_seq=1 ... phase=end`
- 所以当前更值得继续静态审计 / 低扰动验证的是：
  - `dma_copy_spm_pages_to_host_linux()` 的 direct path
  - second alias target (`target_seq=1/address2`)
  - first chunk 之后到整个 export 返回之前的无日志区间

### 这轮与上一轮 `segment3 plateau` 的关系

- 上一轮 `cfg32 + no-TraceV` rerun 的 authoritative plateau 在：
  `segment3`
  compute 区域。
- 本轮 `bounce-bypass-enable=1` 的 fresh rerun，则收敛到一个更早的
  `segment0 stage0 tensor2 address2 direct export`
  plateau。
- 这说明：
  1. 主线确实仍存在“卡点看起来会移动”的现象；
  2. 但这次移动**不是**因为真正命中了 bounce path，
     因为现场已经直接证明 `bounce=0` 且没有任何 `bounce-bypass`。

### 下一步建议

1. 暂时停止围绕 `bounce path` 做主线试探。
2. 把主线静态排查和下一轮低扰动探针收敛到：
   - direct export
   - `target_seq=1/address2`
   - page/chunk>0 的中段路径
3. 若必须直接看到 guest 线程停在系统调用还是用户态哪条指令，再切到 `gdbserver` 路线；继续试图用“是否 bounce”来解释当前卡点，信息增益已经很低。

## 2026-04-21 12:26 UTC cfg32 + No-TraceV Mainline Rerun

下面顶部内容优先于后面的旧摘要；当前主线 latest authoritative 结论已经更新到
`2026-04-21 12:26 UTC`
这一轮基于 `cfg32` bitstream、并去掉主线 `TraceV` 依赖后的
`g6 bertmini`
fresh rerun。

### 当前结论

- 本轮使用：
  - cfg32 runtime:
    `config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_g6_cfg32.yaml`
  - cfg32 hwdb / AGFI:
    `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32`
    / `agfi-05624ed0cd6c28034`
  - `tracing.enable: no`
- 主线 `g6` workload 仍然没有自然跑通；它再次收敛到
  `segment3`
  compute 区域的 plateau。
- 与较早的 `segment3` plateau 记录相比，这轮至少可以强说两件事：
  1. **软件侧 `cfg32` 与新的 `cfg31` xlate 保留槽配置没有阻止程序推进到 `segment3`。**
  2. **去掉主线路径上的 `TraceV` 依赖后，stall 仍然存在；因此当前 blocker 不是“主线被 TraceV 卡住”。**

### 本轮现场

- runworkload session:
  `pairdummy-sbus128-runworkload-20260421-114526`
- terminaterunfarm session:
  `pairdummy-sbus128-terminaterunfarm-20260421-122208`
- 远端实例:
  `i-006b0ad1acf3c8623`
  / `192.168.1.67`
- 本地抓取:
  `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260421T122136Z-g6-cfg32-notrace-stall-capture/`
- 关联完整记录:
  [`debug_records/20260421T122600Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260421T122600Z.md)

### 这轮确认了什么

- guest 确实已进入 `pipeline-runtime` 用户程序：
  - `bertmini-batch8.status` 为 `state=running`
  - `runner.stage` 到：
    `after-bin-spawn method=ours2 pid=205 kind=launcher`
  - `bertmini-batch8.log` 已写到约 `595 KiB`
- `segment0 tensor2 export` 旧歧义走廊再次被确认越过：
  - breadcrumb 仍记录到
    `dma_page_direct_path_decided`
    `seg=0 gstage=0 lstage=0 sb=7 tensor=2 tok=2 mgr=0 page=62`
- 这轮新的 guest sparse log 尾部推进到：
  - `segment=3 flush-spm-xlate end`
  - `segment=3 begin stages=3 sinks=1 subbatch_size=1 target_batch=8 target_subbatch=8`
  - `stage-exec-views phase=end segment=3 stage=0 layer=3 rc=0 flush=1`
  - `worker stage=0 subbatch=0 begin op=1 acc=0 dma=0 tiles=2`
  - 多次
    `conv-sync-strided stage=0 mgr=0/1 ... flushed use_pointwise=1`
  - 末尾停在
    `rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1`
- `uartlog` 给出了新的 Linux 内核侧证据：
  - 在 `S99run` 之后出现 `RCU stall`
  - task 为 `rerocc_pipeline`
  - task dump 中一条线程停在：
    `pagemap_pmd_range -> walk_pmd_range -> walk_pud_range -> ...`
  - 说明 guest 内确实有线程卡进了内核 page-table walk / pagemap 相关路径，而不是 manager 纯粹没同步结果。

### 当前对根因的收敛

- 这轮不能把 stall 简化成“就是最后一条 sparse log 对应那一行代码”。
  - sparse log 的最后一条可见日志位于
    `segment3 stage0 pointwise / rr-acquire handoff`
    附近；
  - 但 Linux 内核同时报告某个 `rerocc_pipeline` 线程卡在
    `pagemap_*`
    路径。
- 因而当前更稳妥的说法是：
  **`cfg32 + no-TraceV` 已证明主线能再次到达 `segment3`，但 `segment3` plateau 仍存在；现有直接证据把嫌疑收敛到 `segment3 stage0 pointwise/rr handoff` 与并发线程中的 `pagemap` 相关路径。**

### 当前局限

- 这轮 breadcrumb filter 仍然是：
  - `segment=0`
  - `global_stage=0`
  - `local_stage=0`
- 所以 breadcrumb 这次主要用来再次确认：
  - 旧 `segment0 export` frontier 已被越过
- 它仍不足以直接给出：
  - `segment3 stage0` / `stage2`
    的精确最后 breadcrumb 相位

### 下一步建议

- 下一轮优先对 `segment3` 主线补低扰动观测，而不是再围绕旧 `segment0 slot14` 做文章：
  1. 把 breadcrumb filter 转到 `segment=3`
     且优先覆盖 `stage0 pointwise`
  2. 若继续跑 FPGA，可把 pointwise caller / matmul begin-return / postcall fence 这组 breadcrumb 作为主观测面
  3. 若需要解释 `RCU stall` 与 `segment3 sparse frontier` 的关系，再启用网络化 `gdbserver` 或更窄的 guest 进程线程级观测


## 2026-04-20 Mainline Override

下面顶部内容优先于后面的旧摘要；当前主线 authoritative 结论已经更新到
`2026-04-20 14:16 UTC`
启动、随后人工收尾的这轮
`g6 bertmini`
fresh rerun。

### 2026-04-20 15:42 UTC Static-Audit Correction

- 若希望阅读一份更少缩写、面向读者解释推理过程的版本，优先看：
  `docs/architecture/g6_export_frontier_explained_20260421.md`

- 之前顶部文字里把这轮 fresh rerun 直接写成：
  **停在 `page28 after_accounting` 之后，且早于 `page29 before_v2p`**
  ，这个表述过强，现已修正。
- 当前真正能强说的是：
  - 最后一个**稳定偶数** breadcrumb 仍是
    `seg=0 gstage=0 lstage=0 sb=3 tensor=2 page=28 phase=dma_page_after_accounting`
  - 但 `page29` 对应的关键 breadcrumb 槽
    `slot14`
    在两份 live capture 里都不是空零，而是同一个 odd torn state：
    - `seq=503`
    - `phase=dma_submitwait_after_cleanup`
    - `page=19`
    - `tok=1176`
    - `mgr=0`
- 这件事重要，是因为：
  - breadcrumb 不是时间 ring，而是哈希槽表；
  - slot key 不含 `phase`，也不含 alias `target_seq`；
  - `page29/token0` 的 page-level notes 也正好哈希到 `slot14`
  - 所以“`--all` 里没有稳定的 `page29 before_v2p`”**不能单独证明**
    “控制流一定还没到 `page29`”
- 同时，静态代码审计又表明：
  `page29 before_v2p` 之前的 direct-path 只剩
  - 地址/长度重算
  - `dma_trigger_export_host("pset", ...)`
  - `dma_chunk_needs_bounce(...)`
  而当前 run config 下：
  - `debug_trigger_enable=0`，trigger note 早返回
  - `page29` 不是 sparse page probe 点
  - `page29` 不是默认 chunk-marker 点
  - `src=0x40006000`
    `dst=0x103b31800`
    `chunk=0x400`
    静态上应走 direct path，不该 bounce
- 因而这轮更准确的当前结论应改写为：
  **最后稳定 front 在 `page28 after_accounting`；但 `page29` breadcrumb 证据本身带 torn/collision 歧义，不能再把“早于 `before_v2p`”当成已证实结论。**

### 2026-04-21 01:15 UTC Pre-Rerun Probe Prep

- 为了避免下一轮 rerun 继续把关键判断压在 `page29/token0 -> slot14` 这条歧义路径上，本地已经补上一条新的低扰动 breadcrumb probe：
  - phase：
    `dma_page_direct_path_decided`
  - 位置：
    `dma_chunk_needs_bounce(...)` 判定为 false 之后、`before_v2p` 之前
  - 目的：
    直接判断程序是否已经进入 `page29` 的 direct path
- 这条新 probe 不再沿用 page-level note 的 `token_id=0`，而是利用 export alias wrapper 已有的 `target_seq`：
  - 在 `copy_tensor_pages_to_model_alias_target()` 外层做 scoped token
  - 编码规则：
    `token_id = target_seq + 1`
  - 因而 page-level notes 仍保持 `token=0` 语义不变
- 本地已重新核对当前 `bertmini` 主线里 `tensor_id=2` 的 unique alias targets：
  - `target_seq=0` -> `0x20400`
  - `target_seq=1` -> `0x842400`
- 对当前主线上下文
  `segment=0 stage=0 subbatch=3 tensor=2 manager=0 page=29`
  ，静态计算得到：
  - `target_seq=0` -> `token=1` -> `slot25`
  - `target_seq=1` -> `token=2` -> `slot39`
- 也就是说，下一轮关键 probe 已经不再复用 `slot14`。
- 关联记录：
  - [`debug_records/20260421T011522Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260421T011522Z.md)
  - [`change_records/20260421T011522Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260421T011522Z.md)

### 2026-04-21 01:55 UTC Segment3 Stall Update

- 带新 probe 的 fresh rerun 已经完成到“确认 plateau 后人工 terminate 收尾”的阶段：
  - runworkload session：
    `pairdummy-sbus128-runworkload-20260421-012435`
  - terminaterunfarm session：
    `pairdummy-sbus128-terminaterunfarm-20260421-015240`
  - 远端实例：
    `i-0069b03d475d2dbe6`
    / `192.168.1.239`
  - 本地现场抓取：
    `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260421T015240Z-g6-segment3-stall-capture/`
- 这轮首先确认了一个关键事实：
  **旧的 `segment0 tensor2 export direct-path` 歧义走廊已经被越过。**
  - guest 成功进入 `pipeline-runtime` 用户程序
  - `runner.stage` 记录到：
    `after-bin-spawn method=ours2 pid=198`
  - `runner-proc.stage` 记录到真实命令行：
    `rerocc_pipeline_runtime-linux ...`
  - 新 probe 成功出现在 guest breadcrumb 中：
    - `kind=dma`
    - `phase=dma_page_direct_path_decided`
    - `seg=0 gstage=0 lstage=0 sb=7 tensor=2 tok=2 mgr=0 page=62`
    - `slot=43`
  - 这条证据不再复用旧的 `slot14`
- 因而当前主线 blocker 已经更新为：
  **`segment3` compute 区域的新 plateau**
  - guest sparse log 推进到：
    - `segment3 stage0` fixed-load 完成
    - `worker stage=0 subbatch=1 begin op=1 acc=0 dma=0`
    - 多次 `conv-sync-strided stage=0 mgr=0/1 ... flushed use_pointwise=1`
    - `worker stage=1 subbatch=0 compute-done`
    - `worker stage=1 subbatch=0 done`
    - `worker stage=1 ... waiting phase=entry-full kind=c3-isolate tensor=6`
    - `worker stage=2 subbatch=0 begin op=1 acc=4 dma=4`
    - `conv-sync-strided stage=2 mgr=4/5 ... flushed use_pointwise=1`
  - 之后进入长期 plateau：
    - `guest_sparse=627538`
      长时间不再增长
    - 稀疏日志尾部在两个长窗口内完全一致
    - 当前 breadcrumb 摘要也保持不变
  - 但与此同时：
    - `heartbeat.csv` 继续从
      `1171`
      推进到
      `1629`
    - FireSim manager 一直显示 simulation 仍在运行
    - host watchdog 没有误杀
- 因而这轮新的 authoritative 结论是：
  1. 旧 `segment0 export page29` 不是这轮主线 stall 点
  2. 当前更靠后的稳定 plateau 落在 `segment3` compute / pipe interaction 区域
  3. 下一轮观测点不应再优先围绕旧 `slot14` 展开，而应直接服务于
     `segment3 stage0/stage2`
     的 compute 根因定位
- `runworkload` 本轮退出码：
  `1`
  - 退出原因不是 workload 自然完成
  - 而是人工 terminate 后 manager 在 `monitor_jobs_wrapper` 中失去 SSH banner
- 关联记录：
  - [`debug_records/20260421T015501Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260421T015501Z.md)
  - [`change_records/20260421T015501Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260421T015501Z.md)

### 2026-04-21 04:14 UTC TraceV DMA Window Prep

- 为了避免下一轮继续只靠 coarse worker marker 或稀疏文本日志猜测卡点，
  当前已经在 `prt_dma.c` 中补上了一组更窄的
  `TraceV selector=3` raw instruction markers，
  只覆盖：
  - `stage=0`
  - `tensor=6`
  - `copied==0`
  - 可选 page range：
    `PIPELINE_RUNTIME_DMA_TRACERV_PAGE_START/END`
- 这些 marker 直接围绕当前怀疑最强的
  `segment3 stage0 tensor6 export DMA/RR`
  指令走廊：
  - `program-begin`
  - `program-post-fence`
  - `program-post-dst`
  - `program-post-src`
  - `wait-before-fence`
  - `wait-after-fence`
  - `wait-before-shared-fence`
  - `wait-after-shared-fence`
  - `batch-release-begin`
  - `batch-release-end`
- 对应精确匹配值：
  - `0x00018013`
  - `0x00020013`
  - `0x00028013`
  - `0x00030013`
  - `0x00038013`
  - `0x00040013`
  - `0x00048013`
  - `0x00050013`
  - `0x00058013`
  - `0x00060013`
- 静态复核已补上一处关键覆盖缺口：
  - 这组 DMA-window marker 现在同时覆盖
    `direct-path` 与 `bounce-path`
  - 因而下一轮若目标页因为 `mod64` 不匹配而走 bounce，
    也不会再得到“marker 没触发但其实只是路径没覆盖”的假阴性
- `tracerv-inst` workflow 也已补上两项基础设施，避免手改 runtime yaml：
  - `PAIRDUMMY_TRACERV_START_INST`
  - `PAIRDUMMY_TRACERV_END_INST`
  - workflow 会生成临时 effective runtime config，再交给 FireSim manager
- local image freshness 也已扩充，下一轮可以在 rerun 前直接检查：
  - worker marker 是否存在
  - 新 DMA-window marker 是否存在
- 当前新的建议执行顺序是：
  1. 先固定 page range，优先复用历史签名页
     `page=124`
  2. 用 `program-post-src -> wait-before-fence`
     判断 submit 路径是否完整退休
  3. 若已越过，再用
     `wait-before-fence -> wait-after-fence`
     与
     `wait-before-shared-fence -> wait-after-shared-fence`
     二分
  4. 只有前两段都能退休，才继续看
     `batch-release-begin -> batch-release-end`
- 关联记录：
  - [`debug_records/20260421T035237Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260421T035237Z.md)
  - [`change_records/20260421T035237Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260421T035237Z.md)
  - [`docs/plans/segment3_stage0_tensor6_tracerv_dma_window_plan_20260421.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/plans/segment3_stage0_tensor6_tracerv_dma_window_plan_20260421.md)

- 当前活跃线程仍是：
  `pipeline-runtime mainline`
- 当前最新一轮 run：
  - runworkload session：
    `pairdummy-sbus128-runworkload-20260420-141608`
  - 远端实例：
    `i-01d398695bdc22fd6`
    / `192.168.1.67`
  - profile：
    `pairdummy-sbus128-g6-fixed-v1`
  - 结果目录：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-20--14-16-10-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-g6-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync-g6/`
  - live snapshots：
    - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T143126Z-g6-live-capture`
    - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T144843Z-g6-live-capture`
- 这轮 freshness 已通过，且这是 export `v2p` probes 的第一轮有效 fresh rerun
  - local freshness 通过
  - remote freshness 通过
  - runtime binary hash 与本地构建一致
  - guest env 与本地渲染一致
  - 因而这轮 guest binary 确实已经带上
    [`change_records/20260420T121216Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260420T121216Z.md)
    中加入的
    `dma_page_before_v2p / dma_page_after_v2p`
    probes
- 这轮再次真实验证：
  `host-watchdog`
  不是主线 blocker
  - watchdog 在
    `2026-04-20 14:22:57 UTC`
    已 armed
  - `guest_sparse`
    从
    `1011`
    推进到
    `198085 -> 240481 -> 323227`
  - plateau 后 heartbeat 仍从
    `425`
    持续推进到
    `1912`
  - watchdog 最后仍记录：
    - `idle=625s hb_idle=0s`
    - `idle=844s hb_idle=0s`
    - `idle=1063s hb_idle=0s`
  - host 没有误杀；
    本轮停机是人工按 SOP terminate
- 这轮 guest 已明确进入
  `rerocc_pipeline_runtime-linux`
  用户态执行
  - live status 仍是：
    `state=running`
    / `exit_code=` 空
  - wrapper shell 处于
    `do_wait`
  - runtime binary pid：
    `196`
- 这轮最新 run 的 **局部** authoritative frontier
  仍是：
  **`segment=0 stage=0 subbatch=3 tensor=2` 的 export host-copy page loop**
  - 两次 live capture 的 last-slot 都是：
    - `kind=dma`
    - `phase=dma_page_after_accounting`
    - `seg=0 gstage=0 lstage=0 sb=3 tensor=2 page=28`
    - `src=0x40005c00 dst=0x103b31400 aux0=0x400`
    - `line=1000`
  - decode 文件：
    - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T143126Z-g6-live-capture/breadcrumb.decode.txt`
    - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T144843Z-g6-live-capture/breadcrumb.decode.txt`
  - 这说明 page28 的：
    - `submit`
    - `wait`
    - `cleanup`
    - `copied += chunk`
    - `remaining -= chunk`
    都已完成
  - 但这里不能再直接下结论说
    “一定早于 `page29 before_v2p`”
  - 因为：
    - `slot14` 原始值在两份 capture 里都是同一个 odd torn state
    - `slot14` 也是 `page29/token0` page-level note 的目标哈希槽
    - breadcrumb slot key 不含 `phase`，也不含 alias `target_seq`
  - 所以当前只能安全表述为：
    **最后稳定偶数 breadcrumb 仍停在 `page28 after_accounting`，
    而 `page29` 的 breadcrumb 证据目前是模糊的**
- `trigger.log` 当前仍是 `0`，
  但这轮配置里
  `debug_trigger_enable=0`
  - 所以“没有 trigger 输出”在这轮是预期现象
  - 它不能被解读成
    `pset / v2p / bounce`
    没有执行
- 这条 fresh-run 结论**不能**覆盖 older run 里更靠后的 segment 进度
  - `pipeline-runtime` 的 segment/action 是串行执行的：
    [`src/prt_runtime.c`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c#L4864)
    逐个 `seg_idx` 建 action、启动该 segment 的 stage worker，
    在
    [`src/prt_runtime.c`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c#L5072)
    等到 `done_subbatch >= target_subbatch` 后，
    才在
    [`src/prt_runtime.c`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c#L5109)
    release 当前 action 并进入下一个 segment
  - 因此 older run 里记录到的
    `segment=3 stage=2 compute-done`
    仍然明确比这轮
    `segment=0 ... page28`
    更靠后
  - 当前正确解释是：
    新 breadcrumb 只锚定了**这轮 fresh rerun 自己**的局部冻结点；
    它说明本轮确实更早冻结，
    但不推翻 older run 曾到达 `segment=3` 的事实
  - 所以“观测/时序扰动会改变冻结点”仍然是活跃假设
- 当前 g6 mapping 仍对齐到：
  - `segment0`
  - `globalStageId=0`
  - `layerIdList=[0]`
  - `exportTensorIdList=[2]`
  - `vAccIdxList=[[0,1,2,3]]`
  - RR cfg：
    - DMA / opcode2 -> `cfg0`
    - Gemmini / opcode3 -> `cfg1`
- 对这轮 fresh frontier，
  静态优先级已经再次切换：
  1. `page29` 迭代开头的地址/长度重算
  2. `dma_trigger_export_host("pset", ...)`
  3. `dma_chunk_needs_bounce(...)`
  4. 只有在真正看到
     `before_v2p`
     之后，
     才重新把
     `prt_host_virt_to_phys()`
     升回主嫌
- 当前可以先降级的旧嫌疑：
  - page28 自己的
    `hw_dma_fence()` /
    `rr_fence(cfg0)` /
    `release`
    没退完
  - `v2p` 内部的
    `/proc/self/pagemap`
    访问卡死
  - page29 bounce path 仍是低优先级；
    下一页地址应为
    `src=0x40006000`
    `dst=0x103b31800`
    `chunk=0x400`
    ，静态上仍满足 direct path 对齐条件
- `g6` artifact 仍已确认是：
  - `num_gemmini=6`
  - `num_dma=6`
- 关于 cfg 风险，当前结论不变：
  - 当前 g6 每个 segment 最多 `4` 个 local stages
  - 当前 g6 不会因为某个大 `globalStageId`
    就静态撞到
    `cfg15`
  - `cfg15`
    仍只保留为 future artifact budget 风险，
    不是这轮 stall 的首要解释
- 当前 run 已按 SOP 收尾：
  - `terminaterunfarm` session：
    `pairdummy-sbus128-terminaterunfarm-20260420-145330`
  - `terminaterunfarm` exit code：
    `0`
  - EC2 状态已确认：
    `terminated`
- 当前下一步优先级：
  1. 静态审计
     `segment0 stage0 tensor2`
     的
     `page28 after_accounting -> page29 before_v2p`
     这段更窄走廊
  2. 修正 breadcrumb 证据强度：
     不再把
     “当前 ring 里没有稳定的 `before_v2p`”
     直接当作
     “控制流没到 `page29`”
  3. 若需要下一轮 rerun，
     优先设计一个**不复用 `slot14` 等价类**的低扰动判别点，
     用来区分：
     - 真没到 `page29 direct-path`
     - 还是到了，但 breadcrumb 因 torn/collision 不可信
  4. 只有在新证据真正证明控制流已到
     `before_v2p`
     之后，
     才继续追
     `prt_host_virt_to_phys()` /
     `rr_release_scope(cfg0)`
  5. 在用户同意前，
     不再启动新的
     `launchrunfarm / infrasetup / runworkload`

## 当前目标

- 主线：
  `12-pair sbus128` dummy-model 上继续推进
  `bertmini batch8 file-only`
- 方法：
  固定 workflow + freshness 闭环 + guest-file-first + breadcrumb
- 调试策略：
  先静态读代码与 artifact，
  再读 capture，
  最后才加新的窄 probe；
  新一轮窄 probe 默认走
  `trigger-gated short log`
  而不是直接回到 deep log；
  当前继续走 FPGA/FireSim 软件调试，
  不做 bitstream 重建

## 当前 authoritative 结论

- 当前仍不改硬件；
  本轮只做 FPGA/FireSim 软件调试、
  静态审计和低扰动观测收敛。
- fixed profile 已升级到
  `pairdummy-sbus128-fixed-v20`，
  且
  [`src/prt_trigger_log.c`](../src/prt_trigger_log.c)
  的 strict-trigger 语义已经生效：
  命中前若事件不在目标维度内，
  直接返回，
  不再做
  `seq/format_line/ring_push`。
- `v20` control rerun
  已完成 manager 侧收尾；
  这轮的 manager 失败是基础设施侧
  `Error reading SSH protocol banner`，
  不是 guest panic / kernel crash。
  对应 EC2
  `i-0cfb64404d409fed9`
  已确认
  `terminated`。
- 本轮最重要的有效结论不是
  `page28`
  新前沿，
  而是：
  旧的
  `sb3/page24`
  frontier
  已经被穿过，
  先前把它当成当前 blocker 的结论已失效。
- 直接证据有三条：
  1. trigger log
     恰好只有
     `33`
     行，
     等于
     `match line + post_budget(32)`；
     因而最后一行
     `sb=3 page=28 ph=pset`
     是 capture cutoff，
     不是 authoritative frontier。
  2. sparse log 明确出现：
     - `c2-export stage=0 tensor=2 phase=copy-end idx=0 subbatch=3 rc=0`
     - `worker stage=0 subbatch=3 done`
     - `worker stage=0 subbatch=4 begin`
     - `c2-export stage=0 tensor=2 phase=copy-begin idx=0 subbatch=4`
  3. breadcrumb `--all`
     中已有
     `sb=4 tensor=2 page=63 dma_submitwait_after_cleanup`
     /
     `dma_page_end`
     以及
     `sb=4 rr_release_end`
     记录，
     说明 capture 时 guest 至少已经推进到
     `subbatch=4`
     的 export 后段 / cleanup 窗口。
- 因此：
  - 之前“新前沿在
    `page28 pset -> v2p-b`
    之间”的说法无效
  - 当前更可信的表述是：
    这轮 strict-trigger control rerun
    证明旧的
    `sb3/page24`
    卡点已被穿过，
    但新的 blocker 还未被重新精确定义
- 对旧窗口
  `sb3/page24..28`
  的静态审计结论：
  - 在
    [`src/prt_dma.c`](../src/prt_dma.c)
    的 export host 直通路径里，
    `pset -> v2p-b`
    之间没有新的硬件指令，
    只剩：
    `dma_chunk_needs_bounce()`
    和紧邻其后的
    `prt_host_virt_to_phys()`
  - 结合本轮实际地址：
    `src mod64 == dst mod64 == 0`
    （page24..28 及 page63 均如此），
    `dma_chunk_needs_bounce()`
    为
    `false`；
    旧窗口不会走 bounce path，
    也没有暴露新的硬件语义错位
  - `req.src_acc == req.dst_acc == manager_id`
    与外层
    RR scope
    的 acquire/release 语义保持一致，
    当前看不出与
    `HybridMapper`
    / pair-manager
    设计约束冲突的静态证据
- 本轮新增一个纯软件工具修正：
  [`scripts/triage_prt_capture.py`](../scripts/triage_prt_capture.py)
  现在会解析
  `guest-trigger-log.txt`，
  并在
  `trigger_line_count == post_budget + 1`
  时明确提示：
  当前 trigger log 已打满预算，
  最后一行只能算 capture 截断点，
  不能直接当 frontier。
- 当前 blocker 排查 SOP 继续固定为：
  1. artifact audit
  2. 静态读代码
  3. `triage_prt_capture.py`
  4. 只有静态窗口仍不够细时，才用
     `triage_prt_capture.py --emit-trigger-env`
     生成 trigger overlay
  5. 单变量 rerun
- 下一轮不应再把 trigger 钉在
  `sb3/page24`；
  优先重定向到更靠后的窗口：
  - 首选：
    `dma-export sb=4 tensor=2 page=63`
    并适当增大
    `POST_BUDGET`
    （如
    `64`
    或
    `96`）
  - 备选：
    runtime family 仅钉
    `subbatch=4`
    的 worker 边界
  - 仍然先静态读代码，
    再做单变量 rerun

- `v15`
  live run
  `192.168.1.206`
  已把 export 路径缩到：
  `tensor=2 page=63 dma_page_end`
  之后的极窄窗口：
  - `dma_batch_scope_release(&scope)`
  - `prt_rr_release_scope(scope)`
  - `rr_release(cfg)`
  - 以及上层
    `c2-export copy-end/retire`
- 这已经把旧的三类 export 候选清掉：
  1. `dma_batch_scope_acquire()` /
     `prt_rr_acquire_scope()` 返回边界
  2. export page0 的
     `prt_host_virt_to_phys((const void *)dst_ptr, &dst_pa)`
  3. page0 首个
     `dma_submit_wait_annotated_scoped()` /
     `prt_dma_submit()` /
     `prt_dma_wait()`

- `v16`
  通过新增
  `RR_RELEASE_BEGIN/END`
  breadcrumb
  继续推进了同一条 live 主线：
  - profile：
    `pairdummy-sbus128-fixed-v16`
  - host：
    `192.168.1.68`
  - sparse log 已明确出现：
    - `c2-export stage=0 tensor=2 phase=copy-end idx=0 subbatch=3`
    - `c2-export stage=0 tensor=2 phase=retire idx=0 next_sbatch=4`
    - `worker stage=0 subbatch=3 done`
    - `worker stage=0 subbatch=4 begin`
    - `pointwise-matmul-fallback ... mgr=0..3 ... begin/end`
    - `conv-sync-strided ... mgr=0..3 ... fence-end rc=0`
- 因而新的 authoritative 结论是：
  export 侧的
  `rr_release`
  不是 blocker；
  当前前沿已经后移到
  `stage0/subbatch4`
  pointwise fallback
  更晚的路径

- 对 pointwise split-OC 路径做了静态审计：
  - `split_1d_range()`
    与 host 参考实现一致
  - 子 conv 只对
    `out_channels / weights / bias / output`
    做
    `oc_beg`
    偏移，
    仍保留 full
    `in_stride=256`
    `weight_stride=256`
    `out_stride=256`
  - 这与
    `HybridMapper`
    的按
    `OC`
    切分语义、
    以及
    [`runtime_mechanisms.md`](runtime_mechanisms.md)
    /
    [`alignment_constraints.md`](alignment_constraints.md)
    中当前 runtime 兑现方式对齐；
    静态上没看到
    `tile4`
    指针计算本身的错误

- 若把 `v16` sparse frontier 当真，
  当前静态窗口已经非常窄：
  从
  `conv-sync-strided stage=0 mgr=3 fence-end rc=0`
  到下一条可见的
  `oc-split-pointwise ... end`
  /
  `mgr4 begin`
  之间，
  实际有语义动作的只剩：
  - `prt_rr_release_scope(&scope)`
  - 返回到
    `conv_call_for_manager_sync_strided()`
  - 回到外层
    `oc-split-pointwise ... end`
    和下一轮
    `mgr4`
    入口

- 但这轮又发现了新的 observability 问题：
  `v16`
  同一份 capture 内，
  sparse log 与 breadcrumb 前沿不一致
  - sparse log 文件尾部只到：
    `subbatch=4 mgr=3 fence-end`
  - breadcrumb 却显示：
    `subbatch=4 mgr=6 gemmini_pointwise_call_begin`
- 静态阅读
  [`src/prt_breadcrumb.c`](../src/prt_breadcrumb.c)
  后确认根因：
  旧实现的 breadcrumb slot 只按
  `stage`
  选槽，
  所有
  `stage 0`
  的 Gemmini / RR / DMA 事件都在争抢同一个 slot；
  在当前 live 并发下，
  `last_phase`
  不再可靠

- 本轮已做纯软件、低扰动修正：
  - [`src/prt_breadcrumb.c`](../src/prt_breadcrumb.c)
    - breadcrumb slot 现在按
      `stage/subbatch/kind/manager/tensor/page/token`
      做稳定 hash，
      不再把整个
      `stage 0`
      压到单槽上
  - [`scripts/decode_prt_breadcrumb.py`](../scripts/decode_prt_breadcrumb.py)
    - `--all`
      输出改为按
      `seq`
      排序，
      便于直接看最新局部前沿
  - [`scripts/pairdummy_sbus128_fixed_env.sh`](../scripts/pairdummy_sbus128_fixed_env.sh)
    - profile 升级到
      `pairdummy-sbus128-fixed-v17`
    - 当前固定 profile 关闭了
      fixed-load / export
      高频 probe，
      回到
      breadcrumb-only
      的低扰动观察面

- 继续观察 `v17`
  live run 后，
  又获得了比上一轮更窄的新前沿：
  - `heartbeat.csv`
    在
    `724s -> 941s`
    期间继续推进
  - `bertmini-batch8.log`
    大小固定在
    `368377`
    bytes
  - sparse 文件尾始终停在：
    `conv-sync-strided stage=0 mgr=7 flushed use_pointwise=1`
  - 新的 static-first 结论因此变成：
    当前可疑窗口已经前移到
    `mgr7 flushed`
    之后、
    `dispatch=pointwise`
    之前

- 对这段新窗口做静态阅读后确认：
  在当前 fixed profile 下，
  这段路径里已经没有新的硬件语义动作；
  主要只剩：
  - 若干不会真正落盘的 marker / crit 调用
  - coarse guest log append
    `dispatch=pointwise`
  - 以及随后的
    pointwise call-begin breadcrumb
- 这意味着当前第一嫌疑从“硬件 release 语义”
  转成了
  “pointwise 热路径里剩余 coarse guest log 自干扰”

- 本轮又补跑了静态 artifact 审计：
  - `segments=13`
  - `split_kind_coverage={'oc': 32, 'resadd_spatial': 6, 'single': 2}`
  - `op_type_coverage={'conv': 32, 'resadd': 8}`
  - 结果：
    PASS

- 因而本轮做了新的纯软件、低扰动修正：
  - [`src/prt_gemmini_adapter.c`](../src/prt_gemmini_adapter.c)
    - 当 breadcrumb 已启用时，
      关闭 pointwise 热路径内已被 breadcrumb 覆盖的 coarse guest log
  - [`scripts/pairdummy_sbus128_fixed_env.sh`](../scripts/pairdummy_sbus128_fixed_env.sh)
    - profile 升到
      `pairdummy-sbus128-fixed-v18`
    - 关闭
      `PIPELINE_RUNTIME_CRITICAL_UART_PROBE`
      以去掉当前 profile 下本来就不会输出的
      `CRIT`
      探针格式化开销

- `v18`
  已完成部署闭环：
  - `image-closure`：
    通过
  - local freshness：
    通过
  - remote freshness：
    `192.168.1.14`
    通过
  - runtime binary sha256：
    `36e85f2a3e357f6d2b0fc79a741120f6566effebc3c140b36e56b8be325d0a97`
  - firemarshal env sha256：
    `04aa1fe0bb9e00d992924adb8c751f4fe16ed7138cd2d816c600e218edbd285e`
  - remote image sha256：
    `eb8ada339a11d77f2c61268e95f3cb26c91c61fd1d976dc768e3de1efe1dc9a3`

- `v18`
  重新拉起 run farm 时，
  `launchrunfarm`
  曾因 AWS `f2.6xlarge`
  容量不足在多个 subnet 间重试；
  这不是新的 runtime blocker

- 当前 run 状态：
  - 最近一次 control rerun：
    - host：
      `192.168.1.14`
    - session：
      `pairdummy-sbus128-runworkload-20260414-150549`
    - results dir：
      `sims/firesim/deploy/results-workload/2026-04-14--15-05-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
    - manager log：
      `sims/firesim/deploy/logs/2026-04-14--15-05-50-runworkload-UIKVAB93PDHR9S5I.log`
    - pane：
      `tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260414-150549.pane.log`
    - official capture prefix：
      `tmp/firesim-aws-f2/captures/pairdummy-sbus128-runworkload-20260414-150549-192.168.1.14-host-watchdog-20260414T152611Z`
    - tmux exitcode：
      `1`
  - 这次退出是 manager 侧 SSH banner 中断，
    发生在官方 capture 已落盘之后；
    当前不要把它记成新的 guest root cause。
  - 下一轮 rerun 前，
    按固定 FireSim 约束重新做：
    `launchrunfarm -> infrasetup -> runworkload`
    不直接复用这次中断 run 的 manager 状态。

## 当前固定配置

- profile：
  `pairdummy-sbus128-fixed-v19`
- target：
  `rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128`
- batch：
  `8`
- `NUM_CORES=4`
- `NUM_GEMMINI=12`
- `NUM_DMA=12`
- `PAIR_MANAGER_MODE=1`
- `DUMMY_GEMMINI_MODE=1`
- `PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD=1`
- `PIPELINE_RUNTIME_SKIP_INPUT_LOAD=1`
- `PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK=1`
- `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
- `PIPELINE_RUNTIME_UART_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
- `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_CRITICAL_UART_PROBE=0`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=1`
- `PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`
- 当前 profile 的目的：
  在不回到高频文本 probe 的前提下，
  让 pointwise 热路径回到
  breadcrumb-first
  观察面，
  避免 coarse guest log
  在同一窗口里继续自扰；
  trigger-gated 短日志默认保持关闭，
  只在 triage 明确建议后做单变量 overlay

## 当前关键文档

- 总纲：
  [`project_guide.md`](project_guide.md)
- 硬约束：
  [`constraints/hard_constraints.md`](constraints/hard_constraints.md)
- 固定流程：
  [`workflows/pairdummy_sbus128.md`](workflows/pairdummy_sbus128.md)
- 观测机制：
  [`testing/observability.md`](testing/observability.md)

## 当前最新记录

- debug：
  [`../debug_records/20260414T103114Z.md`](../debug_records/20260414T103114Z.md)
- change：
  [`../change_records/20260414T103114Z.md`](../change_records/20260414T103114Z.md)
- debug：
  [`../debug_records/20260414T125526Z.md`](../debug_records/20260414T125526Z.md)
- change：
  [`../change_records/20260414T125526Z.md`](../change_records/20260414T125526Z.md)
- debug：
  [`../debug_records/20260414T132728Z.md`](../debug_records/20260414T132728Z.md)
- change：
  [`../change_records/20260414T132728Z.md`](../change_records/20260414T132728Z.md)
- debug：
  [`../debug_records/20260414T144739Z.md`](../debug_records/20260414T144739Z.md)
- change：
  [`../change_records/20260414T144739Z.md`](../change_records/20260414T144739Z.md)
- debug：
  [`../debug_records/20260414T153308Z.md`](../debug_records/20260414T153308Z.md)
- change：
  [`../change_records/20260414T153308Z.md`](../change_records/20260414T153308Z.md)

## 下一步

1. terminate 这次中断 run 对应的 run farm，
   然后重新：
   `launchrunfarm -> infrasetup`
2. 在单独 subshell 中叠加下面这组单变量 trigger：
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=1`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND=dma-export`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH=3`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID=2`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE=24`
3. 先跑：
   `pairdummy_sbus128_workflow.sh debug-preflight`
4. 通过后再做单变量 rerun，
   只看：
   - `heartbeat.csv`
   - `bertmini-batch8.status`
   - `bertmini-batch8.breadcrumb.bin`
   - `bertmini-batch8.trigger.log`
5. rerun 后继续按
   “静态读代码 -> capture -> 更窄软件 probe”
   顺序推进；
   继续禁止硬件修改
