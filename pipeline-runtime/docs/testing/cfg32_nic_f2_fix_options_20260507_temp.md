# cfg32 NIC F2 build repair options - 2026-05-07 temporary note

## 背景

目标是拿到一个能用于 `pipeline-runtime` 调试的 F2 AGFI：`cfg32`、带 NIC、
带当前 gdbserver 软件栈，最好保留优化后的 CoupledDMA。已有 single-core
Rocket + NIC + no TraceIO + 1BP 路线已经恢复并通过 gdbserver smoke，所以当前
大目标的主要问题不是远端 gdbserver 软件路径，而是大规模 F2 实现闭合。

本文只讨论硬件构建如何修。更详细的资源与失败记录见：

- `cfg32_nic_vivado_resource_comparison_20260506_temp.md`
- `cfg32_nic_resource_rootcause_20260506_temp.md`
- `vivado_route_congestion_static_audit_20260506_temp.md`
- `dummy8x8_sbus64_cfg32_nic_synth_status_20260506_temp.md`
- `dummy16x16_c4p8_sbus128_cfg32_nic_build_status_20260506_temp.md`

## 当前证据

### 失败和资源量

| 构建 | post-synth 资源 | 结果 | 说明 |
|---|---:|---|---|
| 12p dummy16x16/sbus128/cfg32/NIC/mainline | LUT `1,264,037` (`96.96%`), Logic LUT `1,047,093`, LUTRAM `216,218`, FF `624,396`, DSP `2,079` | placement 失败 | `Place 30-487`; CL pblock 可用 `26405` CLB，但未放置实例还需要 `29960` CLB。 |
| 12p dummy16x16/sbus128/cfg32/NIC/noTrace | LUT `1,169,035` (`89.67%`), Logic LUT `1,026,303`, LUTRAM `142,006`, FF `618,967`, DSP `2,079` | route 失败 | placement 已过，但 `5774` signals failed to route，`5975` node overlaps。 |
| 8p dummy16x16/sbus128/cfg32/NIC/noTrace | LUT `923,410` (`70.83%`), Logic LUT `801,010`, LUTRAM `121,694`, FF `482,665`, DSP `1,407` | DFX boundary 失败 | route 后期报 `Constraints 18-4430`; `RL_SHIM/DMA_PCIS_AXI_REG_SLC` 和 `DDR_STAT_PIPE_DATA` 边界 nets 缺 PartPin LOC。 |
| 12p dummy8x8/sbus64/cfg32/NIC/noTrace | LUT `1,044,525`, Logic LUT `925,213`, LUTRAM `118,586`, FF `551,796`, DSP `1,995` | 当前仍在 post-route phys-opt | 已看到 `Router Completed Successfully` 和 DFX DRC `0 Errors`，但 post-route phys-opt 仍未生成 packaging collateral。 |

历史 2026-04-23 的 12p+NIC build 曾经路由完成并生成可用 AGFI：
`agfi-02e18c6f7a7a95096` / `afi-03ae6bee93f249537`。该 DCP 的 top flat LUT
约 `1,019,917`，`FireSim_` LUT 约 `933,474`，`globalNoCDomain` LUT 约
`280,774`。当前 12p noTrace 虽然 LUTRAM 接近历史 DCP，但总 LUT 比历史 routed
DCP 高约 `+149k`，`FireSim_` 高约 `+140k`。

### 为什么不带 NIC 曾能过，现在带 NIC 失败

核心差异不是“多了一个小 NIC 外设”这么简单。

1. 带 NIC 后需要 guest-visible IceNIC、host/FPGA token 交换、CPU-managed stream
   engine 和对应的 PCIS/DMA shell 通路。mainline 还带 TraceIO/TracerV，进一步放大
   stream queue 的 LUTRAM 压力。
2. noTrace 能把 TracerV/TraceIO 队列移除，所以 mainline 的 placement 错误消失。
   但 noTrace 不改变 12 个 Gemmini pair、ReRoCC、GlobalNoC、CoupledDMA 和 NIC
   PCIS 边界。因此它继续在 route/DFX 边界上失败。
3. 当前目标比历史 routed 12p+NIC 更大：`cfg32` 让 ReRoCC cfg namespace 变宽并扩散到
   GlobalNoC payload；优化后的 CoupledDMA direct-copy 也让每个 pair wrapper 更大。
4. 本地 `TIMING` 策略实际也发生了漂移。历史 4/23 routed build 实际用了
   `SSI_SpreadLogic_high` placement 和 `AggressiveExplore` route；当前 `TIMING`
   文件解析后执行的是 `ExtraNetDelay_high` placement 和 `Explore` route，并且启用了
   post-route `phys_opt_design -directive AggressiveExplore`。

所以当前问题有两层：

- 12p/full-size 是资源和拥塞已经很接近 F2 small-shell 极限。
- 8p 降到约 70.8% LUT 后仍失败，说明还有独立的 shell/DFX 边界合法性问题。

## DFX boundary 问题是什么意思

AWS F2 small-shell 使用类似 DFX 的静态 shell + 可重构 CL 边界。静态 shell 与 CL
之间的跨边界 net 必须通过 Vivado 放置的 PartPin。`Constraints 18-4430` 的意思是：
某条静态到可重构逻辑的 routing branch 没有合法的 PartPin LOC。

这类错误和普通 `node overlaps` 不一样。即使 LUT 资源很低、route overlap 已经收敛，
也可能因为边界 branch 不合法而失败。8p 构建就是这个形态：

- `DMA_PCIS w`: 约 `121` 条可见报错
- `DMA_PCIS ar`: 约 `24` 条
- `DDR_STAT_PIPE_DATA`: 约 `9` 条
- `DMA_PCIS r`: 约 `3` 条

主要 net family：

- `WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/*`
- `WRAPPER/RL_SHIM/DDR_STAT_PIPE_DATA/Q[...]`
- 对应 CL 侧紧邻模块包括 `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2`
  和 DDR stat pipe 相关逻辑。

## 修复选项

### 选项 A：继续当前 12p dummy8x8/sbus64 build，若能 package 就先拿来测

这是最短路径。当前 12p dummy8x8/sbus64 build 已经完成 legal route，并且 route-time
DFX DRC 为 `0 Errors`。它的问题是 post-route phys-opt 长时间运行，且 PCIS/RL_SHIM
时序仍很差：

- `WNS` 约 `-3.339ns`
- `WHS` 约 `-3.672ns`
- Vivado 提示 `-through` constraint 会阻塞 `sh_cl_dma_pcis_arlen[4]`、
  `cl_sh_dma_pcis_rdata[471]` 等 downstream optimization。

做法：

1. 不取消当前构建，直到它生成 `Developer_CL.tar`/`to_aws`/AFI，或者明确失败。
2. 如果它生成 AGFI，把它标记为低可信 debug AGFI，因为 timing/hold 仍然 violated。
3. 立刻跑 gdbserver attach、`info threads`、`bt`、Ctrl-C、detach 和最小
   pipeline-runtime hang 复现测试。

优点：

- 不需要新 RTL 改动。
- 已经跨过 12p dummy16x16 没跨过的 legal route gate。
- 即使 timing violated，也有历史先例：旧 gdbserver AGFI timing 也差，但 live 测试可用。

缺点：

- 不修 timing。
- 不证明 full-size 12p dummy16x16/sbus128 能过。
- 如果 post-route phys-opt 或 AWS packaging 卡住/失败，需要下一步策略改动。

### 选项 B：同一设计禁用或限制 post-route phys-opt，尝试直接 package legal routed DCP

如果当前 12p dummy8x8/sbus64 已经 legal route，但长时间耗在 post-route
`phys_opt_design`，可以做一个非常窄的策略实验：保留 ordinary `TIMING` 的主要流程，
但关闭或限制 `route_phys_opt`，让 AWS flow 尽快从 legal routed DCP 进入 packaging。

可能改法：

- 新建明确命名的策略，例如 `TIMING_NO_POST_ROUTE_PHYSOPT_DEBUG`。
- 只改：

```tcl
set route_phys_opt 0
```

或者给 post-route phys-opt 增加更保守的限制，而不是改 RTL。

适用条件：

- 当前构建最终证明 route 已经 legal，但 post-route phys-opt 很慢、无明显时序改善，或在
  post-route phys-opt/packaging 前失败。

优点：

- 修改面小。
- 可以验证“我们是否已经有足够好的 legal route，只是被 post-route phys-opt 卡住”。
- 对尽快拿到低可信 debug AGFI 最有价值。

缺点：

- 不修 timing/hold，本质是争取一个可 live validation 的 AGFI。
- 如果 AWS packaging 强制要求 post-route phys-opt 产物，这个策略可能无效。
- 对 8p 的 `Constraints 18-4430` DFX failure 没帮助，因为 8p 是 route_design 阶段已经失败。

### 选项 C：PCIS/DDR_STAT 边界优先修复

这是最应该作为下一轮 RTL/constraint 修复主线的方向。8p 已经把 LUT 降到约 70.8%，仍然报
DFX PartPin，所以单纯砍 pair 数或阵列尺寸不是充分条件。

2026-05-11 的 1C1P hwdebug `TIMING` 重建进一步收窄了 PCIS 部分的对象：这轮没有
`DDR_STAT_PIPE_DATA` 报错，157 条可见 `Constraints 18-4430` 全部来自
`WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE` 的 `aw.aw_pipe`
和 `ar.ar_pipe`。失败 branch 的 sink tile 全在 `X112` 列。用本地 Vivado 打开
`post_phys_opt.dcp` 后确认，对应逻辑连接是：

```text
static/RL_SHIM:
  DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/inst/{aw,ar}.aw_pipe/m_payload_i_reg[*]/Q
    ->
CL reconfigurable:
  CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2/inst/{aw,ar}.aw_pipe/{skid_buffer_reg,m_payload_i_reg}[*]
```

也就是说，当前最窄的 PCIS 修复目标不是 Gemmini、no-DMA、CPU 程序或普通 NoC 逻辑，
而是 AWS shell 512-bit PCIS 地址通道进入 CL 的第一级 SLR2 register-slice handoff。
历史上多个 AGFI 有 AWS shell timing violation 但仍能 functional PASS；当前真正不能接受的是
`route_design` 阶段 DFX PartPin 硬失败，因为它没有 AGFI 可测。

重点对象：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv`
- `firesim_pcis_shell_register_slice`
- `CL_DMA_PCIS_SLV`
- `wide_pcis_clock_convert`
- `pcis_width_bridge`
- `PIPE_DDR_STAT*`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/build/constraints/small_shell_cl_pnr_user.xdc`

具体修法候选：

1. 清理 stale floorplan mapping。当前 `small_shell_cl_pnr_user.xdc` 中仍把
   `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_CROSSBAR` 放在 SLR1 mapping list，但注释又说明
   FireSim 设计里没有该模块。应该移除这个 stale entry，避免约束日志里隐藏真实 mapping
   漏洞。
2. 给实际存在的 FireSim PCIS 入口模块补齐 floorplan。当前只显式约束
   `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2` 和 `AXI4_REG_SLC_PCIS_SLR1`。需要检查
   `wide_pcis_clock_convert`、`pcis_width_bridge` 和第一层 CL-side sink 是否跨出了
   合法 shell-adjacent 区域。
3. 对 `DDR_STAT_PIPE_DATA` 做类似处理。`PIPE_DDR_STAT*` 被放在 SLR2 pblock，但 8p
   报错源头是 `RL_SHIM/DDR_STAT_PIPE_DATA/Q[...]`。需要确认 CL 侧第一级寄存/pipe 是否
   足够靠近合法边界，并且没有被下游逻辑拉远。
4. 如果 Vivado 报告显示 PartPin 是自动创建但 branch 走错，优先通过移动 CL-side first
   register/pipe 解决，而不是直接手写 PartPin LOC。手写 PartPin/DFX 约束只有在确认 AWS
   small-shell flow 支持并且 routed/checkpoint 诊断证明必要时再做。
5. 增加 build preflight 日志：打印实际存在的 boundary/floorplan cells 数量。`get_cells`
   对不存在 cell 返回空时不能只靠肉眼看 xdc。

验证办法：

- 先不构建 full 12p。用已经能暴露问题的 8p dummy16x16/sbus128/noTrace 作为边界修复回归。
- 通过标准 FireSim buildbitstream 进入 route，检查是否还出现：
  `Constraints 18-4430`、`Route 35-17`、`DDR_STAT_PIPE_DATA`、`DMA_PCIS_AXI_REG_SLC`。
- 如果 8p 过 DFX，再把同一边界修复带回 12p dummy8x8/sbus64 或 12p dummy16x16/sbus128。

优点：

- 直接针对当前 8p 的硬失败。
- 比继续砍规模更可能解决“资源低但仍失败”的问题。
- 对未来 full-size NIC/cfg32 目标也有收益。

缺点：

- 需要小心 AWS shell/DFX 约束规则，不能随便把 static `RL_SHIM` 逻辑塞到 CL pblock。
- 需要一次构建验证，普通软件仿真无法证明 PartPin 合法性。

### 选项 D：恢复历史 TIMING directives，作为拥塞修复实验

历史 2026-04-23 routed build 实际使用：

```text
place_design -directive SSI_SpreadLogic_high -no_bufg_opt
phys_opt_design -directive AggressiveExplore
route_design -directive AggressiveExplore -tns_cleanup -timing_summary
```

当前 `TIMING` 实际使用：

```text
place_design -directive ExtraNetDelay_high -no_bufg_opt
phys_opt_design -directive AggressiveExplore
route_design -tns_cleanup -directive Explore -timing_summary
post-route phys_opt_design -directive AggressiveExplore
```

建议不要把这个写成“换成非 TIMING 策略”。更稳的做法是新建一个明确可审计的
`TIMING_LEGACY_20260423` 或类似策略，把历史 directives 固化，并在 build log preflight
打印解析后的 `place_directive`、`phys_directive`、`route_directive`、`route_phys_opt`。

适用条件：

- 目标是 full-size 12p dummy16x16/sbus128 noTrace 的普通 congestion/overlap failure。
- 或者当前 12p dummy8x8/sbus64 需要复刻历史更接近的 route 行为。

优点：

- 直接消除“同名 TIMING 但实际 directives 漂移”的变量。
- 对 12p noTrace 的 `5774` failed route / `5975` overlaps 有实际帮助可能。

缺点：

- 不保证修复 DFX PartPin。8p 的 failure 是 `Constraints 18-4430`，不是普通 overlaps。
- 之前 `TIMING_HOLDFIX` 类策略有变慢/失败历史，所以不要把 hold-fix 参数作为首选。
- 需要作为 tracked collateral 或至少有 preflight 记录，否则以后还会漂移。

### 选项 E：降低 debug 目标规模，但不要把它当成根治

可选缩减包括：

- 12p dummy16x16/sbus128 -> 12p dummy8x8/sbus64
- 12p -> 10p 或 8p
- noTrace
- 降低 stream queue/debug 输出

当前证据：

- 12p dummy8x8/sbus64 从 `1,169,035` LUT 降到 `1,044,525` LUT，约 `-10.65%`，并且已经
  legal route。
- 8p dummy16x16/sbus128 从 `1,169,035` LUT 降到 `923,410` LUT，约 `-21.0%`，但仍因
  DFX PartPin 失败。

结论：

- 缩规模能缓解拥塞，尤其是 full-size 12p route overlap。
- 缩规模不能单独保证成功，因为 DFX boundary 错误在 8p 低资源构建上仍然存在。
- 如果只是需要一个 pipeline-runtime/gdbserver debug AGFI，12p dummy8x8/sbus64 比继续盲目
  尝试 10p 更有价值，因为它已经证明能 legal route。

### 选项 F：参数化关闭优化后的 CoupledDMA direct-copy，作为资源回退

当前优化后的 CoupledDMA misaligned direct-copy 增加了 read-window、cached-read、
partial-mask 和多状态逻辑。和历史 DCP 对比：

- 每个 `GemminiCoupledDMA` 大约增加 `+0.4k` LUT。
- 每个 `GemminiCoupledDMAPairWrapper` 大约增加 `+4.2k` LUT。
- 12 个 pair 合计足以明显影响 route margin。

可选修法：

- 给优化 direct-copy 增加 config 参数，允许大规模 F2 debug bitstream 使用旧实现。
- 保留 cfg32、NIC、gdbserver，先换回低资源 DMA，验证是否能恢复 full-size 12p route。

优点：

- 对 target-fabric LUT 压力有直接帮助。
- 可以和 legacy directives 并行区分“策略漂移”和“DMA 增长”。

缺点：

- 如果本轮 pipeline-runtime 调试必须覆盖优化 DMA 的行为，这个 AGFI 只能当控制组。
- 需要维护新 config 开关，不能粗暴 revert 用户需要的优化。

### 选项 G：重做 cfg32 的 ReRoCC/GlobalNoC 编码

`cfg32` 当前不是纯本地 CSR 变化，它扩大了 ReRoCC client namespace：

- 16 cfg/client: `4 * 16 = 64`，需要 `6` bit client id。
- 32 cfg/client: `4 * 32 = 128`，需要 `7` bit client id。

看起来只多 1 bit，但这个 bit 进入 GlobalNoC payload，会复制到 router buffer、mux 和
route logic。历史 DCP 对比显示 `globalNoCDomain` 增长约 `+57.9k` LUT。

长期修法：

- 保持软件可见 32 个 cfg slot。
- 不把 32 个 slot 全部展开成全局 transaction namespace。
- 用 active-window、bank/page、local cfg decode 或压缩 ID，让 NoC payload 宽度不随
  cfg slot 数线性扩散。

优点：

- 根治 cfg32 对 NoC 的放大效应。
- 对最终 full-size 12p 目标最干净。

缺点：

- 改动面大，涉及 ReRoCC 协议、CSR decode、软件 ABI 和兼容性。
- 不适合为了尽快拿 debug AGFI 而作为第一步。

### 选项 H：NIC/PCIS bridge 结构简化

当前 PCIS 路径包含：

- shell 512-bit PCIS
- `firesim_pcis_shell_register_slice`
- `axi_clock_converter_512_wide`
- `firesim_pcis_width_bridge_512_to_64`
- FireSim 64-bit 内部路径

可选修法：

- 减少 512-bit boundary 附近 fanout，把第一级 handoff 固定在 shell-adjacent 区域。
- 将 width bridge 放在边界之后、远离 DFX crossing，但保留明确 pipeline。
- 减少 PCIS debug counters 或宽总线观测输出。
- 对 `firesim_pcis_width_bridge_512_to_64` 和 shell register slice 做小型 SV 单元测试，
  先保证功能不被 RTL 重排打坏。

优点：

- 直接对 PCIS/RL_SHIM critical family 下手。
- 可能同时改善 timing 和 DFX crossing。

缺点：

- 功能风险高于纯 floorplan。
- 需要重新跑已有 PCIS bridge basic tests 和至少一个 single-core NIC gdbserver smoke。

## 推荐执行顺序

1. 当前 12p dummy8x8/sbus64 build 不取消。它已经 legal route，是目前最接近 AGFI 的候选。
2. 如果它 package 成功，先用该 AGFI 跑 gdbserver/pipeline-runtime live validation。通过后提交 checkpoint，并把它记录为低可信 debug AGFI。
3. 如果它在 post-route phys-opt/packaging 前失败或长时间无进展，优先试选项 B：同一设计关闭 post-route phys-opt，争取直接 package legal routed DCP。
4. 并行准备选项 C 的边界修复，但不要再盲目重复 8p/10p 缩规模 build。8p 已经证明资源低也会报 DFX。
5. 对 full-size 12p dummy16x16/sbus128，下一次应该至少带上：
   - noTrace；
   - legacy TIMING directives preflight；
   - PCIS/DDR_STAT boundary/floorplan 修复；
   - 可选的 DMA old-path 控制组。

## 不建议的路径

- 不建议只把 12p 改成 10p 后直接重试。它可能缓解拥塞，但不能解释 8p 的 PartPin failure。
- 不建议直接切到历史上表现不稳定的 `TIMING_HOLDFIX` 类策略作为主修复。可以作为诊断，但不是首选。
- 不建议在没有 Vivado DFX 诊断的情况下手写 PartPin LOC。先移动/约束 CL-side first register/pipe，并用 DFX DRC 验证。
- 不建议清理或删除 build 目录来“释放空间”作为修复动作；用户已要求清理目录前必须确认。

## 下一步需要补的检查

- 如果当前 12p dummy8x8/sbus64 build 结束，立刻记录：
  - 是否有 `Developer_CL.tar`、`to_aws`、manifest、AFI/AGFI；
  - 是否产生 post-route 或 post-route phys-opt DCP；
  - final timing/DFX/route status。
- 对 `small_shell_cl_pnr_user.xdc` 增加一个只读 preflight 脚本，打印每个 floorplan glob 命中的 cell 数，尤其是：
  - `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2`
  - `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR1`
  - `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_CROSSBAR`
  - `WRAPPER/CL/PIPE_DDR_STAT*`
  - `wide_pcis_clock_convert`
  - `pcis_width_bridge`
- 如果拿到 routed/failed checkpoint，优先跑 Vivado 诊断：
  - `report_route_status`
  - `report_dfx_drc`
  - boundary nets 的 source/sink placement 和 pblock membership
  - PCIS/DDR_STAT 相关 `report_timing`

## 当前判断

最现实的短期目标不是“马上修到 timing clean”，而是先拿到一个 legal-routed、可 package、
可 live validation 的 debug AGFI。当前 12p dummy8x8/sbus64 是最近的候选。如果它不能
package，下一步最小代价修复是跳过 post-route phys-opt；真正面向 full-size 目标的修复则应
集中在 PCIS/DDR_STAT 的 DFX boundary/floorplan，而不是继续单纯砍 Gemmini 规模。
