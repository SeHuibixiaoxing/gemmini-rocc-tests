# Pipeline Runtime Locked Decisions

## 1. 目标范围

1. `pipeline-runtime` 是 Gemmini pipeline 软件栈的执行实现，不引入 `backend {mudnacsim, gemmini}` 之类运行时分叉。
2. 当前阶段保留 single-layer-stage 契约，不实现 MudnacSim 风格的假多层 stage 语义。
3. 不修改 model 定义文件；HybridMapper 原有行为保持不变，只通过额外 exporter 产出 runtime 专用 artifacts。
4. 最终 correctness gate 只看 Linux on FireSim F2；metasim/baremetal 回归只承担回归安全网角色。
5. 当前目标硬件锚定为 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`。

## 2. Artifact 契约

6. Canonical exporter 是 `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`。
7. Canonical artifact 目录是 `conference/HybridMapper/output/pipeline_runtime/bertmini/`。
8. 当前固定消费的文件名是：
   `model.layers.yaml`、
   `gemmini_layer_mapping.<target_key>.yaml`、
   `pipeline_mapping.<target_key>.<method>.yaml`、
   `runtime_model.bin`、
   `runtime_input.<target_key>.bin`、
   `golden.<target_key>.<method>.bin`。
9. generated layer mapping 中的 SPM 地址是 local zero-based 视图；runtime 在执行时负责 rebasing、页表装载和 accelerator 动态分配。
10. 当前 generated mapping 中，`physicalAccIds` 不是主语义路径；活动语义仍然是 runtime 侧分配与 `vAccIdxList + accUtil`。

## 3. Runtime 与硬件接口

11. 当前 CLI 契约固定显式传入 `--layer-mapping-yaml`，不要再假设 runtime 自动发现该文件。
12. shared-spad xlate 控制面固定复用 Gemmini controller 的 `SPM_XLATE_CFG / RANGE / FLUSH / FAULT`，不再为 CoupledDMA 单独发明第二套软件接口。
13. Linux 执行中，DRAM backing 与 shared-spad alias/PT backing 必须保持两套独立语义；不能把 userspace VA 同时拿来当 shared-spad PTE 的物理页号。
14. 当前 critical DMA programming 区域应尽量保持为：
    `rr_set_opc -> fence rw, rw -> set_dst -> set_src -> wait`
    不要在这段临界区里长期保留重日志、`printf/fflush`、`getcpu()` 或额外系统调用。
15. 遇到 Gemmini/DMA 接口调用问题时，先对照这三个 Linux 正例：
    `rerocc_dma_matrix_linux_coupleddma.c`、
    `rerocc_lc_gemmini_matrix_linux_coupleddma.c`、
    `rerocc_lc_coverage_linux_coupleddma.c`。
16. 若问题涉及 overlap 或 completion/wait 语义，再补看：
    `rerocc_lc_nonblocking_linux_coupleddma.c`。

## 4. FireMarshal / FireSim 执行规则

17. FireMarshal 前先执行 `source /home/ubuntu/chipyard/env.sh`。
18. FireSim manager 前必须先执行：
    `cd /home/ubuntu/chipyard/sims/firesim`
    然后 `source sourceme-manager.sh`。
19. 不要再使用 `--skip-ssh-setup`。
20. 长时间 FireSim manager 任务统一通过 `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh` 启动。
21. FPGA 标准流程固定为：
    `marshal build -> marshal install -> launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`。
22. F2 只使用 `f2.6xlarge`。
23. 不手工修改 `sims/firesim/deploy/workloads/*`。
24. 一旦确认 run 已明确卡死，先 `terminaterunfarm`，再做 postmortem。
25. 成功判据不能只看 manager exit code；必须检查 `uartlog` 完成标记，必要时联查 `heartbeat.csv`。

## 5. 文档规则

26. 入口只看 `README.md`，架构只看 `ARCHITECTURE.md`，计划只看 `ROADMAP.md`，验证只看 `TESTPLAN.md`。
27. 当前 live 状态只看：
    `conference/mudnac_hybridmapper_collab_docs/STATUS.md`
    和 `NEXT_SESSION_PROMPT.md`。
28. 历史时间线、旧 run/log、失效路径只看 `docs/archive/2026Q1_history.md`，不要再把 dated updates 堆回主文档。
