# Pipeline Runtime Roadmap

当前主线阶段：`bertmini end-to-end closure on Linux/FireSim F2`

## Phase 1. Export Interface 收敛

目标：

- 固定 `conference/HybridMapper` 的 runtime exporter 输出
- 固定当前 runtime interface 文件名和目录布局
- 不回退到旧接口或手工拼装 artifacts

状态：已完成基础收敛。

当前产物：

- exporter 已固定
- `conference/HybridMapper/output/pipeline_runtime/bertmini/` 已成为 canonical artifact 根目录
- `model.layers.yaml` / `gemmini_layer_mapping.*` / `pipeline_mapping.*` / `runtime_model.bin` / `runtime_input.*` / `golden.*` 已形成稳定命名

## Phase 2. Host Closure

目标：

- host `pipeline_runtime` 能在 `ours2 / gemini2 / tangram2` 上完成 CPU golden 与 FPGA backend 闭环

状态：已完成当前基线。

说明：

- host closure 仍然是改动后的第一道 correctness gate
- 当前是否继续扩大 synthetic coverage，不影响主线阶段判断

## Phase 3. Linux Packaging 与回归对照

目标：

- `rerocc_pipeline_runtime-linux` 构建稳定
- `host-init.sh` 能稳定把 runtime artifacts staged 到 FireMarshal overlay
- 调用序列持续贴近 Linux coupleddma 正例

状态：进行中。

当前重点：

- 维持 `rerocc-linux-tests-coupleddma/workload/host-init.sh`
- 维持 guest wrapper `run_rerocc_pipeline_runtime_bertmini.sh`
- 以三个 Linux coupleddma 回归为接口写法基准

## Phase 4. FireMarshal / FireSim F2 Bring-up

目标：

- dedicated `bertmini` workload image 稳定 build/install
- run farm 能稳定启动并进入 guest workload
- 正确收集 `uartlog`、`heartbeat.csv` 和 tmux pane log

状态：进行中。

注意：

- 真实环境流程固定为 `env.sh -> sourceme-manager.sh`
- manager 长任务固定走 `scripts/firesim-tmux-run.sh`
- 当前 live blocker 不在本文件维护，统一看 `NEXT_SESSION_PROMPT.md` 和 `STATUS.md`

## Phase 5. Bertmini FPGA Correctness Closure

目标：

- 在 F2 目标上让 `ours2 / gemini2 / tangram2` 全部通过
- guest 内打印最终 PASS 标记
- 输出与 CPU golden 完全一致

状态：未完成。

退出条件：

- `BERTMINI_PIPELINE_RUNTIME_PASS`
- 三种 method 都通过
- 结果不是基于 manager exit code 推断，而是有 `uartlog` 证据

## Phase 6. Post-Closure Work

目标：

- profiling、schedule point、QoS 或更大 workload

状态：未开始。

前置条件：

- 只有 Phase 5 完成后才进入这一阶段
