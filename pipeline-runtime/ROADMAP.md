# Pipeline Runtime Roadmap

当前阶段名：`bertmini end-to-end closure on globalnoc Linux`

终极验收固定为：

- 模型：`bertmini`
- 方法：`ours2 / gemini2 / tangram2`
- batch：`16`
- 目标硬件：`globalnoc + ReRoCC + CoupledDMA`
- 目标运行环境：Linux
- 结果要求：runtime 输出与 CPU golden 一致

## Phase 1. 项目定义与边界

目标：

- 固定 `pipeline-runtime` 是 Gemmini pipeline 软件栈。
- 固定 MudnacSim 只是参考模拟器。
- 固定 globalnoc-only、Linux-only、file-loading artifact contract。

当前状态：已完成。

下一步：无。

退出条件：相关规则已写入 `DECISIONS.md` 且主文档不再混入 backend 分叉叙事。

## Phase 2. Artifact 契约与 runtime 核心落地

目标：

- 用同一份 bertmini 模型定义生成 Gemmini runtime 所需 artifacts。
- 保持与 MudnacSim 共享 `entire_model/*.yaml` schema。
- 在 runtime 中实现 stage 契约、Gemmini candidate 唯一命中和 tensor stay 语义。

当前状态：基础链路已完成。

- `create-gemmini-pipeline-runtime-artifacts.py` 已能产出 `layers_gemmini.yaml`、`mapping_gemmini/` 和三份 canonical pipeline YAML。
- runtime 已能从 `--model-yaml` 自动发现 `mapping_gemmini/`，并按 stage 元数据校验 candidate。
- 前缀裁剪 + 尾部零填充的尺寸对齐规则已落到 dummy golden 生成和 runtime compare 路径。

下一步：

- 把当前 host-friendly canonical pipeline YAML 继续逼近真实双 Gemmini 资源约束。
- 为尺寸不匹配规则补一组更显式的 synthetic regression。

退出条件：artifact 生成稳定，schema 固定，runtime 对错误 stage / candidate fail fast。

## Phase 3. Host bertmini correctness

目标：

- 在 host Linux 上跑通 bertmini 的 `ours2 / gemini2 / tangram2`。
- 三种方法都以同一份 CPU golden 为准，全部 `RC=0`。

当前状态：已完成第一轮闭环。

- 三种方法当前都已在 host `pipeline_runtime` 上返回 `RC=0`。
- 闭环依赖的文件固定在 `tmp/HybridMapper/output/pipeline/bertmini/`。

下一步：

- 增加更细粒度的 parser failure、page leak、stale allocation 和 tensor alias regression。
- 对当前 canonical YAML 的资源字段做更强的一致性检查。

退出条件：三方法 host correctness 稳定可重放，且不依赖人工修补 artifact。

## Phase 4. globalnoc Linux packaging 与 metasim smoke

目标：

- 生成 `rerocc_pipeline_runtime-linux`。
- 通过 `rerocc-linux-tests` overlay 固定部署 bertmini 全套文件。
- 保持 globalnoc metasim / quick-diag 作为启动链 smoke。

当前状态：进行中。

- `rerocc_pipeline_runtime_linux.c` 和 `run_rerocc_pipeline_runtime_bertmini.sh` 已就位。
- overlay 路径已经固定到 `/root/rerocc-linux-tests/pipeline-runtime/...`。
- `workload/host-init.sh` 已补成支持 `HOST_INIT_CHECK_ONLY=1` 和 `SKIP_BUILD=1` 的两段式流程；当前环境仍缺少 `riscv64-linux-gnu-gcc` / `riscv64-unknown-linux-gnu-gcc`，因此真实交叉编译验证被阻塞。

下一步：

- 当前先用 `HOST_INIT_CHECK_ONLY=1 bash workload/host-init.sh` 保持 overlay 输入静态验收。
- 交叉工具链恢复后立即执行 `workload/host-init.sh`，验证二进制和 overlay 打包。
- 维持 globalnoc metasim suite 作为硬件链 smoke，防止本轮改动破坏启动链。

退出条件：

- `rerocc_pipeline_runtime-linux` 可构建。
- overlay 中 bertmini 文件齐全且路径固定。
- globalnoc metasim smoke 不回退。

## Phase 5. globalnoc Linux FPGA replay

目标：

- 在 `config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml` 上跑 bertmini 三种方法。
- 三种方法都输出 PASS，且与 CPU golden 一致。

当前状态：deferred，等待 FPGA 恢复。

下一步：

- FPGA 可用后，用最新版硬件立即 replay 与硬件行为直接相关的必要测试。
- 若 replay 暴露资源约束差异，回推改进 HybridMapper 的 Gemmini layer mapping 与 canonical pipeline emitter。

退出条件：三方法在 globalnoc Linux 目标硬件上通过最终闭环。

## Phase 6. schedule_point / profiling / QoS

目标：

- 在 bertmini 端到端闭环之后，再推进 schedule_point、profiling 聚合和 QoS 调度。

当前状态：未开始。

下一步：等 Phase 5 通过后再启动。

退出条件：另立 decision 后再定义，不作为当前 blocker。
