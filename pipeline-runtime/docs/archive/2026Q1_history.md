# 2026Q1 历史归档

本文件收纳 2026Q1 期间的开发时间线、旧 run/log 路径、dated updates 和旧 milestone 列表。
这里保留的是原始历史记录，不保证路径、命令或环境要求仍然有效。

当前 live 状态请回到：

- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/README.md`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/CURRENT_STATUS.md`
- `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/NEXT_SESSION_PROMPT.md`

说明：

- 本文中保留的 `/home/wzy/...`、`--skip-ssh-setup` 等内容仅用于历史取证
- 这些旧路径和旧命令不应再被当成当前执行规范

## 1. 归档范围

本归档主要吸收自以下旧主文档：

- 旧的 implementation/handoff 流水账
- 早期版本 `ARCHITECTURE.md` 中带时间语义的专项推进记录
- 已删除的兼容壳文档中的历史 run/log 路径

## 2. 2026-02 基线 bring-up

这一阶段完成了 runtime 的基础闭环：

- 建立 `pipeline_runtime` CLI 与核心 runtime 框架。
- 实现 YAML 驱动 topology build，移除 mock topology 依赖。
- 支持 strict `--input/--golden` compare 和 mismatch 非零返回。
- 用 batch-complete 替代 watchdog-only 退出语义。
- 支持 multi-segment 执行。
- 建立 C1-C8 buffer taxonomy、blocking wait 语义和基础 page allocation。
- 建立 `--trace` 的基础 runtime 观测能力。

当时的 Host baseline 以 BertMini dummy data 为主，核心命令后来被整理进 `TESTPLAN.md`。

## 3. 2026-03-05 ReRoCC + CoupledDMA 集成里程碑

这一阶段的主要新增：

- `ScheduleAction` 作为 per-segment 资源生命周期抽象进入 runtime。
- Gemmini / DMA 路径接入 ReRoCC manager scope。
- Runtime 增加 manager topology 参数：
  - `--num-gemmini-mgrs`
  - `--num-dma-mgrs`
  - `--gemmini-base-id`
  - `--dma-base-id`
  - `--sync-mode`
- Async 默认组合切换为：
  - DMA `poll_progress_thread`
  - Gemmini `async_experimental`
- Runtime 增加 shared scratchpad page-table API 与 observability：
  - map / unmap / translate
  - PTBR / PTE / fault 观测
- 建立 paged shared-spad DMA helper。
- `run_rerocc_coupleddma_baremetal_metasim_suite.sh` 作为 baremetal metasim suite 入口落地。

这一阶段的专项计划后来被精简为 `ROADMAP.md`，不再单独维护旧计划文档。

## 4. 2026-03-06 至 2026-03-07 metasim stall 调试时间线

### 4.1 首次稳定复现

globalnoc quick-diag：

- run dir: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-globalnoc-quickdiag-20260306-202700`
- infrasetup log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-06--12-27-00-infrasetup-VO6DG4278T6HXQMK.log`
- runworkload log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-06--12-28-06-runworkload-58V1V021WHG04H9W.log`
- gdb stack: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-globalnoc-quickdiag-20260306-202700/sim_slot_0/gdb_run_bt_20260306-210207_esc.txt`

non-globalnoc quick-diag：

- run dir: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-nonglobalnoc-quickdiag-20260306-204126`
- infrasetup log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-06--12-41-27-infrasetup-1EF6CWLKM58GMN78.log`
- runworkload log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-06--13-03-56-runworkload-W5JXJE0IL5DZQ0CE.log`
- gdb stack: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-nonglobalnoc-quickdiag-20260306-204126/sim_slot_0/gdb_run_bt_20260307-094901_nonglobal_fixcheck.txt`

共同症状：

- `uartlog` 停在启动 banner / plusargs 附近。
- `heartbeat.csv` / `memory_stats*.csv` 只有 header 或无前进。
- gdb 栈都停在 `simif_emul_t::do_tick` 的 condition variable wait。

### 4.2 根因定位

核心根因不是 globalnoc 拓扑本身，而是 Verilator emulation binary 的入口被错误链接：

- 旧坏版本 `VFireSim` 的 `main` 反汇编表现为：
  - `main -> entry(int, char**)` 直跳
- 这绕过了 Verilator 生成的 scheduler loop，导致 DPI tick / notify 链断开，最终 `simif_emul_t::do_tick` 永久等待。

对应修复：

- FireSim emulation link 输入必须排除 `main.cc`。
- `plusarg_reader.v` 只能在 `FireSim-generated.sv` 未定义该 module 时按需包含。

### 4.3 修复后验证

globalnoc quick-diag 修复后：

- run dir: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-globalnoc-quickdiag-fix-20260307-091051`
- infrasetup log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--01-11-21-infrasetup-5YBQCQRXQYNLTOVG.log`
- runworkload log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--01-17-23-runworkload-2VOCXDMVXCWHYPR2.log`
- result uart: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-workload/2026-03-07--01-17-23-rerocc-lc-baremetal-quick-diag/rerocc-lc-baremetal-quick-diag0/uartlog`

non-globalnoc quick-diag 修复后：

- run dir: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-nonglobalnoc-quickdiag-fix-20260307-091051`
- infrasetup log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--01-29-27-infrasetup-USQIIEQD155XWTWH.log`
- runworkload log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--01-34-30-runworkload-U0GLVBJW1O1FBLTM.log`
- result uart: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-workload/2026-03-07--01-34-30-rerocc-lc-baremetal-quick-diag/rerocc-lc-baremetal-quick-diag0/uartlog`

修复后的两个 quick-diag 都到达：

- `GEMMINI_MATRIX_RESULT`
- `DMA_MATRIX_RESULT`
- `ALL_TESTS_PASS`
- `COMMAND_EXIT_CODE="0"`

## 5. 旧 milestone 摘要

以下内容曾以长列表形式分散在旧文档中；现统一摘要如下：

- runtime framework、blocking synchronization foundation、DMA/Gemmini backend abstraction 已完成。
- shared scratchpad page allocator、translation API、lifecycle checks 已完成 baseline。
- model parser、pipeline parser、strict compare、batch completion、multi-segment 已完成 baseline。
- ReRoCC + CoupledDMA integration、manager-aware routing、async overlap baseline 已完成第一轮功能接入。
- metasim startup stall 已完成复现、根因定位、修复和双配置 revalidation。
- FPGA correctness replay、hardware overlap calibration、Linux FPGA gate 仍等待硬件恢复。

## 6. 迁移后的 canonical home

历史归档迁移完成后，后续请使用：

- 当前入口：`README.md`
- 当前状态：`docs/CURRENT_STATUS.md`
- 当前架构：`ARCHITECTURE.md`
- 当前计划：`ROADMAP.md`
- 当前验证：`TESTPLAN.md`
- 锁定决策：`DECISIONS.md`
- 当前交接：`NEXT_SESSION_PROMPT.md`

不要再把新的 dated update 追加回主文档正文。

## 7. 2026-03-07 bertmini / globalnoc / Linux 主线重定向

这一轮重构把主目标从“Host + metasim correctness 框架建设”进一步收敛为：

- 模型固定为 `bertmini`
- 方法固定为 `ours2 / gemini2 / tangram2`
- 主硬件固定为 `globalnoc + ReRoCC + CoupledDMA`
- 最终执行环境固定为 Linux

同时明确了几个长期规则：

- `pipeline-runtime` 是 Gemmini pipeline 软件栈，不做 backend 分叉。
- MudnacSim 只是参考模拟器，用于提供运行时协同机制和 shared pipeline YAML schema 参考。
- HybridMapper 根目录改为 `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper`。
- MudnacSim 根目录为 `/home/wzy/proj/wp2/chipyard/tmp/MudnacSim`。

这一轮还落下了 bertmini 的 Gemmini artifact 链：

- `layers_gemmini.yaml`
- `mapping_gemmini/*.yaml`
- `entire_model/2_1024_16_19_64_{ours2,gemini2,tangram2}.yaml`
- `model.bin` / `input.bin` / `golden.bin`

当时的 host 闭环结果：

- `pipeline_runtime` 在 `ours2 / gemini2 / tangram2` 上均返回 `RC=0`
- Linux overlay 入口和 run script 已写入 `rerocc-linux-tests`
- 但交叉工具链缺失，`workload/host-init.sh` 尚不能在本机完成 RISC-V Linux target binary 构建

这一阶段也暴露出一个现实问题：

- 当前 `2_1024_16_19_64_*.yaml` 还是为了 host correctness 闭环而构造的 canonical 基线
- 它们保留了 stage contract 和 tensor stay 语义，但还不是最终经过真实双 Gemmini 硬件约束收敛出的最优 mapping

后续如果 FPGA 恢复，需要在最新版硬件上 replay，并根据 replay 结果继续收紧 HybridMapper 的 Gemmini artifact 生成逻辑。

## 8. 2026-03-07 globalnoc baremetal metasim regression

在 bertmini host 主线保持通过的同时，globalnoc baremetal metasim smoke 暴露了一个新的 regression。

触发命令：

- `source ~/.ssh/AGENT_VARS`
- `cd /home/wzy/proj/wp2/chipyard/sims/firesim`
- `source ./sourceme-manager.sh --skip-ssh-setup`
- `cd deploy`
- `SUITE_RUNS=1 RUN_TIMEOUT_SECS=1200 ./run_rerocc_coupleddma_baremetal_metasim_suite.sh`

首个失败样本：

- run dir: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260307-173040/config_runtime_rerocc_metasim_small_baremetal_globalnoc_coupleddma`
- runworkload log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--09-42-07-runworkload-R1Z1H1ZGY892M78O.log`
- kill log: `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--09-48-41-kill-NUW92D3HVGSCQS57.log`
- uartlog: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260307-173040/config_runtime_rerocc_metasim_small_baremetal_globalnoc_coupleddma/sim_slot_0/uartlog`

关键症状：

- workload 不是 bertmini pipeline runtime，而是 baremetal matrix quick case：`rerocc_lc_matrix_baremetal_coupleddma.riscv`
- 失败签名为：`conv=1 resadd=0 shared_mv=1`
- 随后打印：`[gemmini] cpu=0 mgr=0 FAIL cycles=456467`
- 最终 `VFireSim` 被 kill，`COMMAND_EXIT_CODE="137"`

后续单独做 isolated matrix repro 时，运行链也能稳定重放，且新的 `uartlog` 一度推进到：

- `after tiled_conv_auto`

但尚未进入：

- 第二次 `gemmini_flush(0)` 之后的打印
- `tiled_resadd_auto(...)` 后的打印
- `gemmini_fence()` 后的打印

因此当时的定位结论是：

- regression 不在 bertmini host runtime 主线
- regression 也不在 baremetal case 的 conv 或 shared-spad mv 基础功能
- 当前最可疑区间是 `rerocc_lc_matrix_baremetal_coupleddma.c` 中的第二次 `gemmini_flush -> tiled_resadd_auto -> gemmini_fence` 这一段

## 9. 2026-03-07 nodasm single-pair follow-up

在继续怀疑 “runworkload 卡在 DMA” 之前，又补做了一轮更小的 globalnoc metasim 隔离验证：

- 关闭 Rocket unconditional DASM 打印后的新 driver
- `mode=single`
- `logical_cores=1`
- `dma_bytes=1024`
- 独立 run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-full-smalldma-nodasm-20260307-213626`
- runtime config：`/tmp/config_runtime_rerocc_metasim_small_baremetal_globalnoc_coupleddma_full_smalldma_nodasm_20260307-213626.yaml`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--14-04-50-runworkload-64UR9J0WG6X4SUZ0.log`

关键观测：

- `metasim_stderr.out` 不再被每条 Rocket 指令的 `DASM(...)` 淹没，只剩正常 DRAM model 输出。
- `uartlog` 可以稳定推进到：
  - `after cpu_conv_reference_prep`
  - `after tiled_conv_auto`
  - `conv=1 resadd=1 shared_mv=1`
  - 三段 DMA 全部 `compare PASS`
  - `ALL_TESTS_PASS`
- 因而这轮已经明确说明：
  - “看起来像卡在 DMA” 不是可靠表述
  - 旧 driver 的 DASM 洪泛会严重拖慢 metasim，并掩盖 guest 真实所处阶段
  - 当前代码/硬件至少在 single-pair smoke 下，Gemmini 和 CoupledDMA 基础功能都能完整跑通

随后的下一步不再是继续盯 single case，而是回到更接近原始问题的 `full matrix + 2 logical cores`，在 nodasm driver 上重新评估原始 stall 是否仍然存在。


## 10. 2026-03-08 full matrix 根因闭环

在 `nodasm + full matrix + logical_cores=2 + dma_bytes=1024` 路径上，最终把 regression 收敛为两层根因，而不是单一的 “DMA 卡死”。

第一层根因是 shared scratchpad 地址别名：

- 原测试在每个 local shared scratchpad 内为 A/B 使用固定偏移。
- 在 full-matrix + multi-hart 模式下，不同 hart 会落到同一块 local shared scratchpad 上互相覆盖。
- 这来自当前实现里配对的 Gemmini manager 与 CoupledDMA manager 实际共享同一块 local shared scratchpad。

对应修复：

- 在 `rerocc_lc_matrix_baremetal_coupleddma.c` 中按 `cid` 对每个 local shared scratchpad 内的测试区域做分区。
- 同时新增 layout 越界检查，防止地址规划超出 `SHARED_SPAD_LOCAL_SIZE`。

中间态证据：

- 仅加入 shared-spad 分区修复后，run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-full-smalldma-nodasm-sharedspadfix-20260308-005348`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--17-00-51-runworkload-59JEMSIA4F7D5966.log`
- 观测：`shared_mv` 已从失败转为通过，但 `resadd` 仍失败，签名变成 `conv=1 resadd=0 shared_mv=1`。

第二层根因是 barrier 语义错误：

- 测试最初把 `gemmini_fence()` 当成 accelerator completion barrier 使用。
- 实际上 `gemmini_fence()` 只是 CPU memory fence，不能保证 ReRoCC manager 侧的 Gemmini 操作已完全 drain。
- 正确 barrier 是 `rr_fence(GEMMINI_CFG_ID)`。

中间态证据：

- 只额外插入 `gemmini_fence()` 并不能修复问题。
- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-full-smalldma-nodasm-sharedspadfix-convfence-20260308-012341`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--17-30-13-runworkload-MONWRV475QMFON37.log`
- 观测：签名仍然是 `conv=1 resadd=0 shared_mv=1`。

最终修复版：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-full-smalldma-nodasm-sharedspadfix-rrfence-20260308-051600`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--21-24-45-runworkload-Z6MP8L1YLGLPL9FQ.log`
- uart 结果：`GEMMINI_MATRIX_RESULT mode=full pass=4 fail=0 expected=4`、`DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`、`ALL_TESTS_PASS`。

## 11. 2026-03-08 suite 误杀与 coverage xlate 泄漏

### 11.1 suite stall 误判

在 matrix workload 修好后，第一次重跑 baremetal metasim suite 又暴露出一个新的非 RTL 问题：suite wrapper 会把 slow-progress 的 metasim 误判为 stalled。

失败样本：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260308-055012/config_runtime_rerocc_metasim_small_baremetal_globalnoc_coupleddma`
- 关键观测：
  - `uartlog` 长时间只停在 baremetal start line
  - `heartbeat.csv` / `memory_stats*.csv` 长时间不更新
  - 但 `metasim_stderr.out` 持续增长，`VFireSim` 进程 CPU 持续占用
- 旧脚本错误地只看 `uartlog/heartbeat/memory_stats*` 的 mtime，在 181s 后报：`workload appears stalled`

因此这次的结论是：

- 这不是 DMA deadlock，也不是 FireSim startup stall 回归。
- 对当前 long-running metasim case，`uartlog` 静默本身不足以判定 stall。
- suite wrapper 需要把 `metasim_stderr.out` 和 `VFireSim` 子进程 CPU 活动也作为 liveness 信号。

对应修复进入：

- `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/run_rerocc_coupleddma_baremetal_metasim_suite.sh`
- 同时把 suite 命令的 `RUN_TIMEOUT_SECS` 提升到 `2400`，因为当前 localhost 上 full-matrix 正常运行时间已经超过 1200s。

### 11.2 coverage 用例的 xlate 状态泄漏

suite 在继续推进到 coverage workload 后，又暴露出一条真正的功能失败：

- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--02-22-12-runworkload-5DOS4AM6MJXDHYGM.log`
- 失败签名：`spm_xlate_ctrl` PASS，三个 `conv_*` 全部 FAIL，而 `resadd` 与所有 DMA case PASS。

根因是 `spm_xlate_ctrl_case` 改写了 Gemmini frontend shared-spad translation 配置，但没有在退出前恢复默认态。

关键静态证据：

- `Controller.scala` 中 `SPM_XLATE_FLUSH` 只清 fault 状态，不会自动清掉 `spm_xlate_enable`、`range_base` 或 `range_size`。
- 因此 coverage case0 之后，后续 DRAM-based conv case 仍然带着临时的 xlate 配置运行，DRAM 地址被错误重定向到 shared scratchpad 范围。

对应修复：

- 在 `rerocc_lc_coverage_baremetal_coupleddma.c` 的 `run_spm_xlate_ctrl_case()` 退出前显式执行：
  - `rerocc_gemmini_spm_xlate_cfg(0, ..., enable=0)`
  - `rerocc_gemmini_spm_xlate_range(0, 0)`
  - `rerocc_gemmini_spm_xlate_flush()`
  - `rr_fence(GEMMINI_CFG_ID)`

隔离验证：

- runtime cfg：`/tmp/config_runtime_coverage_xlatefix_20260308-103109.yaml`
- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-coverage-xlatefix-20260308-103109`
- infrasetup log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--02-35-23-infrasetup-XQ1E71D61VGDB5IF.log`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--02-44-04-runworkload-Z55S9L7Y9JDU6PFX.log`
- uart 结果：三个 `conv_*` 全部回到 PASS，`resadd` 和全部 DMA case PASS，最终 `COVERAGE_SUMMARY case0=1 ... case10=1`、`ALL_TESTS_PASS`。

## 12. 2026-03-11 Linux 最小 repro：从误判 loadmem 改为收敛到 OpenSBI 初始化窗口

这一轮把 Linux/metasim 问题从 Gemmini/globalnoc 主链上拆下来，重新做了一个最小 repro：

- 硬件：`FireSimQuadRocketSbusRingNoCConfig`
- 工作负载：`br-base.json`
- 运行时配置基线：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/config_runtime_quad_rocket_ring_metasim_br_base_diag_heartbeat.yaml`
- build recipe：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/config_build_recipes_quad_rocket_ring_linux_smoke_threads1.yaml`

### 12.1 这轮首先修正的误判

最开始一度把问题过度归因到 `loadmem bypass`，但继续对照后发现这条判断不够稳，原因有三条：

- `heartbeat.csv` 一直在继续增长，不能再把早期某个固定 cycle 值当成永久死锁点。
- `+heartbeat-polling-interval=100` 会极大改变观测粒度，而且它统计的是 host 侧 bridge tick，不是 target cycles。
- FireSim 默认 `sim_slot_0` 会复用旧目录；如果不给每轮实验独立 run dir，`heartbeat.csv`/`uartlog` 的时间线会混在一起，导致错误比较。

因此，这轮明确把下列做法记为“不要再直接据此下结论”：

- 只看 `heartbeat.csv` 是否更新
- 只看 `uartlog` 普通 `tail`
- 复用公共 `sim_slot_0` 做横向对比
- 把 `TSI-only` 诊断分支直接等同于修复方案

### 12.2 当前最小 repro 的关键样本

普通 loadmem 路径：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-idbits1-thread1-20260311-004453/sim_slot_0`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--16-48-00-runworkload-7OIRD9PCRJYW1WI2.log`
- 观测：`heartbeat.csv` 持续增长，但 `uartlog` 只稳定看到 `OpenSBI v1.2` 和 banner，没有继续出现 `Platform Name` / `Boot HART ...`。

限制 loadmem chunk 大小（64B）路径：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-loadmem64-thread1-20260311-011044/sim_slot_0`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--17-14-16-runworkload-3M42HVN4SLJIQDY6.log`
- 观测：行为与普通 loadmem 基本一致，没有把停点前推到 Linux 之后。

TSI-only 诊断路径：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-tsionly-thread1-20260311-011941/sim_slot_0`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--17-23-03-runworkload-WFQWRBI2E00GE4X9.log`
- 观测：当前并没有形成“明显越过普通 loadmem 停点”的稳定证据；到目前仍不能把它当成正式 fix。

### 12.3 当前真正收敛到哪里

这轮静态核对了 OpenSBI 启动顺序：

- `sbi_boot_print_banner()` 先打印 `OpenSBI v1.2` 和 ASCII banner。
- 然后才进入：
  - `sbi_irqchip_init()`
  - `sbi_ipi_init()`
  - `sbi_tlb_init()`
  - `sbi_timer_init()`
  - `sbi_domain_finalize()`
  - `sbi_hart_pmp_configure()`
  - `sbi_platform_final_init()`
- `Platform Name`、`Platform IPI Device`、`Platform Timer Device`、`Boot HART ...` 这些打印都在上面这些调用之后。

因为当前 `uartlog` 只稳定停在 banner 之后，所以本轮更合理的根因范围变成：

- 不是“Linux 用户态跑慢”
- 也不是“Gemmini runtime 本身”
- 而是 OpenSBI cold boot 期间、从 banner 进入 platform bring-up 的初始化窗口

### 12.4 为什么当前先怀疑 PLIC

当前 FireChip generic OpenSBI 平台通过 FDT 走：

- `.irqchip_init = fdt_irqchip_init`
- `.ipi_init = fdt_ipi_init`
- `.timer_init = fdt_timer_init`

本轮重新生成 FDT 后，最关键的两个节点都在：

- `clint@2000000`
- `interrupt-controller@c000000`，compatible = `riscv,plic0`

再结合本地 RTL 现状：

- `CLINT` attach 已经被改成 `TLFragmenter := TLBuffer() := TLSourceShrinker(1) := _`
- `PLIC` attach 之前仍然是裸 `TLFragmenter := _`

这在多核 + NoC + Linux cold boot 场景下很可疑，因为 OpenSBI banner 之后第一跳就是 `irqchip_init`，也就是 PLIC。

### 12.5 已落地的最小诊断补丁

为了验证上面的怀疑，这轮在 `rocket-chip` 里做了一处诊断性最小改动：

- 文件：`/home/wzy/proj/wp2/chipyard/generators/rocket-chip/src/main/scala/devices/tilelink/Plic.scala`
- 改动：把 PLIC attach 改成与当前 CLINT attach 对称的形式：
  - `TLFragmenter(tlbus, Some("PLIC")) := TLBuffer() := TLSourceShrinker(1) := _`

对应验证配置：

- runtime cfg：`/tmp/config_runtime_quad_rocket_ring_metasim_br_base_plicshrinker_20260311-020756.yaml`
- run dir root：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-plicshrinker-20260311-020756`
- 当前 infrasetup log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--18-09-06-infrasetup-YFKF2GBE07JJT7YY.log`

这条 patch 目前仍处于诊断验证阶段，还没有升级为“已确认修复”。

### 12.6 这轮沉淀下来的经验

- Linux metasim 的“无 UART 新输出”不等于死锁，尤其在 OpenSBI 阶段。
- `heartbeat.csv` 的采样粒度由 bridge tick 决定，必须先理解 plusarg 语义，再拿它做卡点判断。
- `uartlog` 是二进制污染文件，不适合直接用纯文本工具做阶段判断。
- FireSim 默认公共 run dir 会污染实验结论；所有 Linux 最小 repro 都必须分配独立 run dir。
- 如果停在 OpenSBI banner 之后而不是更早，优先看 `irqchip / ipi / timer / domain / PMP / platform-final`，不要先跳去怀疑 Linux app、Gemmini runtime 或 loadmem。

### 12.7 OpenSBI staged workload 与 thread-16 busy-spin 观测

为了把 `OpenSBI banner 之后` 的粗卡点继续收紧，这一轮直接在 OpenSBI `init_coldboot()` 里加入了 staged UART 标记：

- 修改文件：`software/firemarshal/boards/firechip/firmware/opensbi/lib/sbi/sbi_init.c`
- 新增阶段：`banner_done -> before/after_irqchip_init -> before/after_ipi_init -> before/after_tlb_init -> before/after_timer_init -> before/after_ecall_init -> before/after_domain_finalize -> before/after_pmp_configure -> before/after_platform_final_init -> before_boot_prints`
- 重新生成 workload：
  - build log：`/home/wzy/proj/wp2/chipyard/software/firemarshal/logs/br-base-build-2026-03-10--18-31-28-QCJX7ZCYRGKO1R74.log`
  - install log：`/home/wzy/proj/wp2/chipyard/software/firemarshal/logs/br-base-install-2026-03-10--18-33-48-AJLI4IEXCO24ANCQ.log`
- 二进制确认：fresh run 内的 `br-base0-br-base-bin` 已经能 `strings -a` 出全部 `[firesim-diag] coldboot:*` 标记。

随后起了一轮 fresh `thread-16` 最小 Linux metasim：

- runtime cfg：`/tmp/config_runtime_quad_rocket_ring_metasim_br_base_opensbidiag_threads16_20260311-023531.yaml`
- infrasetup log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--18-38-07-infrasetup-VYLGLTX9MCYGFFOG.log`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--18-41-57-runworkload-4BHZ9DTQ7PNM58R5.log`
- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-opensbidiag-threads16-20260311-023531/sim_slot_0`

关键观测：

- `uartlog` 只停在 `simulator_entry`、`FireSim fingerprint` 和若干条 `tsibridge_t::tick skipping tick`，没有进入任何 `[firesim-diag]` 标记。
- `heartbeat.csv` 只推进到 `39639, 5` 后就不再刷新。
- 但同一时刻 `VFireSim` 新进程 `3180096` 维持 `17` 个线程，其中约 `16` 个线程长期接近 `100% CPU`，明显不是 host wait/futex 型停住。

这与旧的 thread-1 行为不同：thread-1 更像 slow-progress，而这轮 thread-16 更像 Verilator 多线程 busy-spin 或零时延回路，已经不能再简单归类为“Linux 自身启动慢”。

顺带记录一个环境层面的坑：

- 在当前 agent 执行环境里，若直接在沙箱内起 `firesim infrasetup`/`firesim runworkload`，Python `multiprocessing.SemLock` 可能报 `PermissionError: [Errno 13]`。
- 这不是设计/配置错误，而是执行环境权限问题；需要切换到非沙箱执行。

### 12.8 2026-03-11 晚间收敛：thread-16 不是 guest 卡死，而是 Verilator NBA task graph 有静态 SCC

在 `thread-1` / `thread-16` 的 quad-ring Linux smoke 对照继续推进后，这一轮把结论从“OpenSBI banner 之后无输出”收紧到了 host 调度层：

- `thread-1` run：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-opensbidiag-threads1-20260311-024855/sim_slot_0`
  - `heartbeat.csv` 持续增长。
  - `uartlog` 已经出现 `Platform Name : ucb-bar,chipyard`、`Boot HART ID : 1`、`Linux version 6.6.0-00004-g67bc4513761f`。
- `thread-16` run：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-opensbidiag-threads16-20260311-023531/sim_slot_0`
  - 很早停在 `tsibridge_t::tick skipping tick`。
  - host 侧 `VFireSim` 保持 `17` 个线程高 CPU 忙跑。
  - 这已经足够证明 guest 启动链本身并未完全坏掉，问题只在多线程 Verilator 路径上触发。

为了避免继续拿已经被 `thread-1` 重写过的 `VFireSim.csrc` 做错位分析，这一轮重新使用匹配的 recipe 恢复了真实 16-thread generated C++：

- runtime config：`/tmp/config_runtime_quad_rocket_ring_metasim_br_base_opensbidiag_threads16_20260311-023531.yaml`
- recipe：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/config_build_recipes_quad_rocket_ring_linux_smoke_threads16.yaml`
- 重新 infrasetup 日志：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-10--23-49-18-infrasetup-GNY5CR1JB10R1YCJ.log`

在这份真实 16-thread `VFireSim.csrc` 上重新解析 `__Vm_mtaskstate_*` 后，得到两个关键结论：

1. `__Vm_mtaskstate_*` 的构造计数与 `signalUpstreamDone` 数量完全一致。
   - 这排除了“notify 丢失 / 某个 waiter 永远没人 signal”的简单解释。
2. task dependency graph 静态上存在强连通环（SCC）。
   - 解析得到的 SCC 为：`[9,85]`、`[186,188]`、`[87,88,117]`。
   - 这意味着 Verilator 16-thread NBA 任务图本身不可拓扑排序；worker 最终会互等，不是 guest 软件死锁。

其中最清晰的两个例子是：

- `task 9 <-> task 85`
  - `task 9`：`Vemul___024root____Vthread__nba__14`，`signal {80,101}`，随后 `wait 88`
  - `task 85`：`Vemul___024root____Vthread__nba__10`，`signal {117,157,169,80,87,88}`，随后 `wait 101`
  - 构成 `task85 --(state88)--> task9 --(state101)--> task85` 的二元环。
- `task 186 <-> task 188`
  - `task 186`：`signal {189,179}`，随后 `wait 172`
  - `task 188`：`signal {172,179}`，随后 `wait 189`
  - 构成 `task186 --(state189)--> task188 --(state172)--> task186` 的二元环。

而 `task 87 / 88 / 117` 形成的三元环则进一步说明问题不只在单一 bank scheduler 内部：

- `task 87`：`signal {90,168}`，随后 `wait 176`
- `task 88`：`signal {90,136,148,167,168,176,132}`，随后 `wait 131`
- `task 117`：`signal {124,131,141,148}`，随后 `wait 168`

从 task block 里的模块调用看，这些环主要落在：

- `coh_wrapper.l2.inclusive_cache_bank_sched*`
- `MSHR`
- 一部分已经跨到了 `system_bus_noc` 的 `input_buffer`
- 以及 `tile_prci_domain_*`

因此，这一轮正式把根因判断改写为：

- 不是 Linux / OpenSBI guest 逻辑首先死锁。
- 不是 `loadmem` / `TSI-only` / PLIC 诊断链首先命中根因。
- 是 Verilator 16-thread 对当前 quad-ring Linux smoke 设计生成了带 SCC 的 NBA worker 依赖图，最终停在 `VlMTaskVertex::waitUntilUpstreamDone` busy-spin。

这条结论解释了为什么：

- `VERILATOR_THREADS=1` 可以继续进入 Linux；
- `VERILATOR_THREADS=16` 会很早停住；
- `simif_emul_t::do_tick` 的 futex/condvar 只是 host 顶层线程在等 worker 收敛后的次级现象。

这也把下一跳工作从“继续怀疑 guest 冷启动窗口”切换成了两条：

1. 在真实 16-thread generated source / Verilog 上继续把 SCC 压到更具体的 ready-valid / combinational path。
2. 在根因未清楚前，把 `VERILATOR_THREADS=1` 保留为 Linux smoke 的功能基线，避免再把 host-scheduler 问题误判成 guest bring-up 问题。
