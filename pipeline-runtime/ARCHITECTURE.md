# Pipeline Runtime 架构说明

## 1. 目标与边界

`pipeline-runtime` 的职责不是做一个可切换 backend 的通用执行器，而是实现 Gemmini 中 pipeline 软件栈的执行侧。
当前唯一主目标是：

- 输入 HybridMapper 为 `bertmini` 生成的 pipeline 编排结果。
- 在 `globalnoc + ReRoCC + CoupledDMA` 最新硬件上执行这些 stage。
- 让 Linux 目标侧结果与 CPU golden 一致。

MudnacSim 的定位是参考模拟器：

- 参考其运行时协同机制。
- 复用同一份 pipeline 编排 YAML schema。
- 不要求与 Gemmini runtime 对同一模型生成完全相同的 model / layer mapping 数值文件。
- 不在 runtime 中引入 `backend {mudnacsim, gemmini}` 之类分叉字段。

当前阶段的非目标：

- 不把 quick-diag 或 baremetal metasim 当作终极验收。
- 不在 host/metsim 本地近似验证上输出性能、cycle/ns 或 QoS 结论；这类结论只在 AWS FPGA replay 上建立。
- 不扩展到 non-globalnoc 主路径。

## 2. 外部组件与协同关系

### 2.1 HybridMapper

根目录固定为 `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper`。

它负责离线生成：

- bertmini 模型定义导出的层信息。
- 逐层 layer mapping。
- 全图 pipeline 编排 `entire_model/*.yaml`。
- 运行时需要的 dummy `model.bin / input.bin / golden.bin`。

### 2.2 MudnacSim

根目录固定为 `/home/wzy/proj/wp2/chipyard/tmp/MudnacSim`。

它是参考模拟器，主要提供两类参考：

- pipeline runtime 协同机制。
- `entire_model/*.yaml` 的消费契约。

本项目当前采用的协同基线来自：

- `/home/wzy/proj/wp2/chipyard/tmp/mudnac_hybridmapper_collab_docs/v2/协同机制文档_v2.md`
- `/home/wzy/proj/wp2/chipyard/tmp/mudnac_hybridmapper_collab_docs/v3/协同机制文档_v3.md`

### 2.3 pipeline-runtime

`pipeline-runtime` 是唯一执行实现，分成两种构建形态：

- host Linux binary：`pipeline_runtime`
- RISC-V Linux target binary：`rerocc_pipeline_runtime-linux`

两者复用同一套核心执行逻辑：

- `prt_main_entry(...)`
- YAML 装载
- stage 解析
- tensor 生命周期管理
- Gemmini artifacts 校验
- strict golden compare

### 2.4 rerocc-linux-tests overlay

最终目标运行链不新造部署系统，而是复用现有 `rerocc-linux-tests` overlay / rootfs 机制。

目标 rootfs 约定路径固定为：

- `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/layers_gemmini.yaml`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/mapping_gemmini/*.yaml`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/entire_model/2_1024_16_19_64_<method>.yaml`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.bin`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/input.bin`
- `/root/rerocc-linux-tests/pipeline-runtime/bertmini/golden.bin`

## 3. Artifact 契约

### 3.1 共享与分叉的边界

当前协议是：

- 模型定义 Python 源共享，不复制模型实现。
- pipeline 编排 YAML 共享，直接沿用 MudnacSim 使用的 `entire_model/*.yaml` schema。
- layer mapping 不强求与 MudnacSim 数值一致，Gemmini runtime 维护自己的 `layers_gemmini.yaml` 和 `mapping_gemmini/`。

也就是说，除了 layer mapping 以外，执行侧与参考模拟器尽量使用相同的 YAML 文件和相同的 schema 语义。

### 3.2 bertmini 目录布局

当前 canonical 产物位于：

- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/layers_gemmini.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/mapping_gemmini/*.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/2_1024_16_19_64_ours2.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/2_1024_16_19_64_gemini2.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/2_1024_16_19_64_tangram2.yaml`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin`
- `/home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin`

### 3.3 Gemmini layer mapping 的最小字段

`mapping_gemmini/<layer>.yaml` 当前只导出 runtime 真正要消费的字段：

- `target.accel`
- `mapping.tile`
- `mapping.dramBypass`
- `mapping.spmBypass`
- `performance.accelUtil`
- `performance.spmUtil`
- `performance.spmPageUtil`
- `performance.dramAccess`
- `others.spmTensorPageCount`
- `others.spmTensorUtil`
- `others.totalTiles`
- `others.spmDimensions`
- `others.spmTensorAddr`
- `others.firstTensorPageNum`

这份 layer mapping 的目标不是复刻原 HybridMapper 全量表达，而是为 runtime 提供：

- 该层如何切 accelerator 内 tile。
- 该层需要多少 shared SPM 页。
- 该层是否走 DRAM / SPM bypass。
- 后续 pipeline mapping 生成时需要参考的基本容量信息。

## 4. Runtime 消费契约

### 4.1 文件式装载

runtime 通过 CLI 从文件装载 artifacts：

- `--model-yaml` 指向 `layers_gemmini.yaml`
- `--pipeline-yaml` 指向共享 `entire_model/*.yaml`
- `--model-bin` / `--input` / `--golden` 指向 bertmini 二进制文件

`mapping_gemmini/` 不通过独立参数传入，而是从 `--model-yaml` 所在目录自动发现。

### 4.2 stage 与 candidate 匹配

当前实现与 MudnacSim 协同文档保持同一 stage 契约：

- `stages` 是 `list<list<object>>`
- runtime 只消费 `stages[i][0]`
- 空列表直接报错
- 多候选允许告警，但只取第 0 个

对 Gemmini layer mapping 的命中规则固定为：

- 用 `layerIdList[0] + accUtil + dramBypassList[0] + spmBypassList[0]`
- 在 `mapping_gemmini/<layer>.yaml` 中唯一命中一个 candidate
- 找不到或多命中都 fail fast

### 4.3 第一阶段执行限制

当前只保证以下范围：

- 只支持 `conv` 和 `resadd`
- 只支持 `layerIdList` 长度为 1 的 stage
- 只支持 `entire_model`

### 4.4 tensor stay 语义

当前 runtime 真正承接以下五类 tensor stay 语义：

- `DRAM`
- `DRAM_DEPEN`
- `ISOLATE_SPM`
- `SHARED_SPM`
- `ALL_RINGBUFFER`

这些语义直接决定：

- 数据从 DRAM 取还是从 shared / isolate SPM 继承
- 是否需要 ring buffer 参与 decoupling
- stage 间 buffer 生命周期如何切换

### 4.5 shared scratchpad 地址翻译

当前 globalnoc 目标硬件上的 shared scratchpad 地址翻译分成两条兼容路径：

- legacy 直映路径：`use_page_table_xlate=false`，保留原先的 range-base 到 shared scratchpad base 的直接偏移翻译，不影响旧配置。
- page-table 路径：`use_page_table_xlate=true`，在 Gemmini frontend TLB 里为 shared-spad 范围单独维护一套小 TLB，并通过专用 `SpmPageTableWalker` 从普通内存读取 PTE。

2026-03-12 起，目标 globalnoc coupled-DMA 配置进一步把这套 shared-spad translation context 共享给 `GemminiCoupledDMA`：

- Gemmini compute/load-store 路与 Gemmini shared-spad copy 路共用同一套 `enable / page_shift / ptbr / range / shared_base` 配置视图。
- runtime 在启动时一次性下发 `SPM_XLATE_CFG / RANGE`，后续 segment 只做 `FLUSH` 以刷新 shared-spad translation cache。
- CoupledDMA 当前复用同一份 page-table 上下文，但仍保持单 miss、独立 refill 的简化实现；这样不会改动旧配置的控制接口。
- Linux 上 alias VA window 允许 runtime 动态保留；但 PTW 使用的 PTE slab 仍必须有连续物理 backing，因为硬件访问模型固定为 `ptbr + vpn * 8`。

控制面由 Gemmini controller 的 `SPM_XLATE_CFG / RANGE / FLUSH / FAULT` 指令负责，当前锁定语义是：

- 命中已编程范围且 `enable=1`：访问走 shared-spad PTW/TLB。
- 命中已编程范围且 `enable=0`：访问直接透传到 shared scratchpad 物理地址，用于软件控制地关闭地址翻译。
- `FLUSH` 只清 fault 和 shared-spad translation cache，不清 `enable/range` 寄存器。

当前实现刻意保持简单：

- PTE 格式固定为 `bit0=valid`，高位为物理页号。
- shared-spad TLB miss 当前只允许一个 outstanding miss。
- 该模式只在 `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA` 这条目标配置上打开。

## 5. 尺寸不匹配统一规则

由于当前不支持 pooling 等层，上一层 output 和下一层 input 可能尺寸不匹配。
第一阶段固定规则为：

- 大变小：前缀裁剪
- 小变大：尾部零填充

具体语义是：

- 保留低地址起始的 `min(src_bytes, dst_bytes)` 字节
- 如果 `dst_bytes > src_bytes`，剩余高地址尾部全部补零

这条规则必须在三处保持完全一致：

- CPU golden 生成逻辑
- host Linux 上的 `pipeline-runtime`
- globalnoc Linux 目标执行入口

第一阶段不新增 YAML 字段显式记录这件事，由运行时和 golden 生成器根据 producer / consumer tensor byte size 自动应用。

## 6. 当前验证边界

当前已经闭环的内容：

- Host Linux 上的 bertmini `ours2 / gemini2 / tangram2` dummy-data correctness。
- `layers_gemmini.yaml + mapping_gemmini + entire_model canonical YAML` 的生成链。
- Linux overlay 的打包路径和 run script 入口。
- coupled-DMA globalnoc U280 bitstream 的本地构建链；可复用的 `firesim.tar.gz` 与 `built-hwdb` 入口已经生成。

当前尚未闭环的内容：

- RISC-V Linux target binary 的本机交叉编译，受限于当前环境缺少交叉工具链。
- globalnoc Linux FPGA replay，需要转到 AWS manager 执行；本地 bitstream 已成功生成，但 `built-hwdb` 里的 `file:///home/wzy/...` 路径不能直接在 AWS 复用，必须同步产物并重写路径，或在 AWS 重新 buildbitstream。
- 更贴近真实双 Gemmini 硬件资源约束的 canonical pipeline mapping，当前 `2_1024_16_19_64_*.yaml` 仍是 host-friendly 基线，不等价于最终硬件最优解。
