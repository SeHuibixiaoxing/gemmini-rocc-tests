# Pipeline Runtime Debug Records

这个目录专门存放 `pipeline-runtime` 每一轮调试记录。

约束：

- 每一轮调试必须新建一个单独文件，文件名使用 UTC 时间戳，例如：
  `20260413T163651Z.md`
- 不要复用旧文件追加新一轮内容
- 每个文件至少记录：
  - 本轮目标
  - 输入状态
  - 使用命令
  - 配置 / 环境
  - 证据路径
  - 新观察 / 新卡点
  - 分析
  - 下一步

关联规则：

- 若本轮有实际修改，必须同步新增：
  `../change_records/<timestamp>.md`
- `CURRENT_STATUS.md` 与 `NEXT_SESSION_PROMPT.md` 只保留最新 authoritative 摘要；
  这里才是每轮完整原始调试记录。
- 原始记录文件保持扁平时间戳命名；按调试类别的聚合索引见：
  `CATEGORY_INDEX.md`
