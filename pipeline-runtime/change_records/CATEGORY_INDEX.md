# Pipeline Runtime Change Record Categories

更新时间：`2026-05-04 10:13 UTC`

说明：

- 原始记录文件仍保持扁平的 UTC 时间戳命名；本索引只负责按调试类别聚合。
- 分类规则由 `scripts/generate_record_category_index.py` 生成。
- 当前固定三类：`main`、`tracev` 与 `gdbserver`；新增记录按规则自动归类。

## 类别说明

- `main`：主线 `pairdummy/sbus128` / `pipeline-runtime` 语义、workflow、artifact、DMA/Gemmini/RR/SPM-xlate 调试（当前 change record 数量：`72`）
- `tracev`：`TraceV` / `TracerV` / `workerpc` / `metasim` / marker / tracefile 调试（当前 change record 数量：`21`）
- `gdbserver`：`gdbserver` / NIC 网络联通 / host-guest attach / 最小 Linux smoke 调试（当前 change record 数量：`55`）

## 按类别索引

### `main`

- `20260413T163651Z.md`：- 落地 `pipeline-runtime` 文档体系重构计划。
- `20260414T033537Z.md`：- 修正静态审计里已经确认的 runtime 语义偏差。
- `20260414T034734Z.md`：- 给 late pointwise / Gemmini 区间补上低扰动 breadcrumb 边界。
- `20260414T035557Z.md`：- 把更快的 static-first debug 方法固化成脚本和文档。
- `20260414T050436Z.md`：- 对新的 fixed-load blocker 加最小条件探针，不回退到大范围热路径文本日志。
- `20260414T060823Z.md`：- 把新的 `tok=726` 稳定前沿与已证伪假设同步进主文档。
- `20260414T063021Z.md`：- 把 `set_src` 前的默认 probe 从高扰动 monitor 指令改成低扰动 ReRoCC/manager 快照。
- `20260414T072835Z.md`：- 把 fixed-load probe 进一步缩到 `page31`，
- `20260414T090443Z.md`：- 把 fixed profile 从已经过时的 fixed-load `page31` probe
- `20260414T093712Z.md`：- 把用户刚强调的 Linux boot 等待硬约束写进 authoritative 文档。
- `20260414T094819Z.md`：- 把 active rerun 已经越过旧 blocker 的事实同步进 authoritative 文档。
- `20260414T103114Z.md`：- `src/prt_dma.c`
- `20260414T125526Z.md`：- 修正当前 breadcrumb observability，
- `20260414T132728Z.md`：- 降低 pointwise 热路径的 coarse guest-log 扰动
- `20260414T144739Z.md`：- 落地新的 blocker-debug SOP
- `20260414T153308Z.md`：- 修正 `triage_prt_capture.py` 对 export capture 的 frontier 误判
- `20260414T160025Z.md`：- 本轮在 `page24` trigger rerun 启动后，静态确认新的观测缺口：
- `20260414T161120Z.md`：- `2026-04-14 15:55 UTC`
- `20260414T165254Z.md`：- 最新一轮
- `20260415T045655Z.md`：- File changed:
- `20260415T051756Z.md`：- File changed:
- `20260415T055238Z.md`：- Files changed:
- `20260415T085413Z.md`：- Observability-only rerun configuration update
- `20260415T091830Z.md`：- observability-only rerun preparation
- `20260415T094220Z.md`：- observability-only rerun preparation
- `20260415T102021Z.md`：- Observability-only rerun control.
- `20260415T104944Z.md`：在 pairdummy sbus128 dummy-model 主线上补齐“breadcrumb-only 低扰动 rerun”所需的 host 侧 overlay 能力，并把该机制写入 workflow / observability 文
- `20260415T105118Z.md`：修复 pairdummy sbus128 fixed workflow 中的一个 host 配置传播缺陷：
- `20260415T131934Z.md`：为 `12-pair sbus128 / bertmini batch8` 当前已收紧到的
- `20260415T132611Z.md`：修复 pairdummy guest env 的 source-of-truth 漂移，
- `20260415T135008Z.md`：- Observability-only config retarget for the next control rerun.
- `20260415T141944Z.md`：- Restore the fixed `pairdummy sbus128` guest profile from the temporary
- `20260415T144925Z.md`：- Restore the `pairdummy sbus128` fixed profile to a truly low-perturbation baseline.
- `20260416T023615Z.md`：- Type: observability-only
- `20260416T030058Z.md`：- Type: observability-only refinement
- `20260417T052141Z.md`：- 让 `triage_prt_capture.py` 能把当前最新 blocker
- `20260417T061050Z.md`：- 修复固定 workflow 下 `image-closure` 的本地构建阻塞，
- `20260419T084021Z.md`：- 给 `debug_records/` 与 `change_records/` 增加按调试类别聚合的索引。
- `20260419T085031Z.md`：- 归档本轮新的 `pipeline-runtime` 静态调试结论。
- `20260419T092816Z.md`：- 把本轮关于 `rr_release` / `busy` 风险的静态结论落实为最小代码修正。
- `20260419T103900Z.md`：- 新增 pair-manager baremetal probe：
- `20260419T123800Z.md`：调试类别：infra
- `20260419T142820Z.md`：- 文件：
- `20260419T145837Z.md`：- 生成一版只使用 `6` 个 Gemmini/DMA pair-manager 槽位的 `bertmini` pipeline-runtime artifact。
- `20260420T065636Z.md`：- 把 `2026-04-20 g6 bertmini` 这轮 FPGA 复盘的 authoritative 结论写回主文档，
- `20260420T071816Z.md`：- 修正上一轮把 `globalStageId` 误当成 `rr_cfg_id_for_stage()` 输入的错误文档结论。
- `20260420T072430Z.md`：- 修正 `firesim-prt-host-watchdog.sh`
- `20260420T072759Z.md`：- 对已落地的 `host-watchdog` 修补做静态收尾，
- `20260420T080618Z.md`：- 把 `g6 bertmini` 最新 FPGA 复跑结论写回主状态文档和会话提示。
- `20260420T101447Z.md`：- 缩窄
- `20260420T105238Z.md`：- 落盘本轮
- `20260420T111029Z.md`：- 落盘本轮
- `20260420T113701Z.md`：- 增加一次性 worker 绑核观测点
- `20260420T121216Z.md`：- 修正 `pipeline-runtime` 顶部状态文档中已经过期的
- `20260420T134740Z.md`：- 把用户刚澄清的“仅限 `pipeline-runtime` 软件栈”的
- `20260420T140451Z.md`：- 修正文档里对
- `20260420T145630Z.md`：本轮只更新 `pipeline-runtime` 主线文档与记录，把 `2026-04-20 14:16 UTC` fresh rerun 的 authoritative 结论写回仓库：当前首个冻结点已早于 `page29 before_v
- `20260420T154255Z.md`：本轮不改运行时代码，只修正文档里对 `g6 bertmini` fresh rerun breadcrumb 证据强度的表述，并新增一份专门的静态审计记录。
- `20260421T005834Z.md`：本轮新增一份面向阅读者的解释文档，把当前 `g6 bertmini` export frontier 的调试结论和推理过程展开说明，并在状态文档中加入入口。
- `20260421T011522Z.md`：本轮为 `pipeline-runtime` 主线调试补了一条新的低扰动 breadcrumb 观测链路，用来绕开当前 `page29/token0 -> slot14` 的 torn/collision 歧义。
- `20260421T015501Z.md`：本轮没有新增运行时代码；主要变更是执行了一轮新的 FireSim 主线 rerun，并把新的 stall frontier、现场抓取和结论沉淀为记录。
- `20260421T055946Z.md`：本轮目标是把当前已经收敛的 `pairdummy-sbus128 tracerv-inst` FPGA 卡死轮次尽快回收，并把
- `20260421T070147Z.md`：本轮改动是围绕 `pipeline-runtime` 主线的 `gdbserver` 实验收敛做的三件事：
- `20260421T081019Z.md`：本轮改动围绕 `pipeline-runtime` 的 `gdbserver` 路径做了三组工作：
- `20260421T084714Z.md`：本轮改动不是继续堆 guest 日志，而是把“当前 gdbserver 路径为什么一定不可能成功”固化成静态前置检查。
- `20260421T091200Z.md`：本轮开始为 `pairdummy 4c12p12 sbus128` 主线补 NIC-enabled bitstream 构建入口。
- `20260421T122600Z.md`：本轮没有新增运行时代码；主要修改是把这轮 `cfg32 + no-TraceV` 主线 rerun 的现场与 authoritative 结论写回仓库文档。
- `20260421T125757Z.md`：本轮为验证 “当前主线 stall 是否发生在 bounce path” 增加了一组**仅在命中 bounce path 时才生效**的软件旁路，并完成了一轮基于该旁路的 `g6 bertmini` FireSim rerun；随后把实验结
- `20260424T030134Z.md`：本轮改动只做一件事：
- `20260424T033134Z.md`：本轮改动聚焦一个硬件 bridge 语义修复：
- `20260424T035022Z.md`：- 为 NIC/SimpleNIC 调试补一个足够小、成本足够低的 FireSim local metasim 配置：
- `20260427T053456Z.md`：让 `pipeline-runtime` 在 cfg32_nic 调试路径中强制走 direct DMA 软件路线，并补齐正式 batch local-GDB workload。

### `tracev`

- `20260417T093635Z.md`：- 为本轮 `TracerV` bring-up 补齐缺失的修改记录。
- `20260417T094223Z.md`：- 修复 instruction-trigger `TracerV` workload 在 FireMarshal `image-closure` 阶段的构镜失败。
- `20260417T101356Z.md`：对专用 debug workload
- `20260417T103725Z.md`：将 isolated `tracerv-inst` 调试路径从 shell 级 trigger 切换为 runtime worker 本地 trigger：
- `20260417T111041Z.md`：本轮修改分成两部分：
- `20260417T114331Z.md`：- 修正当前 `TracerV` live 调试链的一个关键观测盲区：
- `20260417T122948Z.md`：- 在不改 guest/runtime 业务语义的前提下，
- `20260417T125321Z.md`：- 基于当前 live `tracerv-inst` 证据，
- `20260417T135048Z.md`：- 在不改变 pipeline-runtime 主语义的前提下，
- `20260417T140407Z.md`：- 让 `workerpc` 路径里的 delayed `/proc` + `ptrace` probe 在 live hang 期间更可靠地落到 guest image。
- `20260417T143700Z.md`：- 让 `workerpc` 路径在再次 freeze 到 `dma-export-host first-chunk-submitwait-end` 之前，
- `20260418T000500Z.md`：- 继续调试 `TracerV`，但不启动新机器。
- `20260418T162340Z.md`：- 为 `TracerV` 新增一条独立于 `pairdummy/sbus128` 主线的
- `20260418T181442Z.md`：- 给 `rerocc_lc_export_dma_bertmini_segment3_repro.c`
- `20260419T032347Z.md`：- 为 boot ROM 唤醒链最后一步增加 host-side 精准诊断，
- `20260419T033833Z.md`：- 新增一份 live debug record，固化 bootdiag + TracerV + gdb 的直接证据：
- `20260419T060641Z.md`：- 为下一轮本机 metasim 准备一个更适合长窗口 TraceV 的 runtime config。
- `20260419T075152Z.md`：- 把本轮 `TraceV local metasim` 的根因结论和执行约束写回仓库文档。
- `20260419T081124Z.md`：- 为 `selector=3` 增加一条最小、已知可跑通链路的 marker 验证入口。
- `20260419T082820Z.md`：- 新增一份稳定的 `TracerV` 接入与 bring-up SOP。
- `20260421T035237Z.md`：本轮目标是把 `segment3 stage0 tensor6 export DMA/RR` 的 `TraceV selector=3`

### `gdbserver`

- `20260424T042055Z.md`：- 为 `pipeline-runtime` 主线准备一条更小的 `Linux + NIC + gdbserver` FPGA 调试路径，
- `20260424T092231Z.md`：- 实现一版低扰动的 NIC host-side 失败现场抓取。
- `20260424T102452Z.md`：- 撤回上一轮依赖新 bitstream 的 target-side NIC 调试寄存器方案。
- `20260424T104706Z.md`：- 不改主代码逻辑，先把当前 `SimpleNIC / NIC / gdbserver` 调试线的关键静态结论补成可复用文档。
- `20260424T110841Z.md`：- 把本轮 `singlecore + NIC` bitstream 构建期间新增确认的静态事实固化到记录中。
- `20260424T133954Z.md`：- 记录这轮新 AGFI FPGA 验证的结果。
- `20260424T142013Z.md`：- 为 `SimpleNIC` / `gdbserver` 最小 NIC smoke 增加一组低扰动、可综合、失败时一次性读取的观测点。
- `20260425T015051Z.md`：- 把 `singlecore + NIC + debugregs` 新 bitstream 真正接到 FireSim FPGA 验证链路上。
- `20260425T021741Z.md`：- 把本轮 `SimpleNIC/NIC/gdbserver` 静态审计的新结论落盘。
- `20260425T031328Z.md`：- 落地上一轮 `SimpleNIC/NIC/gdbserver` 静态审计后的修复计划。
- `20260425T032850Z.md`：- 记录本轮继续执行 no-Trace NIC bitstream 构建的操作与早期验证结果。
- `20260425T061316Z.md`：- 记录 no-Trace NIC bitstream 完成后的真实 FPGA smoke 结果。
- `20260425T073040Z.md`：- 为 NIC/gdbserver 早期 `actually wrote 0 bytes` 失败加入更深的可综合观测。
- `20260425T105022Z.md`：- 修复本轮静态展开发现的 SimpleNIC target-cycle ready 组合依赖。
- `20260425T133616Z.md`：- 修复 SimpleNIC Bridge 中残留的 HostPort 双向 lockstep 假设。
- `20260425T164540Z.md`：- 把新 direction-fire SimpleNIC AGFI 接到 no-Trace NIC gdbserver smoke 验证链路。
- `20260425T175315Z.md`：- 记录本轮结合 FireSim / Chipyard 文档得到的 NIC bridge 静态结论。
- `20260426T020500Z.md`：- 修复 `SimpleNICBridgeModule` channelized leaf channel 仍可能部分 fire 的同步漏洞。
- `20260426T052258Z.md`：- 文件：`generators/firechip/goldengateimplementations/src/main/scala/SimpleNICBridge.scala`
- `20260426T063000Z.md`：- 补齐上一轮 `runworkload` 因 manager SSH 异常而没有完整 copyback 的现场。
- `20260426T075700Z.md`：静态排查 `build_strategy: TIMING` 是否真的进入 AWS F2 bitstream build。
- `20260426T170527Z.md`：- `../debug_records/20260426T170527Z.md`
- `20260427T042738Z.md`：- `../debug_records/20260427T042738Z.md`
- `20260427T135411Z.md`：- `../debug_records/20260427T135411Z.md`
- `20260427T152916Z.md`：- `../debug_records/20260427T152916Z.md`
- `20260428T032140Z.md`：- `../debug_records/20260428T032140Z.md`
- `20260429T095028Z.md`：- `../debug_records/20260429T095028Z.md`
- `20260429T142217Z.md`：- `../debug_records/20260429T142217Z.md`
- `20260429T145226Z.md`：- `../debug_records/20260429T145226Z.md`
- `20260429T221723Z.md`：修复 F2 host driver 对 CPU-managed stream BAR4 的错误访问模式。当前 BAR4 被映射成 write-combining，但 CPU-managed stream 是 FIFO/破坏性读写语义，不能按
- `20260429T230748Z.md`：类别：`gdbserver` / NIC / FireSim switch
- `20260429T235921Z.md`：日期：2026-04-29T23:59:21Z
- `20260430T162900Z.md`：- `../debug_records/20260430T162000Z.md`
- `20260501T023200Z.md`：日期：2026-05-01T02:32:00Z
- `20260501T045108Z.md`：- `../debug_records/20260501T045108Z.md`
- `20260501T141954Z.md`：日期：2026-05-01T14:19:54Z
- `20260501T161752Z.md`：日期：2026-05-01T16:17:52Z
- `20260502T164947Z.md`：日期：2026-05-02T16:49:47Z
- `20260502T223837Z.md`：日期：2026-05-02T22:38:37Z
- `20260502T233311Z.md`：日期：2026-05-02T23:33:11Z
- `20260502T234445Z.md`：日期：2026-05-02T23:44:45Z
- `20260503T032845Z.md`：日期：2026-05-03T03:28:45Z
- `20260503T035648Z.md`：日期：2026-05-03T03:56:48Z
- `20260503T051319Z.md`：日期：2026-05-03T05:13:19Z
- `20260503T085203Z.md`：调试类别：FireSim / resource-cleanup / constraints
- `20260503T095010Z.md`：日期：2026-05-03T09:50:10Z
- `20260503T140301Z.md`：日期：2026-05-03T14:03:01Z
- `20260503T174646Z.md`：日期：2026-05-03T17:46:46Z
- `20260503T191457Z.md`：日期：2026-05-03T19:14:57Z
- `20260503T232710Z.md`：类别：`gdbserver` / NIC / FireSim CPU-managed stream
- `20260504T022833Z.md`：- `sims/firesim/sim/midas/src/main/scala/midas/core/CPUManagedStreamEngine.scala`
- `20260504T060924Z.md`：Fix the single-core NIC bridge boot-before-Linux deadlock seen on
- `20260504T063838Z.md`：Add a deeper synthesizable SimpleNIC observation surface before any further
- `20260504T073301Z.md`：Add one more layer of synthesizable SimpleNIC observation before any next
- `20260504T101241Z.md`：类别：`gdbserver`
