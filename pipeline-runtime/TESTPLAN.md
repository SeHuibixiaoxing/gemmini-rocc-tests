# Pipeline Runtime Test Plan

## 1. 验收目标

主验收固定为 bertmini 端到端闭环：

- `model = bertmini`
- `methods = ours2 / gemini2 / tangram2`
- `batch = 16`
- `num_gemmini = 2`
- `spm_per_acc_kb = 1024`
- `dram_bw = 19`
- `noc_bw = 64`

当前 FPGA 不可用，因此验证分成三层：

1. Artifact 与 schema 层
2. Host Linux correctness 层
3. globalnoc Linux packaging / metasim smoke 层

FPGA 恢复后，再进入最终 replay 层。

## 2. Artifact 生成与 schema 校验

目标：稳定生成 bertmini 的 Gemmini runtime 产物，并保持 shared pipeline YAML 契约不漂移。

命令：

```bash
cd /home/wzy/proj/wp2/chipyard/tmp/HybridMapper
python3 scripts/create-gemmini-pipeline-runtime-artifacts.py --model bertmini
```

至少检查以下文件存在：

- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/layers_gemmini.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/mapping_gemmini/0.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/2_1024_16_19_64_ours2.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/2_1024_16_19_64_gemini2.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/2_1024_16_19_64_tangram2.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin`

通过标准：

- `layers_gemmini.yaml` 可被 runtime 解析。
- `mapping_gemmini/*.yaml` 对每层至少包含一个可命中的 canonical candidate。
- `entire_model/*.yaml` 保持 MudnacSim 协同文档约定的 `stages: list<list<object>>` 结构。

## 3. Host Linux bertmini correctness

目标：让三种方法都通过同一份 CPU golden。

构建命令：

```bash
make -C /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all
```

执行命令：

```bash
bash /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

通过标准：

- 三种方法都 `RC=0`。
- 无 parser fallback。
- 无 candidate 多命中 / 漏命中。
- 输出与 `golden.bin` 完全一致。

额外一致性要求：

- 尺寸不匹配一律按“前缀裁剪 + 尾部零填充”处理。
- CPU golden 与 runtime 必须共享这条语义。

## 4. Linux target 打包检查

目标：确保 `rerocc-linux-tests` overlay 链可以容纳 bertmini runtime workload。

当前可做的检查：

```bash
cd /home/wzy/proj/wp2/chipyard
bash -n generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload/host-init.sh
sh -n generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
cc -I generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include \
  -c generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_pipeline_runtime_linux.c \
  -o /tmp/rerocc_pipeline_runtime_linux.o

cd generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload
HOST_INIT_CHECK_ONLY=1 bash host-init.sh
```

交叉工具链恢复后执行：

```bash
cd /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload
bash host-init.sh
```

如果 Linux 二进制已经预先构建好，也可以执行：

```bash
cd /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload
SKIP_BUILD=1 bash host-init.sh
```

通过标准：

- `rerocc_pipeline_runtime_linux.c` 可通过头文件和语法检查。
- `host-init.sh` / `run_rerocc_pipeline_runtime_bertmini.sh` 无 shell 语法错误。
- `HOST_INIT_CHECK_ONLY=1 bash host-init.sh` 返回 PASS，证明 artifact 路径和 canonical 文件名齐全。
- 工具链恢复后，overlay 中固定出现以下文件：
  - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/layers_gemmini.yaml`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/mapping_gemmini/*.yaml`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/entire_model/2_1024_16_19_64_<method>.yaml`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.bin`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/input.bin`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/golden.bin`

## 5. globalnoc 硬件链 smoke

目标：确认本轮改动没有破坏 globalnoc 启动链。

当前稳定入口：

```bash
source ~/.ssh/AGENT_VARS
cd /home/wzy/proj/wp2/chipyard/sims/firesim
source ./sourceme-manager.sh --skip-ssh-setup
cd deploy
SUITE_RUNS=1 RUN_TIMEOUT_SECS=0 ./run_rerocc_coupleddma_baremetal_metasim_suite.sh
```

说明：

- quick-diag 和 baremetal metasim suite 现在都只是 smoke，不是终极目标。
- nonblocking quick smoke 默认参数固定为 `dma_bytes=512 / long_conv=4 / short_conv=1 / long_resadd=32 / long_dma=4 / short_dma=1`。
- nonblocking 的基础观测点固定为 `warmup_start` / `warmup_done` / `SCENARIO_PHASE*`；不再在正常路径保留逐 iter UART 日志。
- `RUN_TIMEOUT_SECS=0` 仍是最稳妥的 suite 默认值；若必须设置上限，需要按当前 quick 参数和 workload 版本重新验证。
- suite 的 stall 观察不能只靠 `uartlog/heartbeat`；若 `metasim_stderr.out` 仍在推进且 `VFireSim` 子进程仍有 CPU 活动，应判定为 slow-progress 而不是 stalled。
- Linux minimal repro 还要额外遵守五条观测规则：
  - `+heartbeat-polling-interval=` 是 bridge tick 计数，不是 target cycle 周期；`100` 只适合短时诊断，不适合据此判定 Linux boot 卡点。
  - `uartlog` 可能夹杂 `NUL`；若要判断是否越过 OpenSBI banner，优先用 `strings -a uartlog` 或二进制解码，而不是普通 `tail`。
  - 默认 `default_simulation_dir` 指向公共 `sim_slot_0` 时，不能直接把不同 run 的 `heartbeat.csv`/`uartlog` 拿来横向比较；必须给每轮独立 run dir。
  - 比较不同 `verilator_threads` 时，不能只看 FireSim manager 的 `Sim running: True`；必须同时记录 `VFireSim` 的线程数、CPU 占用和 `heartbeat.csv` 是否继续刷新。当前已观察到 `thread-16` 会出现“17 个 host 线程几乎全核忙跑，但 guest-visible `heartbeat/UART` 很早停住”的形态。
  - 若 `thread-1` 能进入 Linux、`thread-16` 却早期 busy-spin，则应继续检查当前真实 `VFireSim.csrc` 的 `__Vm_mtaskstate_*` 图，而不是回到 guest 侧怀疑链。2026-03-11 的 quad-ring Linux smoke 已经在真实 16-thread generated C++ 中确认了 3 个 task-graph SCC（`[9,85]`、`[186,188]`、`[87,88,117]`）；此时应判定为 Verilator 多线程调度层问题。
  - 在当前 agent 执行环境中，`firesim infrasetup`/`firesim runworkload` 可能因为 Python `multiprocessing.SemLock` 创建失败而直接报 `PermissionError: [Errno 13]`；遇到这种情况应切换到非沙箱执行，而不是把它误判成 FireSim 设计问题。
- DRAM command trace 必须在源码中保持编译期默认关闭；若未来需要看 DRAM 命令流，应谨慎以源码改动临时打开，因为新的 host-side plusarg 路径会把问题重新带回启动链。
- 如果改动触及 FireSim 启动链、Verilator 入口或 plusarg 处理，还需要补跑 globalnoc quick-diag；历史 run/log 与调试细节见 archive。

通过标准：

- 启动链不再出现旧的 metasim startup stall。
- suite 中的 globalnoc baremetal 项不回退。
- 至少保留两条人工 metasim gate：
  - `nodasm + single pair + logical_core=1 + dma_bytes=1024` PASS。
  - `nodasm + full matrix + logical_cores=2 + dma_bytes=1024` PASS。
- nonblocking quick gate 通过标准固定为：`NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1` 且 `ALL_TESTS_PASS`。`overlap=` 只作为观测指标，不单独决定 PASS/FAIL。

最新已验证的 full matrix 回归：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-full-smalldma-nodasm-sharedspadfix-rrfence-20260308-051600`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--21-24-45-runworkload-Z6MP8L1YLGLPL9FQ.log`
- 结果：`GEMMINI_MATRIX_RESULT mode=full pass=4 fail=0 expected=4`、`DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`、`ALL_TESTS_PASS`。

最新已验证的 coverage 回归：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-coverage-xlatefix-20260308-103109`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--02-44-04-runworkload-Z55S9L7Y9JDU6PFX.log`
- 结果：`CASE_RESULT spm_xlate_ctrl PASS`、三个 `conv_*` 全部 `PASS`、`resadd` 和全部 DMA case `PASS`，最终 `COVERAGE_SUMMARY case0=1 ... case10=1`、`ALL_TESTS_PASS`。

最新已验证的 nonblocking isolated 回归：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-nonblocking-warmup-quick-20260308-161745`
- infrasetup log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--08-17-45-infrasetup-KHRV0ZQJXD2WH1IJ.log`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--08-21-28-runworkload-OF5H7K4UQTPNWWT7.log`
- build config：`/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/build-config-nonblocking.txt`
- 结果：4 个 `SCENARIO_RESULT` 全部 `pass=1`，最终 `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`、`ALL_TESTS_PASS`。

最新已验证的 baremetal metasim suite 回归：

- suite root：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260308-163933`
- matrix runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--08-42-55-runworkload-YCO6YZLATET4WL0Q.log`
- coverage runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--09-06-40-runworkload-BX052GVURUTRCBG2.log`
- nonblocking runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--09-17-43-runworkload-RWEKV88M40RMGML4.log`
- 结果：三项都在各自 fresh run dir 中找到 `ALL_TESTS_PASS`，suite 脚本最终打印 `[suite] Baremetal metasim suite PASS (1 run(s) each)`。

## 6. FPGA replay

当前状态：deferred。

FPGA 恢复后必须重跑：

```bash
source ~/.ssh/AGENT_VARS
cd /home/wzy/proj/wp2/chipyard/sims/firesim
source ./sourceme-manager.sh --skip-ssh-setup
cd deploy
firesim infrasetup \
  -c config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes.yaml
firesim runworkload \
  -c config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes.yaml
```

终极通过标准：

- Linux workload 依次跑完 `ours2 / gemini2 / tangram2`。
- 三种方法都打印 PASS。
- 结果与同批 CPU golden 一致。
