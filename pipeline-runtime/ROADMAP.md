# Pipeline Runtime Roadmap

详细路线图已迁到 [`docs/plans/roadmap.md`](docs/plans/roadmap.md)。

当前优先级只有三件事：

1. 维持 `pairdummy/sbus128` 固定 workflow、freshness 闭环和低扰动观测。
2. 对齐 `HybridMapper + MudnacSim` 语义，先覆盖 `conv + resadd`。
3. 继续推进 `bertmini` dummy-model Linux/F2 主线，确认当前 late pointwise / Gemmini 同步区域的真实 blocker。
