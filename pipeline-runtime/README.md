# Pipeline Runtime

`pipeline-runtime` 负责把 HybridMapper 导出的 pre-orchestrated mapping 落到 Gemmini / ReRoCC / CoupledDMA 执行路径上。

## 当前一句话状态

- 当前 `bertmini` 主线已经从“Linux/F2 上疑似 DMA 卡死”收敛到“Linux/F2 上可完整执行完成，但 final golden mismatch”。
- 最新冻结结论是：`2026-04-02` 的 `bertmini batch=8 file-only` 主线 run 已跑到 `segment=31` 结束，并正常触发 guest poweroff；当前 failure 是 `tensor=48` 的 golden mismatch，不再是 mainline DMA hang。
- 当前 golden mismatch 排查暂时搁置；除非新的 fresh run 明确从“可完成”回退成“再次卡死”，否则不要把主线叙事改回 DMA submit hang。

## 当前推荐入口

- `docs/CURRENT_STATUS.md`
  当前冻结状态、关键证据、稳定 workload
- `docs/blockers_and_lessons.md`
  当前仍有效的硬约束、教训和恢复顺序
- `DECISIONS.md`
  已冻结的 runtime / artifact / ReRoCC 边界
- `TESTPLAN.md`
  验证阶梯
- `ROADMAP.md`
  后续实现顺序
- `docs/pair_wrapper_manager_plan_20260405.md`
  `pair-wrapper manager` 的当前 authoritative 计划、测试计划和交接 prompt
- `NEXT_SESSION_PROMPT.md`
  下次接手 `pipeline-runtime Linux/F2 bertmini` 主线时的最小 prompt
- `docs/archive/2026Q1_history.md`
  历史时间线归档

如果当前接手目标是 `pair-wrapper manager` 硬件路线，而不是 Linux/F2 `bertmini` 主线，请优先读：

- `PAPER_HARDWARE_ARCHITECTURE.md`
- `docs/pair_wrapper_manager_plan_20260405.md`
- `/home/ubuntu/chipyard/tmp/firesim-aws-f2/HANDOFF_dummy_gemmini_buildbitstream_20260403.md`

## 当前稳定工作负载

- `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
  当前主线回归 workload。用途是验证 Linux/F2 启动、文件日志链路、全 segment 执行完成，以及是否仍停留在 `tensor=48` mismatch。
- `rerocc-lc-linux-coupleddma-regression-small-pipelinefiles`
  当前可采信的小 Linux smoke。`2026-03-20` 的结果里已经出现 `DMA_MATRIX_RESULT ... PASS`、`SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1`、`NONBLOCKING_SUMMARY ...`、`ALL_TESTS_PASS`。

## 当前执行纪律

- 只用 `f2.6xlarge`。
- FireSim 只走 manager 正规流：`launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`。
- FireSim manager 命令只通过 `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`。
- FireMarshal 只通过 `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`。
- 每次 `infrasetup` 或 `runworkload` 前，必须先核对 workload image/rootfs 是最新 build/install 产物；至少核对路径、mtime、size，最好补 `sha256sum`。
- 任何 run 结束、失败或人工中断后，先收证据，再立刻 `terminaterunfarm --forceterminate`，并继续核对 EC2 状态直到实例不再 `running`。
- Linux 启动期间允许长时间安静窗口；只要没有 panic/crash，且 heartbeat 或启动日志还在前进，就不要过早判定 boot hang。
- guest/bin 日志默认走文件，不走 UART。UART 只保留 Linux 启动和 FireSim verdict。

## 当前日志策略

- 粗粒度日志：`/root/pipeline-runtime-debug/bertmini-batch8.log`
- 细粒度日志：`/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
- 当前主线已支持按 segment/stage/subbatch 开启细粒度日志，入口是 `--deep-log-segment`、`--deep-log-global-stage`、`--deep-log-local-stage`、`--deep-log-subbatch` 及对应 radius 参数。
- 当前稳定策略是不把 bin/runtime 日志经 UART 打印；如果要追卡点，优先开文件细日志，而不是往 UART/stdout 加更多输出。
