# Runtime Alignment Plan

更新时间：`2026-04-13 16:36 UTC`

## 1. 目标

让当前 `pipeline-runtime` 在不改变主硬件接口前提下，对齐
`HybridMapper + MudnacSim`
已经确认的 buffer 与 transport 语义。

## 2. 范围与默认值

- 只覆盖 `conv + resadd`
- 不考虑 `pooling`
- transport 以 `tensor_id` 为主键
- 尺寸不匹配按编排约束里的 `max/min` 规则处理
- DMA 只需要完成正确字节数的地址拷贝，不承担布局重排

## 3. 当前主要差距

### 3.1 `DRAM_DEPEN`

- 需要恢复成 “本地 SPM + DRAM 依赖地址” 语义
- 不允许继续退化成 “SPM ring”

### 3.2 `ALL_RINGBUFFER`

- 需要区分内部 pure ring transport
  与真正对外 alias export
- 不允许对内部 transport tensor 一律 materialize

### 3.3 尺寸与 copy

- 需要把当前 `conv + resadd` 的尺寸规则明确落到 runtime：
  - 取约束后的字节数
  - 只做大小正确的 copy
  - 不做额外布局变换

## 4. 实施顺序

1. 先在 runtime 内增加 artifact / buffer type 静态校验
2. 修正 entry/export transport 分支
3. 修正 alias materialize 条件
4. 修正尺寸和 copy 规则
5. 用 bertmini artifact + current hardware 做静态与动态回归

## 5. 验收

- 静态对照 `HybridMapper + MudnacSim` 时不再出现上述语义偏差
- current hardware fixed workflow 下，不因修正重新退回旧 blocker
- `bertmini` 当前 artifact 在 runtime 中能以一致语义运行
