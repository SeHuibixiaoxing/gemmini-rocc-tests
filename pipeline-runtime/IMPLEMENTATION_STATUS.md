# IMPLEMENTATION_STATUS

此文件保留为兼容入口，不再承载完整状态叙事。

当前应优先查看：

- 当前状态与下一步：`HANDOFF.md`
- 锁定决策：`DECISIONS.md`
- 验证入口与通过标准：`TESTPLAN.md`
- 历史时间线与旧 run/log：`docs/archive/2026Q1_history.md`

当前摘要：

- host `pipeline_runtime` 的 bertmini dummy-data 闭环仍为 PASS。
- globalnoc baremetal metasim 的 startup stall 已修复；matrix isolated PASS、coverage isolated PASS。
- globalnoc baremetal metasim 的 nonblocking 已完成 fresh isolated PASS：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-nonblocking-warmup-quick-20260308-161745`，日志显示 `warmup_start -> warmup_done -> 4 个 SCENARIO_RESULT -> NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1 -> ALL_TESTS_PASS`。
- suite wrapper 的 false-stall 已修复；fresh suite rerun 也已 PASS：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260308-163933`，其中 matrix / coverage / nonblocking 三项全部通过。
- 当前新的 metasim 经验是：DRAM command trace 必须保持编译期默认关闭；nonblocking quick 默认值固定为 `512 / 4 / 1 / 32 / 4 / 1`，并把 `conv` warmup 放到 timed phase 之外，避免把 slow-progress 误判成 DMA stall。
