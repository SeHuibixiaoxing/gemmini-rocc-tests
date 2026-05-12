# Hardware vs Software Breakpoints and 8BP Decision

日期：`2026-05-02`

## 结论摘要

对当前 `pipeline-runtime` 卡死调试来说，**没有必要继续把 8 个硬件 breakpoint
当成主线前置条件死磕**。

原因是：

1. 旧 single-core NIC AGFI 已经证明 remote `gdbserver` 可以使用多个普通软件断点。
2. 当前 `pipeline-runtime` 需要的核心调试能力是：
   - 断在可疑函数；
   - 继续运行到可疑点；
   - 卡住后 `Ctrl-C` 抢回；
   - `info threads` / `thread apply all bt` 看所有线程；
   - 读寄存器、反汇编、读变量/内存。
3. 这些能力主要依赖普通软件断点和 gdbserver 的 ptrace 能力，不要求 Rocket core
   提供 8 个硬件 breakpoint comparator。
4. 当前 8BP 线的问题主要发生在 FireSim/F2 bitstream、OCL 控制面、driver/AGFI
   freshness 一致性阶段，还没有到能公平测试“8 个硬件断点是否可用”的阶段。
5. 硬件 watchpoint / `hbreak` 是可选增强，不是当前 unblock `pipeline-runtime`
   调试的必要路径。

## 软件断点是什么

软件断点是 gdb 最常用的断点形式。

用户输入：

```gdb
break main
break prt_dma_submit_tokenized
break prt_schedule_action_submit
```

gdb/gdbserver 通常会在目标程序对应地址处临时改写一条指令，把原指令替换成 trap /
break 指令。CPU 执行到这里时触发异常，Linux 把异常交给 gdbserver，host gdb 就停在
断点处。

软件断点的特点：

- 数量通常比较充裕，不受 CPU 硬件 comparator 个数强限制。
- 最适合普通 Linux 用户态程序调试。
- 可以断在函数、源码行、普通代码地址。
- 依赖目标代码页能被 gdbserver 临时改写并恢复。
- 对只读 ROM、某些内核早期代码、不可写代码页、自修改代码，可能不好用。

对当前项目来说，`pipeline-runtime` 是 Linux 用户态程序，普通函数断点基本属于软件断点
的适用范围。

## 硬件断点是什么

硬件断点不是改写程序指令，而是用 CPU 内部 debug/comparator 资源监视某个地址。

常见形式：

```gdb
hbreak some_function
watch some_global
rwatch *addr
awatch *addr
```

硬件断点/观察点的特点：

- 数量很少，由硬件资源决定。例如 2 个、4 个、8 个。
- 可以用于不能改写指令的区域，例如 ROM、flash、部分内核/裸机早期代码。
- 硬件 watchpoint 可以在“某个地址被读/写”时停住，这是软件断点不容易直接做到的。
- 需要 CPU、内核 ptrace、gdbserver 三者都正确支持。
- 即使 RTL 里有硬件 comparator，Linux/gdbserver 也未必把它暴露成可用的
  `hbreak` / `watch` 能力。

本项目里 `FireSimRocketNICNoTrace8BPConfig` 增加的是：

```scala
new freechips.rocketchip.rocket.WithNBreakpoints(8)
```

它增加的是 Rocket 硬件 debug comparator 资源，不等于 gdb 普通 `break` 必须依赖它。

## 一般调试是否必须用硬件断点

大多数 Linux 用户态程序调试不需要硬件断点。

普通调试通常用：

```gdb
break <function>
break <file:line>
condition <bpnum> <expr>
continue
next
step
bt
info threads
thread apply all bt
```

这些主要靠软件断点即可。

硬件断点通常在下面场景才特别有价值：

1. 调试 ROM、bootloader、裸机早期启动，代码不能被改写。
2. 调试内核或异常入口，软件断点不稳定或不允许写代码页。
3. 需要数据 watchpoint：不知道是谁写坏了某个变量，只想在该地址被写时停住。
4. 调试 self-modifying code / JIT / 特殊只读映射。
5. 程序代码校验自身内容，软件断点改写指令会破坏校验。

`pipeline-runtime` 当前卡死不是这些典型场景。它更需要的是“卡住时所有线程在哪里”，
而不是“一定要让 CPU 硬件 comparator 在某个地址触发”。

## 当前实测说明了什么

旧 single-core NIC AGFI 已验证：

- `target remote` 成功；
- 多个普通软件断点可设置并命中；
- `continue`、`next` 可用；
- `Ctrl-C` 可从运行中抢回控制；
- `info threads`、`thread apply all bt` 可用；
- 寄存器、反汇编、变量/内存读写可用。

这已经足够支撑主线 `pipeline-runtime` 卡死定位。

同时，硬件 watchpoint 实测失败过：

```text
Could not insert hardware watchpoint 4.
Could not insert hardware breakpoints:
You may have requested too many hardware breakpoints/watchpoints.
```

这说明当前 Buildroot/RISC-V Linux/gdbserver 组合下，硬件 watchpoint 不是可靠能力。
这不是 NIC 通信失败，因为同一会话里普通断点、栈、寄存器、内存读写都正常。

## 为什么 8BP 当前不应该作为主线 blocker

8BP 线现在的问题更像硬件平台稳定性问题，而不是 gdb 断点语义问题。

已经看到的失败包括：

- 8BP AGFI 多轮存在 `.post_route.VIOLATED.dcp`；
- F2 上出现 cycle0 deadlock / prelaunch stall / early exit；
- 某轮 8BP `infrasetup` 被确认是 driver/AGFI MMIO register map freshness 错配；
- 非 8BP 当前源码 AGFI 也出现过 OCL 控制面归零，说明当前问题不只由 8BP 触发。

这些失败点都早于“gdb 连接后插入多个断点”的语义阶段。

因此，继续把“8 个硬件 breakpoint 必须可用”当成主线目标，会把精力消耗在一个暂时
不是必要条件的方向上。

## 是否还要继续硬件端死磕

需要分两件事看。

### 需要继续做的硬件工作

需要继续把 **NIC + F2 bitstream + driver/AGFI freshness** 调稳定。

这是 remote gdbserver 的前提，因为 host gdb 要通过 NIC/TCP 连接 guest gdbserver。
如果 F2 OCL 控制面归零、AGFI/driver register map 不一致、`infrasetup` preflight
超时，那 gdbserver 根本跑不到可测试阶段。

这类硬件工作仍然必要。

### 不建议继续死磕的硬件工作

不建议把 **8 个硬件 breakpoint / hardware watchpoint 可用** 作为当前主线必达目标。

当前更合理的策略：

1. 先用普通软件断点把 pipeline-runtime 卡死点定位出来。
2. 如果后续真的出现“必须知道谁写坏某个地址”的场景，再单独评估 hardware watchpoint。
3. 如果要评估 hardware watchpoint，先等 AGFI/driver/timing/freshness 都稳定，再测
   `hbreak` / `watch`，不要把它和 NIC bring-up 混在一起。

## 推荐决策

当前建议：

1. **主线继续使用软件断点 remote gdbserver。**
   - 这是已验证可用的能力。
   - 它足够支撑 `pipeline-runtime` 卡死调试。

2. **8BP 不作为 pipeline-runtime 调试前置条件。**
   - 8BP 可以保留为后续增强实验。
   - 当前不要因为 8BP 不通而阻塞 gdbserver 主线。

3. **硬件侧只继续修必要基础设施。**
   - NIC 能 boot 到 Linux；
   - guest 有 IP；
   - `gdbserver` 能 listen；
   - `target remote` 能连；
   - FireSim driver/OCL 不归零；
   - AGFI 与 driver collateral 一致。

4. **硬件 watchpoint 只在确有需求时另开分支。**
   - 例如已经知道某个内存地址被写坏，但不知道是谁写的。
   - 否则优先用条件断点、日志、breadcrumb、显式检查点。

## 一句话结论

普通 `pipeline-runtime` 用户态卡死调试，软件断点已经够用；硬件断点不是当前必要条件。

现在应该继续把 NIC/F2 平台稳定到能跑 remote gdbserver，而不是继续围绕 8 个硬件
breakpoint/watchpoint 本身消耗构建和调试周期。
