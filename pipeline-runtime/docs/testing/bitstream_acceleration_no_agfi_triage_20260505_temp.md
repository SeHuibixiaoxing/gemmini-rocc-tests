# Bitstream acceleration and no-AGFI triage notes - 2026-05-05

本文记录当前 1BP single-core Rocket + NIC + no TraceIO + 30MHz gdbserver
恢复工作的构建加速与非 bitstream 排查结论。

## 当前背景

最终目标仍是恢复一个非 8BP 的 1BP F2 AGFI，跑通旧 AGFI 曾通过的 remote
`gdbserver` software-breakpoint 能力矩阵。

已知软件控制已恢复：

- old AGFI: `agfi-0079cbbca617eca4e`
- old AFI: `afi-0ee7774f829acd4de`
- old build: `2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz`
- 2026-05-05 old AGFI + recovered driver 已通过当前 expect/harness 的 1BP
  GDB smoke 矩阵，记录见
  `pipeline-runtime/debug_records/20260505T035417Z.md`

因此，当前新 AGFI 失败应优先按硬件实现 / F2 timing / driver-AGFI 同源性问题
处理，而不是先怀疑 GDB harness。

## 1. 比特流构建加速手段

### 1.1 最高收益：减少无效 Vivado run

真正慢的是 Vivado implementation，而不是 FireSim Verilog 生成或 driver 编译。
最有效的加速不是让每次 route 明显变快，而是减少没有信息量的完整 bitstream
构建。

执行规则：

- 每次只并行构建回答不同问题的候选。
- 普通 `TIMING` 构建默认当诊断/control，不当主候选。
- `TIMING` 一旦在 route 阶段出现 `Route 35-514`，通常应收集日志后尽快回收
  build host。
- `Route 35-514` 的含义是 router 因 hold violator 太多关闭 hold fixing。近期
  1BP 构建历史里，这类 run 后续基本不会自然变成可用 AGFI。

当前使用该规则的例子：

- `TIMING+PCIS` 已经复现 `Route 35-514`，所以被回收。
- 新的 `TIMING+PCIS2SLR+DDRSTAT` 只用于判断结构性修复是否改变 `TIMING`
  失败模式；如果仍复现 `Route 35-514`，应尽快停止。
- 更有希望的候选仍是 `TIMING_HOLDFIX` 系列。

### 1.2 并行构建

并行是当前最实用的墙钟时间优化，但前提是实验正交。

推荐并行形态：

- 一个保守候选：例如 `TIMING_HOLDFIX+PCIS2SLR`
- 一个诊断对照：例如 `TIMING+PCIS2SLR+DDRSTAT`
- 一个较小改动对照：例如单级 PCIS `TIMING_HOLDFIX`

不推荐：

- 同时跑多个只改名字、不改变关键路径假设的 build。
- 在容量满时保留已经提供过结论的低价值对照 build。

当前容量规则：

- manager 是 `c5.2xlarge`。
- 账户当前观察到 32 vCPU 限制。
- `m8i.2xlarge` build host 下通常能保留 3 个并行 build host。
- 如果要换 `m8i.4xlarge`，单次可能略快，但并行度会下降，而且 Vivado route
  不保证线性加速。只建议最后高置信候选使用。

### 1.3 策略选择

`TIMING_HOLDFIX` 慢，但它直接针对当前 blocker：

- 普通 `TIMING` 曾出现 `Route 35-514`。
- `TIMING_HOLDFIX` 设置 `route.enableHoldExpnBailout 0`，避免 router 因 hold
  violator 多而关闭 hold fixing。

`CONGESTION` / `DEFAULT` / `EXPLORE` 可能更快，因为少做或不做 post-route
phys_opt，但风险更高：

- 它们适合做“是否能快速生成诊断 AGFI”的低置信实验。
- 不适合作为当前 30MHz 最终恢复路径的首选。
- 历史上改策略后曾遇到失败，因此不能只为了快而切走当前主策略。

### 1.4 降频构建

可以考虑 20MHz 或 25MHz 的临时 AGFI，用于分离两类问题：

- 如果低频 AGFI 可以跑通 GDB，而 30MHz 不行，说明核心逻辑大概率没坏，主要是
  timing closure。
- 如果低频 AGFI 也不行，再回到逻辑 / driver / runtime 同源性问题。

限制：

- 低频 AGFI 不能替代最终 30MHz 目标。
- 不应过早启动低频 build，避免消耗并行容量。

### 1.5 checkpoint / DCP 复用

F2 脚本会写多个 DCP：

- `post_synth`
- `post_opt`
- `post_place`
- `post_phys_opt`
- `post_route`

`step_user.tcl` 也有从 checkpoint 重新打开实现阶段的逻辑。

适用场景：

- 只改 route / phys_opt strategy。
- 只改少量 XDC 或想重新 report timing。
- 从已有失败 DCP 读取路径、fanout、pblock、QoR 建议。

不适用场景：

- RTL 已改变，例如 PCIS2SLR wrapper 或 DDR stat pipe split。
- 需要交付最终 AGFI。
- 需要证明 FireSim manager 完整 build flow 没有 freshness 问题。

结论：DCP 复用适合诊断，不适合替代最终 FireSim manager clean build。

## 2. 不用 bitstream 的排查

### 2.1 软件 / runtime 控制

可以继续使用 old AGFI 做软件控制。它现在已经重新通过：

- `target remote`
- 多个 software breakpoint
- `continue`
- `next`
- `info threads`
- `thread apply all bt`
- register / memory / variable access
- thread switching
- Ctrl-C 抢回控制
- `detach`

所以以下改动不需要新 AGFI 即可排查：

- FireSim switch
- recovered driver bundle
- rootfs / workload
- SSH tunnel
- expect harness
- host TAP / static ARP 邻居配置

规则：

- 任何 switch / driver / rootfs / workload 改动，先用 old AGFI 控制验证。
- 不要在 old AGFI 控制未通过时，把失败归因给新硬件。

### 2.2 静态同源性检查

不用构建即可排除很多高成本错误：

- HWDB 指向的 AGFI 是否真是目标 AGFI。
- driver bundle 是否和 AGFI 对应。
- runtime config 是否绑定正确 build name。
- 是否误用 8BP config。
- generated RTL 是否来自当前目标 config。
- `FireSimRocketNICNoTraceConfig + BaseF2Config` 是否仍是非 8BP。
- FireSim deploy/build recipe 是否使用预期 `build_strategy`。

这类检查应该在每次 `infrasetup` 或 `runworkload` 前做。

### 2.3 低成本结构检查

每次启动 bitstream 前都应该做：

- `git diff --check`
- YAML parse
- Vivado `xvlog` parse
- 端口宽度 / include path 检查
- pblock cell-name 检查
- generated RTL marker / diff 检查

这不能证明 timing 会过，但能避免把明显语法或配置错误送进长 Vivado run。

### 2.4 使用已有 failed DCP / reports

不生成新 bitstream 也能继续挖 timing：

- `report_timing`
- `report_timing_summary`
- `report_high_fanout_nets`
- `report_qor_suggestions`
- pblock membership 查询
- reset / fanout / SLR crossing 路径查询

当前重点路径：

- PCIS / RL_SHIM / `DMA_PCIS_AXI_REG_SLC`
- `SH_DDR`
- `PIPE_DDR_STAT*`
- `DDR_STAT_PIPE_DATA`
- DDR status/reset 相关 async/default path

这些 report 能指导下一次 RTL/XDC 改动，避免只靠猜测启动新 build。

### 2.5 小型局部测试

full Linux metasim 太慢，而且不等价于 F2 shell/SLR/timing 实现路径。

更适合的小测试对象：

- `ShmemPort` / `SSHPort` packet forwarding
- TCP/IP checksum repair
- flit packing / unpacking
- SimpleNIC bridge token protocol
- PCIS wrapper AXI handshake
- DDR stat pipe latency保持

这些测试不能替代 AGFI 功能测试，但能排除纯软件和局部协议错误。

## 当前执行策略

1. 保留 `TIMING_HOLDFIX` 系列作为主候选。
2. 普通 `TIMING+PCIS2SLR+DDRSTAT` 只作诊断；若出现 `Route 35-514`，收集证据后回收。
3. 不再围绕 8BP 路线排查当前目标。
4. 新 AGFI 可用后，使用 old AGFI 已通过的 recovered-driver GDB harness 做同一矩阵测试。
5. 在等待 build 的同时，继续从已有 DCP/report 中挖 PCIS 和 DDR stat critical path。

