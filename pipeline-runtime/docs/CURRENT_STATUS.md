# Current Status

更新时间：`2026-04-02`

## 冻结结论

- 当前 `bertmini` 主线已经不再是 “Linux/F2 上 DMA 卡死”。最新可信结论是：**当前 mainline 已能完整执行完成，但 final golden mismatch**。
- 当前 golden mismatch 调查暂时搁置。除非新的 fresh run 明确从“执行完成”退化回“卡死”，否则不要把当前状态重新写回旧的 DMA submit hang。
- 当前交接默认应视为“没有需要继续挂着跑的实验”；恢复实验前，先重新核对 manager 状态和 EC2 实例，确保 run farm 已完全回收。

## 当前最可信主线结果

冻结 workload / 配置：

- runtime config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_reloadlogs.yaml`
- hwdb:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb.yaml`
- build recipe:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_coupleddma_10mhz.yaml`
- workload json:
  `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync.json`

冻结结果目录：

- result dir:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/`
- coarse log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/bertmini-batch8.log`
- deep log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/bertmini-batch8.deep.log`
- status:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/bertmini-batch8.status`
- uartlog:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/uartlog`

这轮结果的关键证据：

- `bertmini-batch8.log` 已明确到达：
  - `segment=31 threaded backend complete target_subbatch=8`
  - `golden mismatch: tensor=48 bytes=65536 mismatch=1823 first_idx=12 actual=8 golden=119 max_abs=135`
  - `golden mismatch summary: tensors=1 total_mismatch=1823 first_tensor=48 ...`
  - `runtime_run failed: mismatch (-13)`
- `bertmini-batch8.status` 已明确记录：
  - `state=finished`
  - `exit_code=1`
  - `uart_log_enable=0`
  - `guest_log_enable=1`
  - `guest_deep_log_enable=1`
- `uartlog` 已明确记录：
  - `Simulation complete.`
  - `*** PASSED *** after 44898716942 cycles`
  - `Script done on 2026-04-02 12:31:53+00:00 [COMMAND_EXIT_CODE="0"]`

当前正确解释：

- FireSim/guest/关机回收链路都已经跑通。
- 当前主线 failure 发生在 runtime final compare，而不是 Linux 启动、DMA submit、export wait、或 guest poweroff 阶段。
- 因而当前最准确的状态表述是：
  **“bertmini mainline completes, but current CPU-derived golden disagrees at tensor 48.”**

## 当前稳定工作负载

### 1. 主线回归 workload

- 名称：
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
- 用途：
  - 回归 Linux/F2 启动是否正常
  - 回归文件日志链路是否正常
  - 回归 `segment=31` 能否完整结束
  - 回归当前 failure 是否仍停留在 `tensor=48` mismatch
- 当前预期：
  - 应完成运行并触发 FireSim copy-back
  - 允许仍然返回 `tensor=48` mismatch

### 2. 小 Linux smoke

- 结果目录：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-20--06-16-29-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles/`
- 当前仍可采信的 marker：
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`
  - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS`
  - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1`
  - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
  - `ALL_TESTS_PASS`
  - `COMMAND_EXIT_CODE="0"`
- 用途：
  如果未来又怀疑 FireSim infra、Linux bring-up、CoupledDMA 基本路径或 nonblocking 小回归坏掉，先回到这条 workload 对照，不要直接跳进 `bertmini` 主线。

## 当前稳定技术事实

### 硬件 / 运行时配置

- 当前主线 `default_hw_config` 是：
  `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
- 当前主线 AGFI 是：
  `agfi-06eb561d00d5c5dc1`
- 当前主线 TARGET_CONFIG 是：
  `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`

### 当前数据类型与 padding 语义

- 当前 Gemmini 基本配置不是“8-bit input / 16-bit output”。
- 当前静态代码显示：
  - `inputType = SInt(8.W)`
  - `accType = SInt(32.W)`
  - `spatialArrayOutputType = SInt(20.W)`
- 当前卷积越界语义是 **zero padding**，不是 edge clamp。

### 当前同步语义

- 当前 runtime 在 `spm_xlate_enable=1` 的 scene 下，会把 `sync_mode` 强制收敛到 `blocking_debug`。
- 结果是当前主线 scene 会进一步强制：
  - `dma_backend = PRT_DMA_BACKEND_BLOCKING_FENCE`
  - `gemmini_mode = PRT_GEMMINI_MODE_BLOCKING_FENCE`
- 因而当前 `bertmini` 主线不要再按“异步 overlap 已完全打开”的前提去推理。

### 当前 `num_cores` 语义债

- 当前 runtime 里的 `num_cores` 不是“纯 CPU/hart 数”的干净语义。
- 现状是它被同时拿去表示：
  - CPU/hart 相关上界
  - accelerator slot / local acc domain 数
  - page allocator / `pages_per_acc` 的分配域数
- 这也是当前代码里会强制
  `num_cores >= num_gemmini_mgrs`
  的直接原因；这条绑定来自实现残留，不是冻结的架构要求。
- 当前静态证据包括：
  - runtime init 会直接把 `num_cores` 拉高到不少于 `num_gemmini_mgrs`
  - fallback manager 选择在未显式绑定时会走 `stage_idx % num_cores`
  - page allocator、SPM PT pool、page leak 检查都按 `num_cores * pages_per_acc` 建模
- 因而后续不要把这条绑定解释成：
  “CPU 数必须和 Gemmini 数绑定”。
- 更准确的解释是：
  当前实现还没有把
  `num_cpu_harts`
  、
  `num_acc_slots/page_domains`
  、
  `num_gemmini_mgrs/num_dma_mgrs`
  彻底拆开。
- 这和更高层目标并不矛盾：
  当前代码本身已经独立保留了
  `PRT_MAX_CORES=64`
  、
  `PRT_MAX_ACTIONS=6`
  、
  `PRT_MAX_STAGES=128`
  这些上限；后续如果恢复架构性重构，正确方向应是拆语义，而不是继续拿 `num_cores` 代表一切。

### 当前日志策略

- 当前主线结论建立在“bin/runtime 日志走 guest 文件，而不是 UART”这个前提上。
- 当前冻结结果的 status 已明确是：
  - `uart_log_enable=0`
  - `guest_log_enable=1`
  - `guest_deep_log_enable=1`
- 当前粗粒度日志文件：
  `/root/pipeline-runtime-debug/bertmini-batch8.log`
- 当前细粒度日志文件：
  `/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
- 当前细日志支持按 `segment/global_stage/local_stage/subbatch` gating；后续如果只想看某个卡点附近，优先用 gating 缩范围，而不是重新把 bin 日志打回 UART。

## 当前 golden mismatch 为什么先搁置

当前 mismatch 是真实现象，但它不是“已经证明 RTL/硬件错误”的充分证据，原因至少有三层：

- 当前 runtime artifact manifest 仍是 `mode: fresh`，而 fresh 导出链只明确重新生成了 `runtime_model.bin` 和 `runtime_input.bin` 的 dummy 数据路径。
- 当前 `golden.*.bin` 仍是 host closure 里的 CPU backend 生成物，而不是来自独立硬件真值源。
- 当前 runtime 里仍有调试期硬编码语义：
  - conv activation 临时统一按 `RELU`
  - conv output scale 临时固定为 `1.0`
  - resadd 的 `A/B/C_scale` 固定为 `1.0`
  - resadd `relu=0`

所以当前更准确的说法是：

- 已经证明 FPGA backend 与当前 CPU reference 在这批 synthetic bertmini-shape artifacts 上存在分歧。
- 但还没有证明分歧一定来自 Gemmini RTL、CoupledDMA RTL、或当前硬件配置。

当前冻结策略：

- **golden mismatch 暂不继续深挖。**
- 只有当主线执行完成能力再次稳定复现后，才按“重新生成 golden -> 重新 build/install image -> fresh rerun”的顺序恢复这条调查。

## 保留下来的旧 blocker 证据

- `2026-04-01` 的旧 mainline hang 证据依然保留为“历史 regression 签名”，但不再是当前状态本身。
- 最有代表性的旧 capture 仍是：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/bertmini-b8-fileonly-sync6-run-manualmon3-20260401-192.168.1.44-host-watchdog-20260401T152744Z.guest-deep-log.txt`
- 它冻结的旧边界是：
  `segment=3 stage=0 tensor=6 page=124 ... submit-begin -> [prt-marker] dma`
- 这条旧证据现在只用于：
  如果未来 fresh run 再次回退成 hang，可用来判断是否退回了旧 submit-window regression。

## 如果未来恢复 mismatch 调查

按下面顺序恢复，不要跳步：

1. 先修掉 host `pipeline_runtime` 全量构建里的现存告警/`-Werror` 阻塞。
2. 重新跑 host closure，刷新 `golden.*.bin`。
3. 重新 `marshal build` / `marshal install`。
4. 在跑 `infrasetup` 或 `runworkload` 前，重新核对 image/rootfs freshness。
5. 再做新的 FireSim run。
6. 每轮 run 结束后，立刻回收 run farm。
