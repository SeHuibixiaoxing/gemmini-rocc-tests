# Blockers And Lessons

本文件只保留当前仍有效的硬约束、关键教训和恢复顺序。
旧的逐小时时间线不再写回主文档；历史细节请看 `docs/archive/2026Q1_history.md`。

## 执行硬约束

- 只用 `f2.6xlarge`。
- FireSim 只走 manager 正规流：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- FireSim manager 命令只通过：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- FireMarshal 只通过：
  `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
- 执行 FireSim manager 命令前，必须先：
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh --skip-ssh-setup`
- 每次 `infrasetup` 或 `runworkload` 前，必须先核对 workload image/rootfs freshness。
  最少核对：
  - workload json 里的 `common_bootbinary` / `common_rootfs`
  - 对应 FireMarshal `build` / `install` 日志
  - 目标 image 的路径、mtime、size
  最好再补：
  - `sha256sum`
- 任何 image/rootfs/binary/AGFI 变化之后，下一轮都要重新跑 `infrasetup`。
- 任何 run 被打断、超时或人工停止之后，下一轮也必须重新跑 `infrasetup`。
- 每轮 run 结束、失败或人工中断后，先保留结果目录，再立刻 `terminaterunfarm --forceterminate`，并继续核对 EC2 状态直到实例不再 `running`。
- 不要只看 manager exit code。至少同时联查：
  - `uartlog`
  - `heartbeat.csv`
  - `bertmini-batch8.log` / `bertmini-batch8.deep.log`
  - 结果目录中的 `status`

## 日志纪律

- Linux 启动日志可以继续走 UART。
- 但 bin/runtime 日志默认不要走 UART；当前主线策略是文件日志优先。
- 粗粒度日志：
  `/root/pipeline-runtime-debug/bertmini-batch8.log`
- 细粒度日志：
  `/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
- 如果要追某个卡点，优先开 segment/stage/subbatch gating，而不是往 UART/stdout 再塞更多输出。
- 当前细日志 gating 入口已经存在：
  - `--deep-log-segment`
  - `--deep-log-global-stage`
  - `--deep-log-local-stage`
  - `--deep-log-subbatch`
  - `--deep-log-stage-radius`
  - `--deep-log-subbatch-radius`
- Linux 启动本来就慢。只要没有 panic/crash，且 heartbeat 或启动日志还在前进，就不要把安静窗口误记成新的 boot blocker。

## 当前冻结 blocker

- 当前 mainline blocker 不是 DMA hang，而是：
  **`bertmini` 在 Linux/F2 上完整执行完成后，final golden mismatch 停在 `tensor=48`。**
- 冻结证据在：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/`
- 关键 verdict：
  - `segment=31 threaded backend complete target_subbatch=8`
  - `golden mismatch: tensor=48 bytes=65536 mismatch=1823 ...`
  - `state=finished`
  - `exit_code=1`
  - `Simulation complete.`
  - `*** PASSED *** after 44898716942 cycles`
  - `COMMAND_EXIT_CODE="0"`

## 当前最重要的教训

- 当前主线已经证明：
  - Linux/F2 启动链路可以通过
  - 文件日志链路可以通过
  - `segment=31` 可以完整结束
  - guest 可以正常触发关机和 FireSim copy-back
  所以不要继续把当前主线默认写成 “export DMA infinite wait”。

- 当前 golden mismatch 先不要直接解释成 RTL 错误。
  当前更准确的解释是：
  FPGA backend 与当前 CPU-derived reference，在 synthetic bertmini runtime artifacts 上出现了分歧。

- 当前 reference/golden 链不是绝对真值源。
  原因包括：
  - manifest 仍是 `mode: fresh`
  - fresh 导出链明确会走 dummy runtime data 生成路径
  - `golden.*.bin` 来自 host closure 里的 CPU backend
  - runtime 里仍有调试期硬编码语义：
    - conv activation = `RELU`
    - conv output scale = `1.0`
    - resadd `A/B/C_scale = 1.0`
    - resadd `relu = 0`

- 当前 scene 不要按“完全异步 overlap”去推理。
  在 `spm_xlate_enable=1` 时，runtime 会把 `sync_mode` 强制回 `blocking_debug`，进而把：
  - DMA 收敛到 `PRT_DMA_BACKEND_BLOCKING_FENCE`
  - Gemmini 收敛到 `PRT_GEMMINI_MODE_BLOCKING_FENCE`

- 当前 `num_cores >= num_gemmini_mgrs` 不是需求约束，而是实现残留。
  更准确地说：
  `num_cores`
  现在被 runtime 同时拿去做 CPU/hart 上界、accelerator slot/page-domain 上界、以及 page allocator 规模参数。
  所以代码才会强制把它拉到不少于 `num_gemmini_mgrs`。
  这条绑定不要再被解释成
  “CPU 数必须跟 Gemmini 数绑定”。
  如果未来要恢复大规模架构目标，正确方向是拆出独立的
  `num_cpu_harts`
  、
  `num_acc_slots/page_domains`
  、
  `num_gemmini_mgrs/num_dma_mgrs`
  语义。

- 当前 Gemmini 基本数据类型要按实际硬件看，而不是靠记忆：
  - `inputType = SInt(8.W)`
  - `accType = SInt(32.W)`
  - `spatialArrayOutputType = SInt(20.W)`

- 当前卷积越界语义是 zero padding，不是 edge clamp。

- 输出路径本身会扰动现象。
  以前已经出现过：
  - 中间标准输出把场景拖死
  - 改成文件输出能继续通过
  所以“加更多打印再看”不是默认正确动作。当前默认动作是保留少量粗日志，再按 segment 打开文件细日志。

## 稳定 workload 清单

- 主线回归：
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
  用途：验证主线是否仍能完成执行，并观察 mismatch 是否仍停在 `tensor=48`。

- 小 Linux smoke：
  `2026-03-20--06-16-29-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles`
  当前可采信 marker：
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`
  - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS`
  - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1`
  - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
  - `ALL_TESTS_PASS`
  - `COMMAND_EXIT_CODE="0"`

## 旧 blocker 只作为 regression 签名保留

- `2026-04-01` 的旧 hang 证据现在只作为“如果 future run 回退，再拿来对照”的 regression 签名。
- 最关键的旧 capture 是：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/bertmini-b8-fileonly-sync6-run-manualmon3-20260401-192.168.1.44-host-watchdog-20260401T152744Z.guest-deep-log.txt`
- 它冻结的旧边界是：
  `segment=3 stage=0 tensor=6 page=124 ... submit-begin -> [prt-marker] dma`
- 只有当未来 fresh run 再次回到 hang，而不是 mismatch，才需要重新展开这条旧线。

## 恢复 mismatch 调查的顺序

1. 先修掉 host `pipeline_runtime` 当前全量构建里的现存 `-Werror` 阻塞。
2. 重新跑 host closure，刷新 `golden.*.bin`。
3. 重新 `marshal build` 和 `marshal install`。
4. 在 `infrasetup` / `runworkload` 之前，再做一次 image/rootfs freshness 校验。
5. 用当前冻结 workload 再跑 fresh FireSim 回归。
6. 每轮 run 后立刻回收 run farm。
