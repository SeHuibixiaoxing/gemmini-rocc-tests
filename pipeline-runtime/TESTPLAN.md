# Pipeline Runtime Test Plan

详细测试计划已迁到 [`docs/testing/test_plan.md`](docs/testing/test_plan.md)。

当前测试入口分三层：

1. 静态语义校验：对照 `HybridMapper + MudnacSim` 与 runtime 当前实现。
2. 构建与 workflow 校验：本地 build、image freshness、remote freshness、固定脚本链路。
3. 动态主线校验：`12-pair sbus128` dummy-model 上运行 `bertmini`，以 guest 文件日志和 breadcrumb 为主观测面。
