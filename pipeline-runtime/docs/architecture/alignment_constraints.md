# Pipeline Runtime Alignment Constraints

更新时间：`2026-04-13 16:36 UTC`

## 1. 基线

当前对齐基线固定为：

- `HybridMapper`：
  决定 runtime type、slot、page span、stage local view、artifact 字段
- `MudnacSim`：
  决定执行语义、SPM/DRAM 物理分配、transport 语义

runtime 只能在这个基线下实现，不应再发明一套独立 buffer 语义。

## 2. 当前范围

- 支持算子：
  `conv`、`resadd`
- 当前不覆盖：
  `pooling`
- tensor transport 语义以 `tensor_id` 为主键

## 3. 关键 buffer 语义

| 类型 | 基线执行语义 | runtime 必须满足 |
| --- | --- | --- |
| `DRAM` | 从 DRAM 装载到本地 SPM，再从本地 SPM 写回 DRAM | 允许显式 DRAM alias materialize |
| `DRAM_DEPEN` | 本地 SPM + DRAM 依赖地址语义 | 不能退化成纯 SPM ring |
| `ISOLATE_SPM` | stage local SPM，按 pair / send / recv 路径搬运 | 维持本地页语义 |
| `SHARED_SPM` | 多 stage 共享同一片上页集合 | 通过 shared binding / alias-group 兑现 |
| `ALL_RINGBUFFER` | pure ring transport，不额外持有 stage local storage | 不能在内部 transport 后无条件 export 回 model alias |

## 4. 用户已确认的补充规则

- 对 `conv + resadd` 范围，尺寸不匹配按约束中的 `max/min` 规则处理即可。
- DMA 拷贝只需要正确模拟对应大小的地址拷贝，不承担真实数据布局变换。
- `resadd` 的双输入大小由模型声明保证一致；runtime 不额外放宽或扩展语义。
- 当前不考虑 `pooling` 介入。
- 对同一 logical tensor，transport 只需要围绕 `tensor_id` 处理，不应被入口/出口 slot 表象误导。

## 5. 当前 runtime 仍需重点修正的地方

### 5.1 `DRAM_DEPEN`

当前主偏差不是“命名不同”，而是实现可能把它错误退化成 “本地页 + ring slot 页” 的 transport。

需要修正为：

- 仍以 DRAM 依赖地址为语义基底
- ring 只表达 transport / dependency，不替代 DRAM 语义本身

### 5.2 `ALL_RINGBUFFER`

当前主偏差是：

- 内部 transport 结束后，runtime 仍可能无条件 materialize 到 model alias

正确目标：

- 内部 pure ring transport 只在需要 publish 到真正外部 alias 时才 materialize
- 不允许把内部 transport tensor 一律当成外部 export

### 5.3 尺寸规则

当前 runtime 需要把尺寸不匹配处理显式写清楚：

- 只看编排给出的尺寸约束
- 对 copy 使用约束后的字节数
- 不做额外 layout transform 假设

## 6. 当前 artifact / HybridMapper 侧要求

- 搜索器可以继续复用动态规划段划分、模拟退火等高层搜索逻辑。
- 但 lowering / artifact 生成必须面向当前 Gemmini runtime API。
- artifact 必须显式给出：
  - stage local view
  - buffer type
  - slot count / pages per slot
  - tensor size / stride / page span
  - manager / SPM 需求

当前主线不要默认接受：

- 边界 tensor 被任意改成 `ALL_RINGBUFFER`
- 未支持算子的 artifact 混入当前 runtime

## 7. 文档与计划

- 机制说明：
  [`runtime_mechanisms.md`](runtime_mechanisms.md)
- runtime 修正计划：
  [`../plans/runtime_alignment_plan.md`](../plans/runtime_alignment_plan.md)
- HybridMapper 修正计划：
  [`../plans/hybridmapper_alignment_plan.md`](../plans/hybridmapper_alignment_plan.md)
