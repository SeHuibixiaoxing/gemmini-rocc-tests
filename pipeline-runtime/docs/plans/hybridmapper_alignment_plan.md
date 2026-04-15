# HybridMapper Alignment Plan

更新时间：`2026-04-13 16:36 UTC`

## 1. 目标

让 `HybridMapper` 输出的 pipeline runtime artifact
既保持原有高层搜索能力，又能与当前
`Gemmini + ReRoCC + DMA`
runtime API 和语义对齐。

## 2. 可复用部分

以下能力继续复用：

- 段划分 / 动态规划
- 模拟退火或其他高层搜索
- 多核划分与资源估算框架

## 3. 必须改造的部分

### 3.1 Backend lowering

- artifact emitter 必须面向当前 runtime API
- 只生成 `conv + resadd` 当前支持范围
- 不把未支持算子混入当前主线 artifact

### 3.2 Buffer / transport 语义

- 显式表达 `DRAM_DEPEN`
- 显式表达 `ALL_RINGBUFFER`
- 不默认允许无效边界 tensor 语义
- 继续以 `tensor_id` 作为主 transport 键

### 3.3 硬件自适应

- 不把 manager 数量、SPM 页数、pair 数量写死在 emitter
- 应从当前目标硬件配置导出：
  - core / Gemmini / DMA 数量
  - SPM page span
  - slot / ring / stage view 需求

### 3.4 尺寸规则

- 对当前 `conv + resadd` 范围：
  - 输出 artifact 时显式写入编排决定的尺寸信息
  - runtime 现场可重算并与 artifact 做交叉校验

## 4. 测试

- 静态：
  - artifact audit
  - buffer type 覆盖统计
  - runtime / hardware 配置匹配
- 动态：
  - 用当前 `12-pair sbus128` 硬件配置重新生成 bertmini artifact
  - 在 fixed workflow 上运行 runtime

## 5. 验收

- 当前 bertmini artifact 在 runtime 中不再依赖 ad hoc 特判
- artifact 能适应不同硬件配置而不是写死当前 `12/12` 资源
- buffer type 与尺寸规则在静态审计中可直接读出
