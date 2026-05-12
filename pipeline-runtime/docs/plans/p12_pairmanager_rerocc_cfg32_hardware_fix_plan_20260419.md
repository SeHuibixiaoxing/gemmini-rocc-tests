# P12 Pair-Manager ReRoCC CFG32 硬件修复计划

更新时间：`2026-04-19 UTC`

## 目标

- 目标配置固定为
  `GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`。
- 只讨论 **硬件修复方案**：把 ReRoCC 可用 `cfg` 槽位上限从 `16`
  提高到 `32`。
- 同时补一套 **极小 metasim 验证负载计划**，用于证明硬件修复后：
  - `cfg15` 仍保持兼容；
  - `cfg16` 可以被真实选中并参与指令路由；
  - `cfg31` 可以被真实选中并参与指令路由。
- 本文不展开 pipeline runtime 的正式迁移，只把它当作后续软件适配事项记录。

## 为什么要做这个修复

- 当前 ReRoCC 硬件侧把 `cfg` 数量上限硬编码为 `16`：
  - `generators/rerocc/src/main/scala/client/CSRs.scala`
  - `generators/rerocc/src/main/scala/client/Client.scala`
- 这会带来两个直接限制：
  - `rrcfg` CSR 只枚举到 `rrcfg15`；
  - `rropc0..3` 与 `rrbar` 中携带的 `cfg` 选择子只有 `4 bit`。
- 对 P12 pair-manager 系统来说，软件已经出现“普通 stage cfg 槽位”和
  “保留给特殊用途的 cfg 槽位”同时竞争 `16` 个槽位的问题。把硬件上限提升到
  `32`，是解决这类冲突的最直接底座修复。

## 当前硬件约束点

### 1. CSR 总量硬编码为 16

- `generators/rerocc/src/main/scala/client/CSRs.scala`
  当前定义：
  - `MAX_CFGS = 16`
  - `rropc0..3` 宽度为 `log2Ceil(MAX_CFGS)`，也就是 `4`
  - `rrbar` 宽度同样为 `4`
  - `rrcfg0..15` 被枚举到 `0x810..0x81f`

### 2. Client 参数也限制在 16

- `generators/rerocc/src/main/scala/client/Client.scala`
  当前定义：
  - `ReRoCCClientParams(nCfgs: Int = 16, ...)`
  - `require(nCfgs <= 16)`

### 3. 软件侧已有大量“16 槽位”假设

- 这不是本文要修的主范围，但必须作为硬件变更的兼容性背景记录下来：
  - `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c`
  - `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_control.h`
  - `generators/rerocc/tests/rerocc.h`
  - `generators/gemmini/software/gemmini-rocc-tests/include/gemmini.h`
- 结论：**硬件先扩到 32 是必要条件，但不是全部工作**。不过本计划只覆盖硬件修复，
  软件只为测试负载提供最小补丁入口。

## 硬件修复范围

## 方案总览

- 核心策略不是“到处把 `16` 改成 `32`”，而是明确完成下面四件事：
  - ReRoCC CSR 空间正式扩展到 `32` 个 `cfg` 槽位；
  - `rropc0..3` 与 `rrbar` 的 `cfg` 选择子宽度从 `4 bit` 提升到 `5 bit`；
  - `ReRoCCClientParams.nCfgs` 的默认值与上限同步提升到 `32`；
  - 保持 `cfg0..15` 的地址与语义完全不变，确保已有软件最小化兼容。

### 1. 扩展 CSR 枚举：新增 `rrcfg16..31`

- 修改文件：
  `generators/rerocc/src/main/scala/client/CSRs.scala`
- 计划修改：
  - `MAX_CFGS: 16 -> 32`
  - 保留现有：
    - `rrcfg0..15` 对应 `0x810..0x81f`
  - 新增：
    - `rrcfg16..31` 对应 `0x820..0x82f`
- 这样做的原因：
  - 低 16 个槽位地址不变，老软件不会因为地址漂移失效；
  - 高 16 个槽位被放入紧邻区间，便于软件头文件和调试工具按线性规则生成；
  - 当前仓库扫描没有看到 ReRoCC 自身占用 `0x820..0x82f`，作为扩展段比较自然。

### 2. 扩展 `rropc` / `rrbar` 的选择子宽度

- 修改文件：
  `generators/rerocc/src/main/scala/client/CSRs.scala`
- 计划修改：
  - `rropc0..3` 的位宽从 `log2Ceil(16)=4` 提升到 `log2Ceil(32)=5`
  - `rrbar` 位宽同样提升到 `5`
- 为什么必须做：
  - 如果只新增 `rrcfg16..31`，但 `rropc` 仍然只能写入 `4 bit`，
    那么软件写入 `cfg16` 会被截断为 `cfg0`，`cfg31` 会被截断为 `cfg15`；
  - 这类 bug 会表现成“高位 cfg 看起来存在，但指令实际打到了错误槽位”，
    是最危险的伪修复。

### 3. 放宽 Client 参数上限

- 修改文件：
  `generators/rerocc/src/main/scala/client/Client.scala`
- 计划修改：
  - `ReRoCCClientParams(nCfgs: Int = 16)` 调整为 `32`
  - `require(nCfgs <= 16)` 调整为 `require(nCfgs <= 32)`
- 为什么必须做：
  - 否则即使 CSR 容量被扩展，外围实例化参数仍然会卡死在 `16`；
  - 这会造成“定义层支持 32，但实例化层仍只允许 16”的不一致。

### 4. 逐点检查宽度是否贯穿到 Client/Manager 路径

- 重点不是大改结构，而是做一轮有目标的 **宽度传递审计**。
- 审计点：
  - `ReRoCCBundleParams` 中是否有依赖 `nCfgs` 的字段宽度；
  - `client_id` / `cfg_id` 相关 bundle 在 client -> manager 之间是否有显式截断；
  - `LazyRoCC(roccCSRs=...)` 暴露的 CSR 列表是否自动跟随新枚举；
  - pair-manager 或 manager 侧是否存在“假设 cfg 编号最多 15”的比较逻辑。
- 期望结果：
  - `cfg_id=16` 与 `cfg_id=31` 能一路从 CSR decode、路由配置、到 manager
    观察点都保持真实值，不发生 truncation。

## 明确不在本次硬件修复范围内

- 不在本轮内正式重写 pipeline runtime 的 `cfg` 分配策略。
- 不在本轮内统一修改所有 Linux / baremetal / runtime 头文件到 `32`。
- 不在本轮内重新定义“`cfg15` 是否继续保留给特殊用途”的软件策略。
- 不在本轮内做大规模 API 兼容层设计。

## 与现有软件 ABI 的兼容性要求

### 必须保持兼容的部分

- `cfg0..15` 地址不变：
  - 仍为 `0x810..0x81f`
- 老软件若只使用 `cfg0..15`：
  - 行为应与修复前完全一致
- `rropc` 的低 4 bit 编码语义不变：
  - 旧值写入后应选择同一个低位 `cfg`

### 新增能力

- 软件可额外访问：
  - `rrcfg16..31`
- 软件可通过 `rropc0..3` / `rrbar` 选择：
  - `cfg16`
  - `cfg31`

## 硬件实现检查清单

### A. CSR decode 检查

- 确认 `0x820..0x82f` 已被加入自定义 CSR decode。
- 确认 Rocket/Tile 内部不会把这段地址误判为非法 CSR。
- 确认 `rrcfg16..31` 的 read/write 路径与低位 `rrcfg` 一致。

### B. 宽度检查

- 确认 `rropc0..3` 保存的是 `5 bit cfg_id`。
- 确认 `rrbar` 保存的是 `5 bit cfg_id`。
- 确认所有 bundle / wire / reg 没有残留 `UInt(4.W)`。

### C. 实例化检查

- 确认 `ReRoCCClientParams.nCfgs=32` 能通过 elaboration。
- 确认 pair-manager 配置下的 client 侧 `nCfgs` 不会被别处回写成 `16`。

### D. 行为检查

- 当软件把 `rropc3 <- 16` 时，随后发出的 `custom3` 指令应打到 `cfg16`。
- 当软件把 `rropc3 <- 31` 时，随后发出的 `custom3` 指令应打到 `cfg31`。
- `cfg16` 与 `cfg31` 不应与 `cfg0..15` 发生别名冲突。

## 最小 metasim 验证负载计划

## 原则

- 验证目标是 **硬件扩容是否真的生效**，不是验证 pipeline runtime。
- 因为 metasim 很慢，负载必须极小：
  - 不跑 Linux；
  - 不拉长数据路径；
  - 只做一个 tile 的 Gemmini 核心计算；
  - 只做一个极小 DMA copy；
  - 只做最短的 acquire / bind / issue / fence / release 闭环。
- 按仓库规范，验证路径必须走 **FireSim-managed metasim**，
  不新增独立 `sims/verilator` 回归。
- 因为当前目标是 **P12 pair-manager**，所以测试不能只验证一种 opcode：
  - `custom2` 路径要实际发一次 coupled-DMA 指令；
  - `custom3` 路径要实际发一次 Gemmini 指令；
  - 两者都要绑定到同一个 `cfg`，这样才能证明 pair binding 正常。

## 建议新增的最小 baremetal 负载

- 建议新增源文件：
  `generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_cfg32_slot_smoke.c`
- 建议以现有文件为模板裁剪：
  `generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_pairmanager_rr_release_probe.c`
- 这个 smoke test 的目标不是覆盖功能，而是覆盖 **三个关键编号**：
  - `cfg15`
  - `cfg16`
  - `cfg31`
- 建议把这个负载设计成 **两级测试**：
  - **Level A: quick smoke**
    - 默认只测 `cfg16`
    - 目标是最短时间验证“高位 cfg + pair binding + DMA + Gemmini”同时正常
  - **Level B: boundary sweep**
    - 再补测 `cfg15` 与 `cfg31`
    - 目标是验证“低位兼容 + 首个高位 + 最大高位”三个边界点

## 推荐的最小 pair-manager 测试序列

### 为什么必须同时测 DMA 和 Gemmini

- 在 P12 pair-manager 配置里，软件视角的关键不是“某个 cfg 可写”，而是：
  - 同一个 `cfg`
  - 同时绑定到 `RROPC2` 与 `RROPC3`
  - 然后分别驱动 `custom2` 与 `custom3`
- 其中：
  - `custom2` 对应 coupled-DMA 路径；
  - `custom3` 对应 Gemmini 路径。
- 如果只测 DMA：
  - 只能证明 `RROPC2 -> cfg` 没坏；
  - 不能证明 `RROPC3 -> cfg` 没坏。
- 如果只测 Gemmini：
  - 只能证明 `RROPC3 -> cfg` 没坏；
  - 不能证明 pair-manager 下 DMA 半边还能跟同一 `cfg` 正常配对。
- 所以更稳妥的 smoke test 应该是：
  - **先发一笔极小 DMA**
  - **再发一个极小 Gemmini 核心计算**
  - **二者都走同一个 `cfg`**
  - **最后统一 `rr_fence(cfg)` 并释放**

### Level A：quick smoke（默认先跑）

- 默认只测一个槽位：`cfg16`
- 原因：
  - `cfg16` 是从 `4 bit` 跨到 `5 bit` 后第一个必须成功的编号；
  - 如果这里失败，基本可以直接判定宽度扩容未打通；
  - 相比完整扫 `15/16/31`，单槽位更快，适合 metasim 首次冒烟。

### Level A 的完整动作序列

- 选择：
  - `cfg_id = 16`
  - `manager_id = 0`（或沿用当前 pair-manager release probe 的最小可用 manager）
- 保存旧值：
  - `prev_opc2 = rr_read_csr(CSR_RROPC2)`
  - `prev_opc3 = rr_read_csr(CSR_RROPC3)`
- acquire：
  - `rr_acquire_cfg(cfg_id, manager_id)`，失败则重试
- 绑定：
  - `rr_set_opc(2, cfg_id)`
  - `rr_set_opc(3, cfg_id)`
- 然后在 **不切换 cfg** 的前提下，连续执行两段子测试：
  - 子测试 1：DMA 小拷贝
  - 子测试 2：Gemmini 单 tile 计算
- 最后：
  - `rr_fence(cfg_id)`
  - `rr_release(cfg_id)`
  - 恢复 `CSR_RROPC2/CSR_RROPC3`

### 子测试 1：极小 DMA copy

- 目的：
  - 证明 `custom2 -> RROPC2 -> cfg16` 路由正确；
  - 证明同一 `cfg` 下 pair-manager 的 DMA 半边是活的。
- 建议大小：
  - `64 B` 或 `128 B`
  - 优先建议 `64 B`
- 原因：
  - 已经足够形成真实 DMA 事务；
  - 比 `4 KB` probe 更快；
  - 足以验证 completion、路由和数据正确性。
- 建议数据组织：
  - `src[64]` 与 `dst[64]` 都做 `64B` 对齐；
  - `completion` 单独 `64B` 对齐。
- 建议动作：
  - 用一个固定 seed 填充 `src`
  - 清零 `dst`
  - `rerocc_coupleddma_set_dst(dst, &completion)`
  - `rerocc_coupleddma_set_src(src, 64)`
  - 等待 `completion != 0`
  - 比较 `src == dst`
- 建议 UART marker：
  - `CFG32_SMOKE_DMA_BEGIN`
  - `CFG32_SMOKE_DMA_OK`

### 子测试 2：极小 Gemmini 核心计算

- 目的：
  - 证明 `custom3 -> RROPC3 -> cfg16` 路由正确；
  - 证明同一 `cfg` 下 pair-manager 的 Gemmini 半边是活的。
- 这里的“极小规模”不是做 `1x1`，因为 Gemmini 当前基本 tile 维度固定为
  `DIM=16`；真正的最小核心计算应理解为：
  - **只做一个 `16x16` tile**
  - **不做 tiled loop**
  - **不做 conv**
  - **不做多 tile pipeline**
- 最推荐的最小算子：
  - `A x I -> C`
  - 其中 `I` 是 `16x16` identity matrix
- 原因：
  - 期望结果最容易判断：`C == A`
  - 不需要额外 CPU golden matmul
  - 指令序列短，适合 metasim
- 建议复用现有最小 Gemmini 模板序列：
  - 参考：
    `generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/template.c`
- 推荐动作序列：
  - 准备 `A[DIM][DIM]`
  - 准备 `I[DIM][DIM]`
  - 准备 `C[DIM][DIM]`
  - scratchpad 地址使用三个连续区域：
    - `A_sp = 0`
    - `C_sp = DIM`
    - `I_sp = 2 * DIM`
  - 发指令：
    - `gemmini_flush(0)`
    - `gemmini_config_ld(DIM * sizeof(elem_t))`
    - `gemmini_config_st(DIM * sizeof(elem_t))`
    - `gemmini_mvin(A, A_sp)`
    - `gemmini_mvin(I, I_sp)`
    - `gemmini_config_ex(OUTPUT_STATIONARY, 0, 0)`
    - `gemmini_preload_zeros(C_sp)`
    - `gemmini_compute_preloaded(A_sp, I_sp)`
    - `gemmini_mvout(C, C_sp)`
  - 然后比较 `A == C`
- 建议 UART marker：
  - `CFG32_SMOKE_GEMMINI_BEGIN`
  - `CFG32_SMOKE_GEMMINI_OK`

### 为什么 Gemmini 子测试必须是“核心计算”，不是只做 flush / mvin / mvout

- 如果只发 `gemmini_flush(0)`：
  - 只能证明一条极短控制指令能过；
  - 不能证明执行阵列、preload、compute 路径真的通。
- 如果只发 `mvin/mvout`：
  - 本质上更接近“Gemmini 搬运路径可用”；
  - 但不足以证明 `compute` 相关 opcode/状态机正常。
- 因此，这里的 Gemmini 子测试必须至少覆盖：
  - `config`
  - `mvin`
  - `preload`
  - `compute_preloaded`
  - `mvout`
- 这样才能说“Gemmini 核心计算路径已被最小规模触发”。

## Level B：boundary sweep（在 quick smoke 通过后再跑）

### Case 1：`cfg15` 回归兼容性

- 沿用 Level A 的完整动作序列；
- 但把 `cfg_id` 改成 `15`；
- 仍要求：
  - `RROPC2 <- 15`
  - `RROPC3 <- 15`
  - 同时通过 DMA 与 Gemmini 两段子测试
- UART 打印 `CFG32_SMOKE_CFG15_OK`

### Case 2：`cfg16` 首个高位槽位

- 沿用 Level A 的完整动作序列；
- 这是默认 quick smoke 的主用例；
- 仍要求 DMA 与 Gemmini 两段子测试都通过；
- UART 打印 `CFG32_SMOKE_CFG16_OK`

### Case 3：`cfg31` 最高位槽位

- 沿用 Level A 的完整动作序列；
- 但把 `cfg_id` 改成 `31`；
- 仍要求：
  - `RROPC2 <- 31`
  - `RROPC3 <- 31`
  - DMA 与 Gemmini 两段子测试都通过
- UART 打印 `CFG32_SMOKE_CFG31_OK`

## 为什么测试里必须“发一条指令”

- 只做 `csrw CSR_RRCFG16, ...` 再读回，不足以证明硬件修复完成。
- 原因是：
  - 这只能证明 CSR 地址存在；
  - 不能证明 `rropc` 的 `cfg` 选择子真的扩到 `5 bit`；
  - 也不能证明高位 `cfg` 没在内部被截断或别名到低位。
- 所以每个 case 至少要做一次：
  - `cfg 绑定`
  - `opcode 路由`
  - `发出 DMA managed 指令`
  - `发出 Gemmini managed 指令`
  - `等待完成`
- 只有这样，才能证明“`cfg16/31` 真能参与真实路由”。

## 为什么这套序列足以验证 pair binding

- 这套序列同时覆盖了：
  - `cfg` CSR decode
  - `RROPC2` 的 `5 bit cfg` 选择
  - `RROPC3` 的 `5 bit cfg` 选择
  - pair-manager 下 DMA 半边
  - pair-manager 下 Gemmini 半边
  - `rr_fence(cfg)` 对指定 `cfg` 的收敛语义
- 如果 `cfg16` 或 `cfg31` 在任何一级被截断为低 4 bit：
  - DMA 或 Gemmini 中至少有一侧会出现别名、超时或结果错误；
  - 因此比“只测一个 opcode”更容易尽早暴露问题。

## 测试负载的最小软件补丁范围

- 本计划虽然是“硬件修复方案”，但为了验证 `cfg16..31`，
  测试负载必须带一个 **测试专用最小软件头文件补丁**。
- 最小补丁只用于 smoke test：
  - 在本测试源文件本地定义 `CSR_RRCFG16..CSR_RRCFG31`
  - 本地把 `RR_MAX_CFGS` 临时扩成 `32`
- 这样做的原因：
  - 当前公共软件头文件仍大量写死 `16`
  - 若把全仓库软件头一起改掉，会把“硬件修复计划”膨胀成“全栈迁移”
- 因此建议：
  - **硬件修复提交**
  - **测试负载补丁**
  分成两个逻辑提交，便于回归定位。

## UART 成功标记

- 建议负载打印以下 marker：
  - `CFG32_SMOKE_BEGIN`
  - `CFG32_SMOKE_CFG16_BEGIN`
  - `CFG32_SMOKE_DMA_BEGIN`
  - `CFG32_SMOKE_DMA_OK`
  - `CFG32_SMOKE_GEMMINI_BEGIN`
  - `CFG32_SMOKE_GEMMINI_OK`
  - `CFG32_SMOKE_CFG15_OK`
  - `CFG32_SMOKE_CFG16_OK`
  - `CFG32_SMOKE_CFG31_OK`
  - `CFG32_SMOKE_PASS`
- 这样 metasim 很慢时，哪怕只看到半截日志，也能立即知道卡在什么阶段。

## FireSim metasim 执行计划

### 建议复用的现有基础配置

- runtime 配置模板：
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_pairmanager_rr_release_probe_tracerv_inst.yaml`
- build recipe：
  `sims/firesim/deploy/config_build_recipes_local_metasim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128.yaml`
- hwdb：
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`

### 建议新增的 workload 侧文件

- baremetal workload json：
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-cfg32-slot-smoke.json`
- host-init 脚本：
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/host-init-cfg32-slot-smoke.sh`
- runtime config：
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_cfg32_slot_smoke.yaml`
- 可选再补一个更快的 runtime config：
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_cfg32_slot_smoke_quick.yaml`
  - 默认只跑 `cfg16`
  - 用于首次硬件冒烟

### 建议运行方式

- 按仓库要求走 FireSim manager 顺序：
  - `launchrunfarm`
  - `infrasetup`
  - `runworkload`
  - `terminaterunfarm`
- 若只是本地 metasim 且已有现成环境，也可以在现有 local metasim 工作流基础上，
  仅替换 workload 名称和输出二进制名。

## 验收标准

- 硬件 elaboration 通过。
- quick smoke 的 `cfg16` 用例通过，并且：
  - DMA 小拷贝成功；
  - Gemmini 单 tile `A x I -> C` 成功；
  - `RROPC2/RROPC3` 都恢复到旧值。
- `cfg15` case 通过，证明低位兼容没有被破坏。
- `cfg16` case 通过，证明首个高位槽位真实可用。
- `cfg31` case 通过，证明最大编号槽位真实可用。
- UART 出现 `CFG32_SMOKE_PASS`。
- 若打开 trace，能看到：
  - `custom2` 在 `cfg16/31` 情况下没有回落到低位别名；
  - `custom3` 在 `cfg16/31` 情况下没有回落到低位别名。

## 主要风险记录

### 风险 1：只扩 CSR 数量，不扩 `rropc` 位宽

- 结果：
  - `cfg16`/`cfg31` 会发生截断；
  - 软件误以为高位槽位可用，实际上仍路由到低位槽位。
- 这是本次修复最需要优先避免的风险。

### 风险 2：Client 参数放宽了，但中间 bundle 仍残留 `4 bit`

- 结果：
  - elaboration 可能通过；
  - 运行时才暴露 alias / truncation。
- 所以必须做“端到端宽度审计”，不能只看编译通过。

### 风险 3：验证负载若只做 CSR 读写，会误判为修复成功

- 结果：
  - 地址 decode 正常；
  - 但实际 routed instruction 仍然错误。
- 所以 smoke test 必须包含一次真实 managed 指令发射。

### 风险 4：只测 DMA 或只测 Gemmini，会漏掉另一半 opcode 路由问题

- 结果：
  - `RROPC2` 可能正确、`RROPC3` 仍有高位截断；
  - 或者 `RROPC3` 正确、`RROPC2` 仍有高位截断；
  - 单边测试无法证明 pair-manager 绑定完整正确。
- 所以默认 smoke test 必须包含：
  - 一笔 `custom2` DMA
  - 一次 `custom3` Gemmini 核心计算

### 风险 5：公共软件头文件仍停留在 16

- 结果：
  - 硬件可能已经支持 `32`；
  - 但常规软件路径暂时无法自然使用 `cfg16..31`。
- 这不阻塞本次硬件修复，但需要在后续软件迁移文档中继续跟进。

## 建议实施顺序

- 第一步：只改 `generators/rerocc/src/main/scala/client/CSRs.scala`
  与 `generators/rerocc/src/main/scala/client/Client.scala`，
  完成 CSR 数量与位宽扩容。
- 第二步：做一轮针对 `cfg_id` 位宽的静态搜索，确认没有残留 `4 bit` 截断点。
- 第三步：新增最小 baremetal smoke test，默认先跑 `cfg16 + DMA + Gemmini`。
- 第四步：quick smoke 通过后，再补跑 `cfg15/16/31` 边界 sweep。
- 第五步：用 FireSim-managed local metasim 跑 smoke test。
- 第六步：若 smoke test 通过，再决定是否推进 pipeline runtime 的正式 `cfg32`
  软件迁移。

## 结论

- 对 P12 pair-manager 来说，把 `cfg` 上限从 `16` 提升到 `32` 的正确入口，
  是 **ReRoCC CSR 容量 + `rropc/rrbar` 选择子位宽 + Client 参数上限**
  三者一起扩，而不是只改一处常量。
- 最小验证不能停留在 CSR read/write，也不能只测单边 opcode；
  必须让同一个 `cfg` 同时驱动：
  - 一笔真实 DMA routed instruction；
  - 一次真实 Gemmini 核心计算。
- 因为用户当前只要求硬件修复方案，所以本计划把软件侧变化严格压缩为
  “测试负载专用的最小补丁”。
