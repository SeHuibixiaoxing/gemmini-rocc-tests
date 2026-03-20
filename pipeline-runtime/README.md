# Pipeline Runtime

`pipeline-runtime` 是 Gemmini/ReRoCC/CoupledDMA pipeline 软件栈的执行侧实现。当前唯一主目标是：

- 使用 `conference/HybridMapper` 导出的 `bertmini` runtime artifacts
- 在 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA` 对应的 Linux/FireSim F2 目标上执行
- 以 CPU golden 为基准完成最终 correctness 闭环

## 当前代码入口

- Host runtime:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/`
- Artifact exporter:
  `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`
- Canonical runtime artifacts:
  `conference/HybridMapper/output/pipeline_runtime/bertmini/`
- Linux workload staging:
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh`
- FireMarshal workload:
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json`
- FireSim runtime config:
  `sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml`

## 权威文档

- `README.md`
  入口、文档地图、最短检查命令
- `ARCHITECTURE.md`
  当前软件结构、artifact 契约、软件到硬件的耦合点
- `DECISIONS.md`
  已冻结规则、调试约束、FireSim 执行规范
- `ROADMAP.md`
  分阶段目标与当前所处阶段
- `TESTPLAN.md`
  当前验证门和 FireSim F2 证据要求
- `docs/linux_dma_guardrails.md`
  Linux userspace DMA 踩坑总结、guardrails、当前 pipeline-runtime 审计结论
- `NEXT_SESSION_PROMPT.md`
  当前 live FPGA blocker、最近一次收敛到的停点、下一轮接手提示
- `docs/archive/2026Q1_history.md`
  历史时间线、旧 run/log、旧问题的取证材料

跨目录的 live 协同状态以这两份为准：

- `conference/mudnac_hybridmapper_collab_docs/STATUS.md`
- `conference/mudnac_hybridmapper_collab_docs/PROCESS.md`

## 当前必须遵守的环境规则

- FireMarshal 前先执行：
  `source /home/ubuntu/chipyard/env.sh`
- FireSim manager 前先执行：
  `cd /home/ubuntu/chipyard/sims/firesim`
  然后 `source sourceme-manager.sh`
- 不要再加 `--skip-ssh-setup`
- FireSim manager 长任务统一通过：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- F2 只使用 `f2.6xlarge`
- 如果 run 已明确卡死，先 `terminaterunfarm`，再分析
- 遇到 Gemmini/DMA 接口调用问题时，优先参考 Linux 下三个 coupleddma 回归测试

## 最短静态/本地检查

1. 重新导出 `bertmini` runtime artifacts：

```bash
cd /home/ubuntu/chipyard
python3 conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py --model bertmini
```

2. 构建 host 版 runtime：

```bash
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all
```

3. 运行 host closure：

```bash
METHODS=ours2 BATCH=1 SKIP_EXPORT=1 SKIP_BUILD=1 \
bash /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

4. 做 Linux overlay 静态检查：

```bash
cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload
HOST_INIT_CHECK_ONLY=1 bash host-init.sh
```

## 文档维护规则

- 当前状态、当前停点、下一步实验，写入 `STATUS.md` 和 `NEXT_SESSION_PROMPT.md`
- 架构或接口规则的变化，写入 `ARCHITECTURE.md` 或 `DECISIONS.md`
- 验证命令、通过标准、结果采信规则，写入 `TESTPLAN.md`
- 带日期的流水账、旧日志路径、失效路径，只进 `docs/archive/2026Q1_history.md`
