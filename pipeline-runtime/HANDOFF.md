# Pipeline Runtime 当前快照

详细历史、旧 metasim stall 时间线和旧 run/log 路径见 `docs/archive/2026Q1_history.md`。

## 当前 baseline

- 主目标仍是 `bertmini on globalnoc Linux`；当前 FPGA 不可用，因此硬件相关验证先收敛到 `globalnoc metasim`。
- Host 侧 baseline 仍成立：`ours2 / gemini2 / tangram2` 三种方法都能在 `pipeline_runtime` 上完成 dummy-data correctness 闭环。
- globalnoc baremetal metasim 启动链 baseline 仍成立：旧的 startup stall 已修复，`globalnoc` quick-diag 可启动并推进。
- baremetal matrix 根因已闭环并完成回归：
  - multi-hart shared scratchpad 测试区必须按 `cid` 分区，不能复用固定 A/B 偏移。
  - ReRoCC 管理的 Gemmini completion barrier 必须用 `rr_fence(cfg_id)`，不能把 `gemmini_fence()` 当成 manager drain。
- baremetal coverage 根因已闭环并完成回归：`spm_xlate_ctrl_case` 必须在退出前显式恢复 Gemmini frontend shared-spad translation 默认态；`SPM_XLATE_FLUSH` 只清 fault，不清 `enable/range`。
- suite wrapper 的 false-stall 已修复：liveness 现在同时看 `metasim_stderr.out` 和 descendant `VFireSim` CPU 活动。
- baremetal nonblocking 已完成 fresh isolated PASS：当前 quick smoke 参数固定为 `dma_bytes=512 / long_conv=4 / short_conv=1 / long_resadd=32 / long_dma=4 / short_dma=1`，并显式输出 `warmup_start/warmup_done` 与 `SCENARIO_PHASE*`。

## 最近一次已验证结果

Host bertmini 三方法闭环：

```bash
bash /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

结果：2026-03-08 重新回归，`ours2=RC0`、`gemini2=RC0`、`tangram2=RC0`，脚本最终打印 `BERTMINI_HOST_CLOSURE_PASS`。

Linux overlay 静态验收：

```bash
cd /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload
HOST_INIT_CHECK_ONLY=1 bash host-init.sh
```

结果：`host-init check-only PASS`。

globalnoc metasim matrix isolated PASS：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-full-smalldma-nodasm-sharedspadfix-rrfence-20260308-051600`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-07--21-24-45-runworkload-Z6MP8L1YLGLPL9FQ.log`
- 结果：`GEMMINI_MATRIX_RESULT mode=full pass=4 fail=0 expected=4`、`DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`、`ALL_TESTS_PASS`。

globalnoc metasim coverage isolated PASS：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-coverage-xlatefix-20260308-103109`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--02-44-04-runworkload-Z55S9L7Y9JDU6PFX.log`
- 结果：`CASE_RESULT spm_xlate_ctrl PASS`、三个 `conv_*` 全部 `PASS`、全部 DMA case `PASS`、`COVERAGE_SUMMARY case0=1 ... case10=1`、`ALL_TESTS_PASS`。

suite 级最新状态：

- suite root：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260308-163933`
- matrix runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--08-42-55-runworkload-YCO6YZLATET4WL0Q.log`
- coverage runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--09-06-40-runworkload-BX052GVURUTRCBG2.log`
- nonblocking runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--09-17-43-runworkload-RWEKV88M40RMGML4.log`
- 结果：matrix PASS、coverage PASS、nonblocking PASS，suite 脚本最终打印 `[suite] Baremetal metasim suite PASS (1 run(s) each)`。

globalnoc metasim nonblocking isolated PASS：

- run dir：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-nonblocking-warmup-quick-20260308-161745`
- infrasetup log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--08-17-45-infrasetup-KHRV0ZQJXD2WH1IJ.log`
- runworkload log：`/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-08--08-21-28-runworkload-OF5H7K4UQTPNWWT7.log`
- 结果：`warmup_start -> warmup_done -> 4 个 SCENARIO_RESULT -> NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1 -> ALL_TESTS_PASS`。
- 备注：`conv_g0_vs_conv_g1_nonblocking` 的 `overlap=0` 仍为 PASS，因为当前非阻塞判据是 `short_before_long=1`，`overlap` 只作为观测指标。

## 当前 blocker / 风险
- 新的当前 blocker 已切换到 `Linux-only minimal metasim` 的 `VERILATOR_THREADS=16` 路径：`FireSimQuadRocketSbusRingNoCConfig + br-base.json` 在 16 线程时很早停止推进，而同一 workload 在 `VERILATOR_THREADS=1` 下已经能稳定越过 OpenSBI 并进入 Linux。
- thread-1 / thread-16 对照已经收敛出新的主结论：当前问题不是 `loadmem`、`TSI-only`、PLIC bring-up 或 OpenSBI 冷启动窗口本身，而是 Verilator 16 线程 NBA task graph 在当前 quad-ring Linux smoke 设计上形成了静态强连通环。
- 直接证据有两层：
  - live run 对照：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-opensbidiag-threads1-20260311-024855/sim_slot_0` 的 `heartbeat.csv` 持续增长并已打印 `Platform Name : ucb-bar,chipyard` / `Linux version 6.6.0-00004-g67bc4513761f`；而 `thread-16` run dir `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/brbase-opensbidiag-threads16-20260311-023531/sim_slot_0` 很早停在 `tsibridge_t::tick skipping tick`，host 侧 `VFireSim` 17 个线程几乎全核忙跑。
  - 实际 16-thread generated C++：重新用 `config_build_recipes_quad_rocket_ring_linux_smoke_threads16.yaml` 恢复了当前 `VFireSim.csrc` 后，`__Vm_mtaskstate_*` 构造计数与 `signalUpstreamDone` 数量完全一致，但 task graph 静态上存在 3 个 SCC：`[9,85]`、`[186,188]`、`[87,88,117]`。这说明不是 notify 丢失，而是多线程任务图本身不可拓扑排序。
- 当前已定位到的环主要落在 `coh_wrapper.l2.inclusive_cache_bank_sched* / MSHR`，并且有一部分边界已经跨到 `system_bus_noc` 的 `input_buffer` 和 `tile_prci_domain_*`。这与“guest 软件卡死”不同，更像 Verilator 多线程分区把相互依赖的 NBA 区域拆到了不同 worker task。
- OpenSBI staged workload 仍然有用，但现在它的作用已经从“怀疑 PLIC/irqchip”切换成“证明 thread-1 guest 路径是通的、thread-16 失败发生在 host 调度层”。
- 当前执行环境里，`firesim infrasetup`/`firesim runworkload` 还多了一层工具约束：若在沙箱内执行，Python `multiprocessing.SemLock` 可能直接报 `PermissionError: [Errno 13]`，因此这两类命令需要按非沙箱方式运行。
- 这轮排查的三个观测坑已经确认：
  - `+heartbeat-polling-interval=` 的计数基准是 host bridge tick，不是 target cycles；不能把 `heartbeat.csv` 的新增频率直接当成 guest 前进速度。
  - `uartlog` 含 `NUL` 字节，直接 `tail` 容易误读；判断 OpenSBI/Linux 阶段时需要用 `strings -a` 或按二进制解码查看。
  - FireSim 默认 `sim_slot_0` 容易复用旧目录；Linux metasim 对比必须给每轮独立 run dir，否则 `heartbeat.csv` / `uartlog` / mtime 会被旧数据污染。

- baremetal metasim smoke 已重新全绿；`nonblocking` 不再是当前 blocker。
- `nonblocking` 根因判断已收敛为 metasim slow-progress，而不是 DMA-first deadlock，主要由三件事叠加：
  - 第一次 `conv` 夹具构造和 reference 生成放在首个 timed phase 内，导致前期长时间看不到 phase 级进展。
  - 旧版 nonblocking 在 workload 路径上输出过多 UART 调试，host 时间被日志 IO 放大。
  - DRAM `CommandBusMonitor` 打开时，`metasim_stderr.out` 会被 command trace 洪泛，进一步主导 host 时间。
- 已落地的新修复：
  - `midas/models/dram/CommandBusMonitor` 改为默认关闭，默认以编译期静态开关禁用 DRAM command stream。
  - `rerocc_lc_nonblocking_baremetal_coupleddma.c` 把 `conv` fixture warmup 前移到 timed phase 之外，并缓存 reference；同时只保留 `warmup_*` 和 `SCENARIO_PHASE*` 级日志。
  - Gemmini / DMA 周期采样点统一移到 `rr_fence(cfg_id)` 之后，避免把未 drain 的 manager 当作完成。
- 当前剩余风险：
  - 当前 quick smoke 参数比历史 stress 版本更轻；若后续要恢复更重负载，需要按当前观测点和 timeout 策略重新标定。
  - FPGA 仍不可用，因此与最终目标最接近的验证仍停留在 `globalnoc metasim`，尚未回到 Linux target / FPGA replay。
  - `workload/host-init.sh` 现已支持 `HOST_INIT_CHECK_ONLY=1` 和 `SKIP_BUILD=1`，但真实 overlay 打包仍被 RISC-V Linux 工具链阻塞；2026-03-08 本机实际失败点是 `riscv64-linux-gnu-gcc: Command not found`。

## 接下来 3 个动作

1. 基于当前真实 `VFireSim.csrc` 把 3 个 SCC 继续往下压到更具体的 Verilog / ready-valid 信号路径，优先确认它们是 Verilator 分区缺陷还是当前 RTL 里确实存在应当被打断的组合依赖。
2. 在根因未消除前，保留 `VERILATOR_THREADS=1` 作为 Linux smoke 的功能基线；任何需要验证 guest 启动链是否正常的实验都先用单线程版本确认语义，再把 16 线程仅作为 host-scheduler 诊断。
3. 等最小 Linux smoke 的 thread-16 根因或最小规避方案稳定后，再把结论 replay 到 globalnoc Linux / pipeline-runtime 主目标，避免在目标配置上重复踩同一个 Verilator 多线程问题。
