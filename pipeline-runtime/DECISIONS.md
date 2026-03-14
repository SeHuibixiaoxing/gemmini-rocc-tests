# Pipeline Runtime Locked Decisions

## 当前主目标

1. `pipeline-runtime` 是 Gemmini pipeline 软件栈，不引入 `backend {mudnacsim, gemmini}` 之类运行时分叉。
2. MudnacSim 只作为参考模拟器，用于提供运行时协同机制和 shared pipeline YAML schema 参考。
3. 当前主硬件目标只保留 globalnoc，主配置锚定为 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`。
4. 最终执行环境只保留 Linux，主 FireSim runtime 配置锚定为 `config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml`。

## Artifact 与 schema 契约

5. bertmini 的模型定义 Python 源共享，不复制模型实现。
6. 除了 layer mapping 以外，Gemmini runtime 与 MudnacSim 尽量使用同一份 YAML 文件；共享的是 pipeline 编排 schema，不是完全相同的数值产物。
7. Gemmini runtime 固定消费：`layers_gemmini.yaml`、`mapping_gemmini/*.yaml`、`entire_model/*.yaml`、`model.bin`、`input.bin`、`golden.bin`。
8. `mapping_gemmini/` 必须从 `--model-yaml` 所在目录自动发现，不新增独立 CLI 参数。
9. `stages` 契约固定为 `list<list<object>>`；runtime 只消费 `stages[i][0]`，空列表报错，多候选允许告警但只取第 0 个。
10. 每个 stage 必须通过 `layerIdList[0] + accUtil + dramBypassList[0] + spmBypassList[0]` 唯一命中一个 Gemmini layer mapping candidate；漏命中或多命中都 fail fast。

## 当前执行范围

11. 第一阶段只支持 `conv` 和 `resadd`。
12. 第一阶段只支持 `layerIdList` 长度为 1 的 stage。
13. 第一阶段只支持 `entire_model` workload。
14. runtime 必须真正承接 `DRAM`、`DRAM_DEPEN`、`ISOLATE_SPM`、`SHARED_SPM`、`ALL_RINGBUFFER` 五类 tensor stay 语义。

## 正确性规则

15. bertmini 首轮端到端基线固定为 `ours2 / gemini2 / tangram2 + batch16 + 2 Gemmini + 1024KB per acc + dram_bw=19 + noc_bw=64`。
16. 输出正确性基线是 strict CPU golden compare；mismatch 必须返回非零。
17. 尺寸不匹配统一按“前缀裁剪 + 尾部零填充”处理：保留低地址起始的公共前缀，目标更大时高地址尾部补零。
18. 这条尺寸对齐规则必须在 CPU golden、host runtime 和 globalnoc Linux target 三处完全一致，不通过新增 YAML 字段单独表达。

## 验证顺序

19. 当前验证顺序固定为 `Artifact -> Host Linux -> Linux packaging -> globalnoc metasim smoke -> FPGA replay`。
20. quick-diag 和 baremetal metasim suite 现在只承担 smoke 角色，不再是终极验收。
21. 性能、cycle/ns 或 QoS 结论只在 FPGA replay 上建立；host 与 metasim 本地近似验证不输出这类最终结论。
22. 任何触及 RTL、FireSim 生成链、DMA/Gemmini 控制路径或 runtime-hardware 接口的改动，都允许立即用最新版硬件补 replay；当前交接默认目标是 AWS manager。

## FireSim / metasim 经验固化

23. FireSim emulation link 必须排除 `main.cc`，保留 Verilator 生成的 `main` 驱动 scheduler loop。
24. `plusarg_reader.v` 只在 `FireSim-generated.sv` 未定义 `module plusarg_reader` 时按需包含；原因是 FireSim 生成物在某些配置下已经内嵌该 module，若再无条件 include 会产生 duplicate module 定义并直接破坏 metasim/verilator 启动链。
25. metasim stall 的主要判据是 `uartlog` 和进程状态；`memory_stats*.csv` / `heartbeat.csv` 只是辅助信号。
26. 若 `gdb -p` 因 ptrace policy 失败，统一改用 run-under-gdb batch 抓栈。
27. multi-hart baremetal matrix 测试必须按 `cid` 划分每个 local shared scratchpad 内的测试地址空间；固定 A/B 偏移在 full matrix 下会让不同 hart 互相覆盖，因为配对的 Gemmini manager 与 CoupledDMA manager 实际共享同一块 local shared scratchpad。
28. 在 ReRoCC 管理的 Gemmini workload 里，凡是“复用 accelerator 内部状态”或“读取 accelerator 输出”的位置，都必须使用 `rr_fence(cfg_id)` 作为 completion barrier；`gemmini_fence()` 只是 CPU memory fence，不能替代 manager drain。
29. `spm_xlate` 控制路径测试若临时打开 Gemmini frontend shared-spad translation，必须在用例结束前显式恢复 controller 默认态；`SPM_XLATE_FLUSH` 只清 fault 状态，不会自动清掉 `enable/range`。
30. baremetal metasim suite 的 stall 判定不能只看 `uartlog/heartbeat`；还必须把 `metasim_stderr.out` 更新和 `VFireSim` 子进程 CPU 活动视为有效活性信号。
31. 当前 localhost metasim suite 默认优先使用 `RUN_TIMEOUT_SECS=0`；若必须设置固定上限，需要按 workload 版本和 quick 参数重新验证，不能假设旧 timeout 仍然可靠。
32. DRAM model 的 `CommandBusMonitor` 必须保持编译期默认关闭，不能再通过新的 host-side plusarg 打开。原因有两层：一是该 trace 在 DMA-heavy metasim case 上会显著放大 `metasim_stderr.out` 并主导仿真时间；二是本轮实验表明，在该 host-side DRAM model 上新增 plusarg 路径会把问题重新带回早期启动阶段。
33. baremetal nonblocking 的 metasim quick 默认参数锁定为 `dma_bytes=512 / long_conv=4 / short_conv=1 / long_resadd=32 / long_dma=4 / short_dma=1`，直到更重负载在同样的观测策略下重新完成回归。
34. baremetal nonblocking 必须把 `conv` fixture warmup 放在 timed phase 之外，并缓存 reference；正常路径只保留 `warmup_*` 和 `SCENARIO_PHASE*` 级日志，逐 iter UART 调试只允许作为临时诊断手段。
35. baremetal nonblocking 的正确性判据锁定为：每个场景都满足 `long_ok=1`、`short_ok=1`、`short_before_long=1`，最终 `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1` 且 `ALL_TESTS_PASS`。`overlap=` 只是诊断信号，不单独决定 PASS/FAIL。
36. shared scratchpad 地址翻译默认保持 legacy 直映行为；只有目标 globalnoc coupled-DMA 硬件配置显式打开 `SharedScratchpadConfig.use_page_table_xlate` 时，才启用软件编程的 shared-spad page-table 模式。
37. 在 shared-spad page-table 模式下，命中已编程范围且 `enable=1` 的访问必须走 Gemmini 专用 shared-spad PTW/TLB；命中范围且 `enable=0` 的访问必须直接把该虚拟地址当作 shared scratchpad 物理地址透传。当前 PTE 打包格式锁定为 `bit0=valid`，高位为物理页号。
38. 当前 shared-spad page-table 模式只允许单个 outstanding TLB miss；这是本轮锁定的简化方案。若后续要扩到 multi-miss，必须单独立 decision 并重新回归 baremetal matrix / coverage / nonblocking。
39. shared-spad PTW refill 和 hit 计算必须精确遵守软件编程的 `spm_xlate_page_shift`，不能再钳到 Rocket `pgIdxBits`。当前 baremetal 软件页表固定使用 1 KiB 页；若硬件擅自升到 4 KiB，会把 PTE 物理页基址左移错位，直接打坏 `shared_mv_xlate` 等 shared scratchpad 地址翻译路径。

## 文档规则

40. 入口只看 `README.md`，架构只看 `ARCHITECTURE.md`，计划只看 `ROADMAP.md`，验证只看 `TESTPLAN.md`，当前状态只看 `HANDOFF.md`，历史只看 archive。
41. shared-spad page-table context 现在按 `gemmini_id` 同时服务两条请求入口：`Gemmini compute/load-store` 与 `GemminiCoupledDMA`。runtime 只允许通过 Gemmini controller 现有 `SPM_XLATE_CFG / RANGE / FLUSH / FAULT` 指令装载这套上下文，不再为 CoupledDMA 单独发明第二套软件控制面。
42. runtime 在启动时必须一次性完成 shared-spad translation bootstrap：确定 alias range、准备 PTBR/PTE backing，并对所有 Gemmini manager 下发 `SPM_XLATE_CFG / RANGE`。segment 切换时只允许做 `FLUSH`，不应把同样的 `CFG / RANGE` 每轮重写一遍。
43. Linux 上 shared-spad alias VA base 允许 runtime 动态保留，不再强制硬编码 `0x80000000`；但 PTW 使用的 PTE slab 不能继续把用户态 VA 假装成 PA。若目标是 Linux 硬件执行，runtime 必须提供可验证的物理 backing；当前代码先落地为“小块连续页探测 + pagemap 校验”的中间方案，后续若切到专用驱动或 CMA allocator，必须保持 `ptbr + vpn * 8` 这个硬件契约不变。
44. 2026-03-12 本地成功生成的 `built-hwdb` 入口与 `firesim.tar.gz` 是 AWS replay 的 canonical handoff 物料；若 hwdb 内 `bitstream_tar` 仍指向本地 `file:///home/wzy/...` 路径，AWS 必须复制产物并重写路径，或直接在 AWS 重新 `buildbitstream`。
