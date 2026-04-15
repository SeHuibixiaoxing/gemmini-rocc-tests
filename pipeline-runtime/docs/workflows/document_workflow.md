# Pipeline Runtime Document Workflow

## 1. 目录职责

- `docs/project_guide.md`
  总纲，只写当前有效目标、机制、约束、进度、文档地图
- `docs/architecture/`
  稳定机制与语义，只在架构或语义变更时更新
- `docs/constraints/`
  强约束与踩坑经验
- `docs/workflows/`
  固定执行流程与文档维护流程
- `docs/testing/`
  测试策略、日志、watchdog、breadcrumb、观测方法
- `docs/plans/`
  尚未完成的改造计划
- `docs/reference/`
  守则、工具、辅助参考
- `docs/archive/`
  superseded 文档与旧时间线
- `debug_records/`
  每轮调试记录
- `change_records/`
  每轮修改记录

## 2. 何时更新

- 纯调试：
  更新 `debug_records/`，必要时同步 `CURRENT_STATUS.md` 与 `NEXT_SESSION_PROMPT.md`
- 有实际修改：
  同时新增 `change_records/`
- 架构或语义结论变化：
  再更新 `docs/architecture/`
- 新增硬约束或稳定教训：
  更新 `docs/constraints/`
- 计划变化：
  更新 `docs/plans/`
- debug SOP 或 probe 策略变化：
  同步更新 `docs/testing/observability.md`
  与对应 `docs/plans/*.md`

## 3. 不允许的做法

- 不要把 run-by-run 历史继续堆回 `README.md`、`CURRENT_STATUS.md`、`NEXT_SESSION_PROMPT.md`
- 不要把 archive 文档重新当成 authoritative 入口
- 不要在没有结构变化时频繁重写 `docs/architecture/`

## 4. 记录模板

- debug record 必须至少包含：
  - 目标
  - 输入状态
  - 命令
  - 配置
  - 证据路径
  - 新观察 / 新卡点
  - 分析
  - 下一步
  - 若本轮涉及 frontier claim：
    `claim_class / disturbance_risk / requires_control_rerun`
- change record 必须至少包含：
  - 目标
  - 相关 debug record
  - 修改文件
  - 修改动机
  - 语义影响
  - 验证
