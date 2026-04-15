# Pipeline Runtime

`pipeline-runtime` 负责把 `HybridMapper` 导出的编排结果落到 `Gemmini + ReRoCC + CoupledDMA` 执行路径上。

当前主线不是抽象重写 runtime，而是：

- 对齐 `HybridMapper + MudnacSim` 语义
- 维持 `12-pair sbus128` dummy-model 固定 workflow
- 在低扰动观测条件下继续推进 `bertmini` Linux/F2 主线

## Start Here

- [`docs/project_guide.md`](docs/project_guide.md)
  总体指导、约束、关键机制、文档工作流
- [`docs/CURRENT_STATUS.md`](docs/CURRENT_STATUS.md)
  当前 authoritative 状态与下一步
- [`docs/constraints/hard_constraints.md`](docs/constraints/hard_constraints.md)
  必须遵守的执行、内存、日志、记录约束
- [`docs/workflows/pairdummy_sbus128.md`](docs/workflows/pairdummy_sbus128.md)
  当前 `pairdummy/sbus128` 固定执行流程
- [`TESTPLAN.md`](TESTPLAN.md)
  测试入口
- [`debug_records/README.md`](debug_records/README.md)
  调试记录规范
- [`change_records/README.md`](change_records/README.md)
  修改记录规范

## Static-First Debug

- 遇到卡点/报错，先做 artifact 静态审计和代码路径阅读，再读 breadcrumb / sparse capture，最后才开新的窄日志。
- artifact 审计入口：
  [`scripts/audit_pipeline_runtime_artifact.py`](scripts/audit_pipeline_runtime_artifact.py)
- capture 快速分诊入口：
  [`scripts/triage_prt_capture.py`](scripts/triage_prt_capture.py)
- rerun 前预检入口：
  [`scripts/pairdummy_sbus128_workflow.sh`](scripts/pairdummy_sbus128_workflow.sh)
  的 `debug-preflight`
- 新 blocker 排查 SOP 计划：
  [`docs/plans/blocker_debug_sop_plan.md`](docs/plans/blocker_debug_sop_plan.md)
- pointwise / Gemmini 区间仍优先依赖 breadcrumb，不回到“先堆热路径文本日志再解释”的旧顺序。
- 若 breadcrumb / sparse 仍不够细，优先用 trigger-gated 短日志，
  只在命中目标 stage / manager / tensor / page / token 后再写极短行。

## Mainline

- 当前固定 profile：
  [`scripts/pairdummy_sbus128_fixed_env.sh`](scripts/pairdummy_sbus128_fixed_env.sh)
- 当前固定 workflow：
  [`scripts/pairdummy_sbus128_workflow.sh`](scripts/pairdummy_sbus128_workflow.sh)
- 当前固定 runbook：
  [`docs/workflows/pairdummy_sbus128.md`](docs/workflows/pairdummy_sbus128.md)

历史长时间线、旧状态叙事和被 supersede 的 dated 文档已归档到
[`docs/archive/reorg_20260413/`](docs/archive/reorg_20260413/)。
