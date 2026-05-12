# 8BP gdbserver Failure Hypothesis

日期：`2026-05-02`

## 结论摘要

当前更强的判断是：

**“不开 8BP 的旧 single-core NIC AGFI 能调通，开 8BP 后调不通”并不能直接说明
`gdbserver` 或 gdb 的多个软件断点能力坏了。**

已有证据更支持下面这个解释：

1. 旧 single-core NIC AGFI 已经证明 remote `gdbserver` 链路本身可用，而且同一
   gdb 会话里已经能设置并命中多个**软件断点**。
2. 8BP 线失败大多发生在 Linux/gdbserver 之前，或发生在 FireSim driver preflight /
   OCL 控制面阶段，不是在 gdb 已连接后插入第 2 到第 8 个断点时失败。
3. 8BP AGFI 历史构建普遍带有 `.post_route.VIOLATED.dcp`，并且运行中出现过
   cycle0 deadlock、prelaunch stall、early exit 等不稳定现场。
4. 至少有一轮 8BP 失败已经被钉死为 driver/AGFI freshness 错配：AGFI 不含新增
   NIC MMIO 字段，但本地/远端 driver 已经按新增字段访问，导致 `+check-fingerprint`
   preflight 超时。
5. 近期当前源码的非 8BP single-core NIC AGFI 也出现过 OCL 控制面归零，说明当前
   阻塞不需要 8BP 才能触发；更像是 F2 bitstream 时序 / OCL 控制寄存器面 / driver
   collateral 一致性问题。

所以当前推测：

**8BP 不是 remote gdb 多软件断点能力的必要条件；8BP 当前调不通，主要是因为对应
bitstream/driver 平台不稳定或不一致，而不是因为 gdbserver 无法使用多个普通断点。**

## 先区分两个“breakpoint”

这里容易混淆两个概念。

### gdb 软件断点

用户在 gdb 里输入：

```gdb
break main
break smoke_iteration_hook
break smoke_worker_heartbeat
```

这类通常是**软件断点**。gdb/gdbserver 会临时改写目标程序内存中的指令，例如放入
trap 指令。它主要依赖：

- guest Linux 里 `gdbserver` 能 ptrace 被调试程序；
- host gdb 到 guest gdbserver 的 TCP/RSP 链路可用；
- guest 程序代码页可被 gdbserver 安装/恢复断点指令。

它不要求 Rocket core 有 8 个硬件 breakpoint comparator。

### Rocket 硬件 breakpoint/watchpoint 资源

`FireSimRocketNICNoTrace8BPConfig` 里增加的是：

```scala
new freechips.rocketchip.rocket.WithNBreakpoints(8)
```

这会增加 Rocket core 内部的硬件 breakpoint/watchpoint 资源。它主要服务于：

- `hbreak`
- 硬件 watchpoint
- 目标平台真的通过 Linux/gdbserver 暴露并使用这些 debug register 的场景

但目前实测中，硬件 watchpoint 已失败：

```text
Could not insert hardware watchpoint 4.
Could not insert hardware breakpoints:
You may have requested too many hardware breakpoints/watchpoints.
```

同一会话中普通软件断点、线程栈、寄存器、内存读写都正常。这说明当时失败的不是 NIC
通信，而是当前 Buildroot/RISC-V Linux/gdbserver 组合没有可靠提供硬件 watchpoint
能力。也就是说，即使 RTL 里放了 8 个硬件 breakpoint，Linux/gdbserver 这条软件栈也
未必能直接用上。

## 已经调通的内容

旧 single-core NIC AGFI：

```text
agfi-0079cbbca617eca4e
```

已经验证过 remote gdbserver 能力矩阵：

- `target remote` 成功；
- `break main` 成功；
- `break smoke_iteration_hook` 成功；
- `break smoke_worker_heartbeat` 成功；
- `continue` 能命中主线程和 worker 线程断点；
- `next` 可用；
- `info threads` 可用；
- `thread apply all bt` 可用；
- 运行中 `Ctrl-C` 能抢回控制；
- `info registers` 可用；
- `x/i $pc` 反汇编可用；
- 全局变量读取、内存读取、内存写入可用；
- 线程切换、frame 切换、`detach` 可用。

这已经覆盖了 `pipeline-runtime` 卡死调试最需要的能力：设置多个普通断点、运行到断点、
卡住后中断、采集全线程栈和寄存器。

所以“之前只有 1 个 breakpoint 能调通”更准确地说应是：

- 旧硬件没有显式打开 8 个 Rocket 硬件 breakpoint comparator；
- 但 gdb 的多个**软件断点**已经实测通过。

## 8BP 当前失败的直接证据

### 1. 8BP AGFI 存在 post-route timing violation

历史 8BP build 结果里，多轮都有：

```text
post_route.VIOLATED.dcp
```

Vivado timing summary 出现明显负 slack，例如 WNS/WHS 都为负。对应运行现场也不是
“gdb 插入第 N 个断点失败”，而是更早的硬件/driver 不稳定：

- cycle0 deadlock；
- prelaunch stall；
- early exit；
- driver readiness preflight timeout。

这类失败发生在 guest Linux/gdbserver 阶段之前，不能当作 gdb 多断点能力失败。

### 2. 8BP 一轮失败被确认是 driver/AGFI freshness 错配

`agfi-054e7cc532edd4698` 这轮 8BP AGFI 启动 fresh F2 后，`infrasetup` 在
`+check-fingerprint` preflight 阶段超时。

后来确认：

- AGFI 的远端 Verilog 快照不含新增的
  `nicbig_ntht_first_*` / `nicbig_pcie_first_*` MMIO 字段；
- 当前本地 driver collateral 已经包含这些字段；
- 部署到 F2 的 `FireSim-f2` 也包含这些新字符串和 MMIO 名称。

这就是 driver/AGFI register map 不一致。它会让 FireSim driver 在很早期就卡住，
还没到 guest Linux 和 gdbserver。

### 3. 非 8BP 当前源码 AGFI 也出现 OCL 控制面归零

当前源码重新构建的非 8BP single-core NIC AGFI：

```text
agfi-0222b9995be0d673e
```

普通 run 看起来是 target cycle 0 deadlock。但 driver-debug run 证明它不是一开始
就死，而是先推进到约 `313M` target cycles，随后 FireSim master/clock/peekpoke OCL
寄存器同时读回 0。

这轮 AGFI 也有 `.post_route.VIOLATED.dcp`，并且 timing report 指向 F2 shell/CL
控制路径，例如：

- `WRAPPER/RL_SHIM/PIPE_RST_OUT_N/... -> WRAPPER/CL/ddr_ready...`
- `WRAPPER/CL/pcis_width_bridge/... -> WRAPPER/RL_SHIM/PIPE_STATUS2/...`

这说明当前有一类问题是 F2 OCL/PIPE_STATUS/CL 控制面稳定性问题。它不是 8BP 独有。

## 当前最可能的根因链

我把概率从高到低排：

### 高概率：bitstream/driver 平台不稳定或不一致

8BP 线当前最强证据都指向这一层：

- `.post_route.VIOLATED.dcp`
- `+check-fingerprint` timeout
- driver/AGFI MMIO register map freshness 错配
- F2 上 OCL 控制面归零
- 失败发生在 gdbserver attach 之前

这层问题会让任何 gdbserver 测试都失真，因为 target 还没稳定跑到可调试状态。

### 中概率：8BP 增加了时序压力，但不是语义根因

`WithNBreakpoints(8)` 会增加 Rocket core 内部 debug comparator 资源。它可能让布局布线更难，
从而把本来就紧张的 F2 shell/CL 控制路径推得更差。

但现有证据不能说“8BP 语义导致 gdbserver 失败”，只能说：

- 8BP 可能增加了实现压力；
- 当前 F2 构建已经有明确 timing violation；
- timing/freshness 问题发生得比 gdb 断点语义更早。

### 低概率：gdb 普通软件断点数量不足

这个目前被旧 AGFI 实测基本排除。旧 AGFI 已经同会话设置并命中多个普通软件断点。

除非后续在一个 timing/freshness 都干净的 8BP AGFI 上出现：

```text
target remote 成功
break main 成功
第 2/第 3 个普通 break 失败
```

否则不应把当前问题归因到“gdbserver 不能多个普通断点”。

### 独立限制：硬件 watchpoint / hbreak 不能作为主路径

硬件 watchpoint 已经实测不可用。即使构建 8BP，当前 Linux/gdbserver 软件栈也未证明
可以可靠使用这些硬件 debug 资源。

如果后续真的需要 `hbreak` 或 watchpoint，应单独做一条验证：

1. 先用稳定、fresh、无明显 OCL 崩溃的 AGFI；
2. `target remote` 成功；
3. 明确执行 `hbreak <func>`、`watch <var>`；
4. 看 gdbserver 是否能插入并命中；
5. 若失败，再查 Linux ptrace / RISC-V debug register / gdbserver 支持，而不是继续改 NIC。

## 对后续调试的建议

### 如果目标是调 pipeline-runtime 卡死

不要把 8BP 当成必要前提。优先使用已经证明可用的能力：

- 普通 `break`
- 条件断点
- `continue`
- `Ctrl-C`
- `info threads`
- `thread apply all bt`
- `info registers`
- 内存/变量读取

这些能力在旧 single-core NIC AGFI 上已经通过，理论上不依赖 8BP。

### 如果目标是验证 8BP 本身

先不要直接跑 pipeline-runtime。应该先跑最小 smoke，并把验收标准拆开：

1. AGFI 与 driver freshness 一致；
2. `infrasetup +check-fingerprint` 通过；
3. guest Linux boot；
4. IceNet 注册和 guest IPv4 正常；
5. `[gdbserver] phase=listening` 出现；
6. `target remote` 成功；
7. 多个普通软件断点通过；
8. 最后才测 `hbreak` / `watch`。

只要失败发生在 1-5，就不是 gdb breakpoint 数量问题。

### 如果 8BP 构建继续 timing violated

优先降低非必要 OCL debug register surface，或者至少不要继续增加 BAR0 可读寄存器。
当前 timing 紧张点多次落在 `PIPE_STATUS*` / OCL / shell-CL 控制路径，过大的可综合
debug register 面会增加控制路径压力。

## 一句话判断

当前不是“1 个 breakpoint 能调、8 个 breakpoint 不能调”的 gdb 语义问题。

当前更像是：

```text
旧稳定 non-8BP AGFI:
  remote gdbserver + 多个软件断点已经可用

当前 8BP / 当前源码 AGFI:
  F2 bitstream timing / OCL 控制面 / driver-AGFI freshness 还不稳定
  因此还没到能公平测试 gdb 多断点能力的阶段
```
