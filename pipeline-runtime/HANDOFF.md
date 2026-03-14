# Pipeline Runtime 当前快照

详细历史、旧 metasim stall 时间线和旧 run/log 路径见 `docs/archive/2026Q1_history.md`。

## 当前 baseline

- 主目标仍是 `bertmini on globalnoc Linux`，目标硬件固定为 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`。
- 本地已经完成三类准备工作：host bertmini dummy-data 闭环、globalnoc baremetal metasim smoke、coupled-DMA globalnoc bitstream 构建。
- shared scratchpad page-table 模式已经落地到目标 coupled-DMA globalnoc 配置，且 shared-spad translation context 已扩到 `GemminiCoupledDMA`；runtime 采用“启动时 bootstrap `SPM_XLATE_CFG / RANGE`，segment 内只 `FLUSH`”模型。
- baremetal matrix / coverage / nonblocking 这三项 smoke 当前都稳定 PASS，说明这轮 shared-spad xlate 与 CoupledDMA 共享路径没有打坏 baremetal 主验证面。
- 后续 FPGA 执行环境转到 AWS manager。当前本地会话看不到 AWS，因此接下来的 `infrasetup` / `runworkload` 和 Linux FPGA replay 需要在 AWS 侧 AI 会话继续。

## 已确认的本地结果

### 1. Host bertmini 三方法闭环

命令：

```bash
bash /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

结果：

- `ours2=RC0`
- `gemini2=RC0`
- `tangram2=RC0`
- 脚本最终打印 `BERTMINI_HOST_CLOSURE_PASS`

### 2. globalnoc baremetal metasim suite

suite root：

- `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260312-215040`

三项 runworkload log：

- matrix: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--14-21-47-runworkload-DAEH4MMZT1C4SVVG.log`
- coverage: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--15-02-57-runworkload-7D5OF4V6I0PVAU78.log`
- nonblocking: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--15-34-11-runworkload-F7R3E8P02TZC15TX.log`

三项结果：

- matrix uartlog: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-workload/2026-03-12--14-21-47-rerocc-lc-baremetal-coupleddma-quick/rerocc-lc-baremetal-coupleddma-quick0/uartlog`
  - `GEMMINI_MATRIX_RESULT mode=full pass=4 fail=0 expected=4`
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`
  - `ALL_TESTS_PASS`
- coverage uartlog: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-workload/2026-03-12--15-02-57-rerocc-lc-baremetal-coupleddma-coverage-quick/rerocc-lc-baremetal-coupleddma-coverage-quick0/uartlog`
  - `CASE_RESULT shared_mv_xlate PASS`
  - `CASE_RESULT shared_mv_passthrough PASS`
  - `COVERAGE_SUMMARY case0=1 case1=1 case2=1 case3=1 case4=1 case4b=1 case4c=1 case5=1 case6=1 case7=1 case8=1 case9=1 case10=1`
  - `ALL_TESTS_PASS`
- nonblocking uartlog: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-workload/2026-03-12--15-34-11-rerocc-lc-baremetal-coupleddma-nonblocking-quick/rerocc-lc-baremetal-coupleddma-nonblocking-quick0/uartlog`
  - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
  - `ALL_TESTS_PASS`

### 3. 本地 bitstream build 成功

命令：

```bash
source ~/.ssh/AGENT_VARS
FIRESIM_DIR=/home/wzy/proj/wp2/chipyard/sims/firesim
cd "${FIRESIM_DIR}"
source ./sourceme-manager.sh --skip-ssh-setup
cd deploy
firesim buildbitstream -b config_build_rerocc_small_globalnoc_coupleddma.yaml -r config_build_recipes_rerocc_coupleddma.yaml
```

结果日志：

- FireSim manager log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--16-55-53-buildbitstream-2NLSKFJM4VHB1EFW.log`
- 关键成功信号：
  - `INFO: [Vivado_Tcl 4-198] DRC finished with 0 Errors`
  - `INFO: [Route 35-16] Router Completed Successfully`
  - `INFO: [Vivado 12-1842] Bitgen Completed Successfully.`
  - `write_cfgmem completed successfully`
  - `FireSim FPGA Build Completed`

产物入口：

- hwdb entry: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/built-hwdb-entries/alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10`
- bitstream tar: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-build/2026-03-12--16-55-53-alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/firesim.tar.gz`
- raw bitstream: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-build/2026-03-12--16-55-53-alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/vivado_proj/firesim.bit`
- cfgmem image: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-build/2026-03-12--16-55-53-alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/vivado_proj/firesim.mcs`
- timing report: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_BUILD_DIR/platforms/xilinx_alveo_u280/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/vivado_proj/reports/final_timing_summary.rpt`
- utilization report: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_BUILD_DIR/platforms/xilinx_alveo_u280/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/vivado_proj/reports/final_utilization.rpt`

构建时记录的源码状态：

- build log 写入的是 `firesim-commit:e0237bab695ef4305fc307b0cdd20a71adfa8055-dirty`
- 这说明当前工作树不是干净提交；AWS 接手时要特别注意同步本地未提交改动，不能只看最近一次 commit

## AWS 接手说明

### 1. 先同步代码状态

- 下一位 AI 需要先确认 AWS 上的 checkout 是否已经带上本地 dirty worktree 的改动。
- 如果 AWS checkout 没有这些改动，优先同步当前代码；否则 AWS 上重跑出来的 RTL/软件产物会和这里的验证结论不一致。

### 2. 决定 bitstream 复用方式

有两条路：

- 复用本地已生成 bitstream：
  - 复制 `firesim.tar.gz` 到 AWS 可见路径
  - 复制 `built-hwdb-entries/alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10`
  - 把其中 `bitstream_tar: file:///...` 改成 AWS 上真实可访问的 `file:///...` 路径
- 不复用本地 bitstream：
  - 在 AWS manager 上重新执行同一条 `firesim buildbitstream -b config_build_rerocc_small_globalnoc_coupleddma.yaml -r config_build_recipes_rerocc_coupleddma.yaml`

如果 AWS checkout 看不到本地 `results-build/.../firesim.tar.gz`，就不能直接使用当前 `built-hwdb` 入口，必须“复制 tar 并改路径”或者“在 AWS 重建”。

### 3. AWS 上优先做的事情

先做环境 bootstrap：

```bash
source ~/.ssh/AGENT_VARS
FIRESIM_DIR=/home/wzy/proj/wp2/chipyard/sims/firesim
cd "${FIRESIM_DIR}"
source ./sourceme-manager.sh --skip-ssh-setup
```

然后按这个顺序继续：

1. 确认 `config_hwdb.yaml` 已经包含 `alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10`
2. 确认 Linux workload 需要的 overlay / binary / artifacts 在 AWS 环境可构建或已存在
3. 运行：

```bash
cd /home/wzy/proj/wp2/chipyard/sims/firesim/deploy
firesim infrasetup \
  -c config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes.yaml
firesim runworkload \
  -c config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes.yaml
```

4. 如果先想建立更小的 AWS baseline，可以先跑最基础 Linux smoke，再切回 bertmini pipeline runtime workload

### 4. AWS 上最该盯的文件

- runtime config: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml`
- build recipe: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/config_build_recipes_rerocc_coupleddma.yaml`
- built hwdb entry: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/built-hwdb-entries/alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10`
- Linux packaging scripts:
  - `/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload/host-init.sh`
  - `/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`

## 当前风险 / 次要问题

- 本地 `VERILATOR_THREADS=16` 的 Linux smoke 仍有 task-graph SCC 问题。这是 metasim / Verilator 多线程问题，不是当前 AWS FPGA replay 的主 blocker，但如果 AWS 侧继续依赖本地 metasim 复核，需要记住这条坑。
- 本机交叉工具链仍缺 `riscv64-linux-gnu-gcc` / `riscv64-unknown-linux-gnu-gcc`。AWS 环境若具备交叉工具链，优先在那里完成 Linux target binary 和 overlay 打包。
- 如果 AWS 侧 checkout 没有同步当前 dirty tree，最容易出现“host/metasim 结论和 FPGA 结果不一致”的假回退。

## 接下来 3 个动作

1. 在 AWS 上确认代码状态与本地 dirty tree 一致，并决定“复制 bitstream”还是“在 AWS 重建 bitstream”。
2. 在 AWS 上让 `config_hwdb.yaml` 指向可访问的 `firesim.tar.gz`，然后跑 `config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml` 的 `infrasetup` / `runworkload`。
3. 等 AWS Linux baseline 站稳后，把 bertmini pipeline-runtime workload 接上，推进到最终 CPU golden 对齐。
