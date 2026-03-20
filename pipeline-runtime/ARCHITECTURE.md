# Pipeline Runtime 架构说明

## 1. 目标与边界

`pipeline-runtime` 的职责是消费 HybridMapper 导出的 runtime interface，并把它映射到 Gemmini/ReRoCC/CoupledDMA 硬件执行路径。

当前边界固定为：

- 模型主线只看 `bertmini`
- 最终 correctness gate 只看 Linux on FireSim F2
- 当前阶段只保留 single-layer-stage 契约
- 不在 runtime 中引入 `backend {mudnacsim, gemmini}` 之类的接口分叉
- metasim 和 baremetal coupleddma 回归只作为回归安全网，不是最终验收

## 2. Artifact 生产链

### 2.1 Exporter

当前 canonical exporter 是：

- `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`

默认输出目录是：

- `conference/HybridMapper/output/pipeline_runtime/bertmini`

### 2.2 Runtime artifacts

当前 runtime 真实消费的文件名与 `create-pipeline-runtime-artifacts.py` 保持一致：

- `model.layers.yaml`
- `gemmini_layer_mapping.<target_key>.yaml`
- `pipeline_mapping.<target_key>.<method>.yaml`
- `runtime_model.bin`
- `runtime_input.<target_key>.bin`
- `golden.<target_key>.<method>.bin`

当前主目标对应的 `target_key` 是：

- `rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024`

这些文件会同时被：

- host closure 脚本读取
- Linux overlay 打包脚本读取
- guest 内的 `rerocc_pipeline_runtime-linux` 读取

## 3. Runtime 软件结构

### 3.1 两个二进制形态

- Host binary:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime`
- RISC-V Linux binary:
  `generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`

两者共享同一套核心实现，只是在构建系统和运行环境上不同。

### 3.2 关键源文件

- `src/main.c`
  CLI、默认配置、`[prt-early]` 启动日志
- `src/prt_runtime.c`
  runtime init/run/destroy、shared-spad xlate bootstrap、stage worker 启动
- `src/prt_dma.c`
  CoupledDMA submit/wait 关键路径
- `src/prt_rerocc.c`
  ReRoCC acquire/opcode/manager 绑定逻辑
- `src/prt_gemmini_artifacts.c`
  layer mapping / pipeline mapping 解析与校验
- `src/prt_page_table.c`
  Linux HugeTLB、pagemap 校验、PTBR/PTE backing 准备
- `src/prt_scheduler.c`
  stage 调度与 worker 生命周期

## 4. Linux 打包与 guest 布局

### 4.1 Host-side staging

当前 Linux 打包入口是：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh`

它负责：

- 构建 `rerocc_pipeline_runtime-linux`
- 构建/检查 Linux coupleddma 回归二进制
- 把 runtime binary 与 `bertmini` artifacts staged 到 FireMarshal overlay

### 4.2 FireMarshal workload

当前 dedicated workload 是：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json`

guest 入口脚本是：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`

### 4.3 Guest 内固定路径

guest rootfs 中的 canonical 路径是：

- `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.<target_key>.yaml`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.<target_key>.<method>.yaml`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/runtime_model.bin`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/runtime_input.<target_key>.bin`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/golden.<target_key>.<method>.bin`

## 5. 软件到硬件的耦合点

### 5.1 目标硬件配置

当前主配置锚点在：

- `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCCoupledDMAConfigs.scala`

其中主配置类是：

- `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`

### 5.2 关键硬件文件

- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`
- `generators/gemmini/src/main/scala/gemmini/Controller.scala`
- `generators/gemmini/src/main/scala/gemmini/FrontendTLB.scala`
- `generators/gemmini/src/main/scala/gemmini/SharedScratchpad.scala`
- `generators/rerocc/src/main/scala/client/Client.scala`
- `generators/rerocc/src/main/scala/manager/Manager.scala`

### 5.3 Shared scratchpad translation

当前 shared-spad translation 的控制面固定复用 Gemmini controller 现有接口：

- `SPM_XLATE_CFG`
- `SPM_XLATE_RANGE`
- `SPM_XLATE_FLUSH`
- `SPM_XLATE_FAULT`

这套 context 同时服务：

- Gemmini compute/load-store
- GemminiCoupledDMA

也就是说，软件不会再为 CoupledDMA 发明第二套单独控制面。

## 6. 当前执行契约

### 6.1 Runtime CLI 契约

当前 `pipeline_runtime` / `rerocc_pipeline_runtime-linux` 的关键输入是：

- `--model-yaml`
- `--layer-mapping-yaml`
- `--pipeline-yaml`
- `--model-bin`
- `--input`
- `--golden` 或 `--golden-out`

当前代码里 `--layer-mapping-yaml` 仍是显式参数，不是自动发现模式。

### 6.2 Mapping 与地址语义

- layer mapping 中的 shared-SPM 地址是 local zero-based 视图
- runtime 在执行时负责实际 rebasing、页表装载和 accelerator 动态分配
- 当前生成的 mapping 中，`physicalAccIds` 不是主语义路径

### 6.3 DMA/Gemmini 接口调试基线

遇到 runtime-hardware 接口问题时，优先对照 Linux 下已通过的 coupleddma 回归：

- `rerocc_dma_matrix_linux_coupleddma.c`
- `rerocc_lc_gemmini_matrix_linux_coupleddma.c`
- `rerocc_lc_coverage_linux_coupleddma.c`
- `rerocc_lc_nonblocking_linux_coupleddma.c`

特别是 `set_dst -> set_src -> wait` 的临界区，应尽量贴近这些正例，不要先从 baremetal 语义或自创调用序列推导。

Linux userspace DMA 的通用 guardrails 另外单列在：

- `docs/linux_dma_guardrails.md`

当前必须记住的规则：

- host DRAM <-> shared-SPM 不能把跨页 host buffer 当成单个连续 PA 区间
- host DRAM 路径必须保持：
  - 按 host page 分 chunk
  - 每 chunk 单独 `virt_to_phys`
  - `bytes >= 64` 且 `src_mod64 != dst_mod64` 时使用 bounce buffer
- overlap fast path 不能绕过这些 guardrails；当前只允许在 contiguous SPM <-> SPM 路径上走 single-request submit

## 7. 验证层级

当前验证顺序固定为：

1. artifact export 与 schema 校验
2. host closure
3. Linux overlay / binary staging
4. Linux coupleddma 回归对照
5. FireMarshal image build/install
6. FireSim F2 replay

当前 live blocker、最新停点和下一轮实验，不写在本文件，统一看：

- `NEXT_SESSION_PROMPT.md`
- `conference/mudnac_hybridmapper_collab_docs/STATUS.md`
