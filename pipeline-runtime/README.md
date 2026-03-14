# Pipeline Runtime

`pipeline-runtime` 是 Gemmini pipeline 软件栈，当前唯一主目标是：
在 `globalnoc + ReRoCC + CoupledDMA` 的最新硬件上，用 HybridMapper 生成的 pipeline mapping 执行 `bertmini`，并让结果与 CPU golden 一致。

最终目标硬件配置固定为 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`，最终运行环境固定为 Linux，最终 FireSim runtime 配置固定为 `sims/firesim/deploy/config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml`。

MudnacSim 只是参考模拟器，不是 runtime backend。
`pipeline-runtime` 本身只有一套执行实现，只区分 host Linux binary 和 RISC-V Linux target binary 两种构建形态。

## 当前能做什么

- 已能从 `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper` 为 `bertmini` 生成 Gemmini runtime 需要的 artifacts：
  - `layers_gemmini.yaml`
  - `mapping_gemmini/*.yaml`
  - `entire_model/2_1024_16_19_64_{ours2,gemini2,tangram2}.yaml`
  - `model.bin` / `input.bin` / `golden.bin`
- host 版 `pipeline_runtime` 已能在 `ours2 / gemini2 / tangram2` 三种方法上完成 bertmini dummy-data 闭环并返回 `RC=0`。
- Linux overlay / run script 已接入 `rerocc-linux-tests`，但当前环境缺少 `riscv64-linux-gnu-gcc` / `riscv64-unknown-linux-gnu-gcc`，因此 RISC-V Linux target binary 还不能在本机完成交叉编译验证。
- coupled-DMA globalnoc 的 U280 bitstream 已在本地成功构建：
  - FireSim log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--16-55-53-buildbitstream-2NLSKFJM4VHB1EFW.log`
  - hwdb entry: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/built-hwdb-entries/alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10`
  - tarball: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-build/2026-03-12--16-55-53-alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/firesim.tar.gz`
- 后续 FPGA 执行环境转到 AWS manager；当前本地会话继续保留 host / metasim / bitstream prep，不直接承担 AWS 上的 infrasetup / runworkload 执行。
- FireSim metasim / quick-diag 目前只保留为 globalnoc 启动链 smoke，不再是终极目标。

## 项目入口

- HybridMapper 根目录：`/home/wzy/proj/wp2/chipyard/tmp/HybridMapper`
- MudnacSim 根目录：`/home/wzy/proj/wp2/chipyard/tmp/MudnacSim`
- 协同机制参考：
  - `/home/wzy/proj/wp2/chipyard/tmp/mudnac_hybridmapper_collab_docs/v2/协同机制文档_v2.md`
  - `/home/wzy/proj/wp2/chipyard/tmp/mudnac_hybridmapper_collab_docs/v3/协同机制文档_v3.md`

## 文档地图

- `README.md`
  - 项目是什么、当前入口、最短上手命令。
- `ARCHITECTURE.md`
  - `HybridMapper -> bertmini artifacts -> pipeline-runtime-linux -> globalnoc Linux workload` 的当前架构。
- `ROADMAP.md`
  - 以 bertmini globalnoc Linux 闭环为主线的阶段计划。
- `TESTPLAN.md`
  - Artifact、host、Linux 打包、metasim smoke、FPGA replay 的唯一验证文档。
- `DECISIONS.md`
  - 已锁定规则，包含 globalnoc-only、Linux-only、shared YAML 契约和尺寸不匹配策略。
- `HANDOFF.md`
  - 当前 baseline、当前 blocker、最近一次已验证命令、接下来 3 个动作。
- `docs/archive/2026Q1_history.md`
  - 历史迭代、旧 metasim stall 调试时间线、旧 run/log 路径。

## 最短上手

1. 重新生成 bertmini Gemmini artifacts：

```bash
cd /home/wzy/proj/wp2/chipyard/tmp/HybridMapper
python3 scripts/create-gemmini-pipeline-runtime-artifacts.py --model bertmini
```

2. 构建 host 版 runtime：

```bash
make -C /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all
```

3. 跑 bertmini host 闭环：

```bash
bash /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

4. 语法检查 Linux packaging 脚本：

```bash
cd /home/wzy/proj/wp2/chipyard
bash -n generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload/host-init.sh
sh -n generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
```

5. 工具链不可用时做 overlay 静态验收：

```bash
cd /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload
HOST_INIT_CHECK_ONLY=1 bash host-init.sh
```

## 当前验证入口

- 主语义门：`TESTPLAN.md` 第 3 节的 bertmini host 闭环。
- 当前 Linux 目标打包门：`TESTPLAN.md` 第 4 节的 overlay 路径与交叉编译检查。
- 当前硬件近似 smoke：`TESTPLAN.md` 第 5 节的 globalnoc metasim suite。
- 当前 FPGA 接手入口：`HANDOFF.md` 的 AWS 接手说明，以及 `TESTPLAN.md` 第 6 节的 AWS FPGA replay。

## 维护规则

- 新设计进入 `DECISIONS.md`。
- 新计划进入 `ROADMAP.md`。
- 新验证命令进入 `TESTPLAN.md`。
- 当前状态进入 `HANDOFF.md`。
- 迭代故事、旧 run/log 和 dated updates 进入 archive。
