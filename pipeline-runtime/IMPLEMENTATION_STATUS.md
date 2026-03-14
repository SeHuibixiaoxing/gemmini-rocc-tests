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
- suite wrapper 的 false-stall 已修复；latest fresh suite rerun 也已 PASS：`/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260312-215040`，其中 matrix / coverage / nonblocking 三项全部通过。
- 2026-03-11 已再次按 `2026-03-08` 文档参数重放 matrix / coverage / nonblocking 三项 baseline，三项全部 PASS，确认“之前 baremetal 能过”的结论仍成立；此前出现差异是因为后续 quick workload 参数漂移，而不是该 baseline 回退。
- 当前新的 metasim 经验是：DRAM command trace 必须保持编译期默认关闭；nonblocking quick 默认值固定为 `512 / 4 / 1 / 32 / 4 / 1`，并把 `conv` warmup 放到 timed phase 之外，避免把 slow-progress 误判成 DMA stall。
- 已增加 shared scratchpad page-table 模式的硬件/软件基线：默认仍保持 legacy 直映行为，只在目标 globalnoc coupled-DMA 配置上打开；命中范围且 `enable=1` 走 shared-spad PTW/TLB，`enable=0` 做 shared-spad 物理地址透传；当前锁定为单个 outstanding miss，PTE 格式为 `bit0=valid` + 物理页号。
- 2026-03-12 已定位并修复 shared-spad PTW page-size 根因：`FrontendTLB` 不能把软件编程的 `spm_xlate_page_shift` 钳到 Rocket `pgIdxBits`。修复后 current `matrix` 的 `single/full` 手工 VFireSim 回归都已 PASS，current `coverage` 拿到 `CASE_RESULT shared_mv_xlate PASS`、`CASE_RESULT shared_mv_passthrough PASS` 与 `COVERAGE_SUMMARY ... case4b=1 case4c=1 ...`，current `nonblocking` 也重新拿到 `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`。
- 2026-03-12 已开始落地 shared-spad xlate context 的第二阶段：CoupledDMA 接入与 Gemmini controller 共用的 shared-spad xlate sideband，runtime 切换为启动时 bootstrap `SPM_XLATE_CFG / RANGE`、segment 内只 `FLUSH`；`gemmini` / `chipyard` 的 Scala 编译和 `pipeline_runtime` 的 host C 编译均已通过。
- 同一轮 fresh baremetal metasim suite 已完成 PASS：suite root `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/metasim-baremetal-suite-20260312-215040`；matrix `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--14-21-47-runworkload-DAEH4MMZT1C4SVVG.log`、coverage `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--15-02-57-runworkload-7D5OF4V6I0PVAU78.log`、nonblocking `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--15-34-11-runworkload-F7R3E8P02TZC15TX.log` 全部拿到 `ALL_TESTS_PASS`。
- 2026-03-12 本地 `firesim buildbitstream` 也已成功：log 为 `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--16-55-53-buildbitstream-2NLSKFJM4VHB1EFW.log`；产物入口为 `/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/built-hwdb-entries/alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10` 和对应 `results-build/.../firesim.tar.gz`。因此当前下一步已切换为“在 AWS 上消费或重建这份 bitstream，并继续 globalnoc Linux replay”，而不是“等待 FPGA 恢复”。
