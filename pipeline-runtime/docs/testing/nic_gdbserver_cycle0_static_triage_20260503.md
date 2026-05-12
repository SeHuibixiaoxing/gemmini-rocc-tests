# NIC/gdbserver cycle0 回归静态排查临时记录

日期：`2026-05-03`

## 结论摘要

本轮复核后，当前最重要的结论是：

1. 最新失败 `agfi-018e48135d1d0b7ce` 仍停在 `Simulator deadlock detected at target cycle 0`，没有进入 Linux、IceNet 或 `gdbserver`。
2. 旧成功链路的 host/switch/driver 修复大多已经进入当前测试：BAR4 non-WC、switch `SSHPort/mac2port`、`+simplenic-empty-switch-poll-interval=1024` 都能在当前现场或配置中找到证据。
3. 普通 single-core NIC 配置没有被 8BP 硬件断点实验污染。`FireSimRocketNICNoTrace8BPConfig` 是单独配置，普通 `FireSimRocketNICNoTraceConfig` 没有叠加 `WithNBreakpoints(8)`。
4. 之前把 05-03 cycle0 单独归因到“缺 grouped from-host sideband ready”的结论需要修正：05-02 能 boot 的 `agfi-032d10150efc1d4ec` 已经包含 grouped ready，而最新失败 AGFI 也已经恢复 grouped ready。
5. 目前更强的静态判断是：最新 cycle0 不是 gdbserver 软件问题，也不是已知 host-only 修复缺失；剩余主嫌疑集中在 `SimpleNICBridgeModule` 的观测面/CR map/fanout 引入的实现回归、driver 与 AGFI collateral 一致性边界、以及 F2 post-route violated 的物理实现敏感性。

## 当前失败现场

最新测试对象：

- AGFI：`agfi-018e48135d1d0b7ce`
- AFI：`afi-01fc85fcaa9b6a469`
- build：`sims/firesim/deploy/results-build/2026-05-03--09-52-16-firesim_rocket_singlecore_nic_notrace_30mhz/`
- run：`sims/firesim/deploy/results-workload/2026-05-03--12-55-09-rocket-singlecore-nic-gdbserver-smoke-f2-rocket-singlecore-nic-gdbserver-smoke-notrace-relaxed-noprint-longheartbeat-preserve/`

直接证据：

```text
AFI ID for Slot  0: agfi-018e48135d1d0b7ce
Attached to BAR4 (PCIS, non-WC)
FireSim fingerprint: 0x46697265
Commencing simulation.
Simulator deadlock detected at target cycle 0. Terminating.
```

`remote-sim-slot-0/heartbeat.csv` 只有：

```text
Target Cycle (fastest), Seconds Since Start
0, 602
```

因此这轮失败发生在 guest Linux 之前，不能用 `gdbserver`、TCP/RSP、用户程序断点解释。

## 1. 历史修复对当前问题的帮助

| 时间 | 修复/结论 | 当时推进 | 当前是否仍相关 | 当前证据 |
| --- | --- | --- | --- | --- |
| 2026-04-24 | `SimpleNICBridge` 早期 host/target token lockstep 修复 | 从 `actually wrote 0 bytes` 早期失败向后推进 | 仍相关，但不是当前新增解释 | 当前源码已经是方向解耦后的结构，不是旧 `tFire` 单点推进 |
| 2026-04-25 | 去掉 `toHost.hReady` 对 `fromHost.hReady` 的组合互等 | 避免 HostPort ready 组合环 | 仍相关 | 当前 `toHostReadyDrive = Mux(toHostPayloadValid, ntht_queue.io.enq.ready, true.B)` |
| 2026-04-25 | `fromHostValidDrive = htnt_queue.valid` 改进，后续又演进为 self-cleaning no-packet | 推过 “fromHostValidDrive=0” 卡点 | 仍相关 | 当前 `fromHostValidDrive = true.B`，不再等待 to-host |
| 2026-04-26 | leaf channel 同步/partial fire 修复 | 解决 NIC 多 leaf channel token skew 风险 | 仍相关 | 当前仍用 grouped ready/valid 和 mask 观测，但最新失败太早，尚无 leaf 现场 |
| 2026-04-29 | BAR4 从 write-combining 改成 non-WC | 推过 CPU-managed stream FIFO 访问错误 | 已进入当前测试 | 当前 UART 有 `Attached to BAR4 (PCIS, non-WC)` |
| 2026-04-29 | FireSim switch `SSHPort/mac2port` 修复 | 推过 switch segfault，target ARP reply 可回 host | 已进入当前软件栈 | 当前 switch 能打开 `tap0`，没有旧 segfault；但 target 没启动，所以没有 guest 回包 |
| 2026-05-01 | host-driver empty switch poll interval | remote gdb 基线跑通 | 已进入当前测试 | 当前 plusarg 有 `+simplenic-empty-switch-poll-interval=1024`，driver strings 有 `empty_rounds_skipped_switch` |
| 2026-05-02 | 新 AGFI 能 boot Linux/IceNet/gdbserver，但出现 target->host payload corruption | 卡点从 boot/NIC bring-up 推到 TCP/RSP payload 完整性 | 对当前很重要：它是最近的“能 boot”对照 | `agfi-032d10150efc1d4ec` 能 boot，且 generated RTL 已有 grouped ready |
| 2026-05-03 | grouped sideband ready 修复 | 修复了 `agfi-0a8a...` 中确实缺 sideband ready 的 RTL 形态 | 只能解释 `agfi-0a8a...`，不能单独解释最新 `agfi-018e...` | 最新 `agfi-018e...` 已恢复 grouped ready，但仍 cycle0 |

结论：历史修复没有白做，整体是向前推进过的；但最新回退已经不在那些已闭环的 host-only bug 上。

## 2. 成功 gdbserver 基线的修复是否进入当前硬件/运行配置

已经成功过的 remote gdb 基线：

- `agfi-0079cbbca617eca4e`
- AFI：`afi-0ee7774f829acd4de`
- build：`2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz`
- 记录：`debug_records/20260501T045108Z.md`

该基线已经验证：

- `target remote`
- 多个软件断点：`break main`、`break smoke_iteration_hook`、`break smoke_worker_heartbeat`
- `continue`、`next`
- `info threads`
- `thread apply all bt`
- `Ctrl-C`
- `info registers`
- 反汇编、变量读写、内存读写、线程/frame 切换、`detach`

当前配置与该成功基线的关键对照：

| 项 | 成功基线 | 当前最新 | 结论 |
| --- | --- | --- | --- |
| BAR4 | non-WC driver 修复后通过 | UART 明确 `Attached to BAR4 (PCIS, non-WC)` | 已包含 |
| empty switch poll | `+simplenic-empty-switch-poll-interval=1024` 后通过 | 当前命令行含同一 plusarg | 已包含 |
| switch root SSHPort | 修复后通过 | 当前 switch 能打开 `tap0`，未见旧越界崩溃 | 已包含 |
| grouped ready | 04-30 通过版 generated RTL 没有 grouped sideband ready；05-02 能 boot 版有 grouped ready | 当前最新也有 grouped ready | grouped ready 不是唯一成败分界 |
| SimpleNIC datapath | 05-02 boot 版与当前最新在关键 datapath 表达式上基本一致 | 当前最新仍 cycle0 | 差异更偏向 debug/CR/fanout 或物理实现 |
| driver/RTL freshness | 04-30/05-02 均有对应 driver bundle | 当前 run 的 `FireSim-f2` SHA 与 05-03 两个 build 一致；fingerprint 正常 | 尚未看到旧式 register-map 明确错配，但仍需警惕 CR map 变化 |

当前最新与 05-02 能 boot 版的关键 generated RTL 对照：

```text
05-02:
fromHostAllReady = hPort_nicIn_ready & configChannelReady
targetCycleReady = toHostReadyDrive & fromHostAllReady
hPort_nicIn_valid = 1'h1
htnt_queue_io_deq_ready = fromHostEmptyTokenAvailable | fromHostPayloadCapture
ntht_queue_io_enq_valid = hPort_nicOut_valid & hPort_nicOut_bits_valid

05-03 latest:
fromHostChannelReady = hPort_nicIn_ready & configChannelReady
targetCycleReady = toHostReadyDrive & fromHostChannelReady
hPort_nicIn_valid = 1'h1
htnt_queue_io_deq_ready = fromHostEmptyTokenAvailable | fromHostPayloadCapture
ntht_queue_io_enq_valid = hPort_nicOut_valid & hPort_nicOut_bits_valid
```

这说明最新失败不能再只靠“缺 sideband ready”解释。

## 3. 8BP 实验是否导致当前普通 NIC 无法推进

静态检查结果：

```scala
class FireSimRocketNICNoTraceConfig extends Config(
  new WithNIC ++
  new chipyard.config.WithNoTraceIO ++
  new WithDefaultFireSimBridges ++
  new WithFireSimHighPerfConfigTweaks ++
  new chipyard.RocketConfig)

class FireSimRocketNICNoTrace8BPConfig extends Config(
  new freechips.rocketchip.rocket.WithNBreakpoints(8) ++
  new FireSimRocketNICNoTraceConfig)
```

普通 build recipe 使用：

```yaml
TARGET_CONFIG: FireSimRocketNICNoTraceConfig
```

8BP build recipe 使用：

```yaml
TARGET_CONFIG: FireSimRocketNICNoTrace8BPConfig
```

因此：

- 当前普通 single-core NIC AGFI 没有静态证据显示被 `WithNBreakpoints(8)` 污染。
- 8BP 失败不应再作为当前普通 NIC cycle0 的主解释。
- 8BP 相关文档里的结论仍成立：pipeline-runtime 用户态调试主线优先使用软件断点，不把 8 个硬件 breakpoint 当作前置条件。

## 4. 当前 bug 的候选根因与修复路径

### 候选 A：`SimpleNICBridgeModule` debug/CR/fanout 改动引入实现回归

证据：

- `FPGATop` 在 05-02 能 boot 与 05-03 latest 之间 byte-identical。
- `SimpleNICBridgeModule` SHA 不同，行数从约 5052 行增加到约 5894 行。
- 两者关键 datapath 表达式基本一致，差异主要来自 payload snapshot、`nicbig_last_pcie_out_words`、`to_host_hist_*`、额外 CR 读口/寄存器。
- 05-03 两颗 cycle0 AGFI 的 driver 都包含更多 payload-heavy snapshot strings。

为什么可疑：

即使 Chisel 语义上只是可观测寄存器，综合后会给原始 datapath 增加 fanout、寄存器、CR mux 和控制路径负载。在 F2 post-route 已经 violated 的背景下，这类“观测面变大”可能改变实现结果。

修复路径：

1. 做一个最小回归 bitstream：保留已经证明必要的 host-driver/switch 修复，硬件 RTL 回到 05-02 能 boot 的 SimpleNIC datapath/观测面，不带 latest payload-heavy snapshot。
2. 如果能 boot，再分批恢复观测点，每次只增加一小组，避免一次性把 payload snapshot、first16、CR 顺序、ready 聚合全部混在一起。
3. 对必须保留的观测点，优先把高 fanout 原信号先打一拍成 debug-only shadow，再挂 CR，降低对热路径和 targetCycle ready 的直接 fanout。

### 候选 B：CR map / host driver collateral 与 AGFI 的边界仍需更严格核验

证据：

- 之前 8BP 线已经出现过 driver/AGFI register map freshness 错配，导致 `+check-fingerprint` 或早期 preflight 异常。
- 当前 `FireSim-f2` SHA 在 05-03 05:15 与 09:52 两个 build 相同，但两个 AGFI 的 generated RTL SHA 不同。
- 当前 `+check-fingerprint` 能通过，说明 OCL presence/fingerprint 基本可读；但这只排除了最粗的 driver/AGFI 错配，不等于所有 SimpleNIC CR 地址语义都匹配。

为什么可疑：

SimpleNIC CR 顺序和字段数量近期多次调整。driver 如果按新的 `simplenic.h` 读旧 AGFI，或按旧字段解释新 AGFI，可能在打印/轮询 debug 寄存器时访问错误地址。当前失败没有打开 token debug，所以它不是已发生的直接证据，但后续调试必须防止再次误判。

修复路径：

1. 每次 build 进入 Vivado 后，固定检查 build collateral 里的 generated Verilog 是否含本轮目标 marker。
2. 每次 run 前，记录 `FireSim-generated.sv` SHA、`FireSim-f2` SHA、HWDB AGFI、run UART AGFI 四者。
3. 如果需要读 SimpleNIC CR，先用当前 AGFI 对应 build 产物里的 driver，不混用历史 driver bundle。

### 候选 C：grouped sideband ready 的方向仍未完全钉死

证据：

- 05-03 `agfi-0a8a...` 的 generated RTL 确实丢了 sideband grouped ready：`targetCycleReady` 只看 `hPort_nicIn_ready`。
- 最新 `agfi-018e...` 已恢复 `hPort_nicIn_ready & configChannelReady`，但仍 cycle0。
- 05-02 能 boot AGFI 也有 grouped ready。

为什么仍需保留：

grouped ready 对 `macAddr/rlimit/pauser` 这组 sideband leaf 是否同 target cycle 一起闭合是合理的；但它不是最新失败的充分原因。后续如果继续改，应避免在“只看 nicIn.ready”和“看全部 sideband ready”之间来回震荡，而应该用可综合观测确认 cycle0 时到底哪个 leaf `ready=0`。

修复路径：

1. 下一轮如必须改硬件，应加入 cycle0/first-blocked sticky snapshot，记录 `nicIn/macAddr/rlimit/pauser/nicOut` 的 valid/ready/fire、`targetCycleReady`、`done`、`debugCycle`。
2. 该 snapshot 要能在 target cycle 0 未推进时仍被 OCL 读到。
3. 现场中要打印完整 leaf mask，而不是只打印 `target cycle 0`。

### 候选 D：`done` 语义与 FireSim bridge 调度期望不一致

证据：

- 2026-04-25 记录把 `done` 从 `!tFire` 改为 `!(toHostFire || fromHostFire)`。
- 当前源码仍是：

```scala
genROReg(!(toHostFire || fromHostFire), "done")
```

为什么可疑：

如果 FireSim host bridge 调度仍把 `done` 当作“该 bridge 在本轮 target cycle 已经完成全部必要 input/output channel”的信号，而 SimpleNIC 又允许 to-host/from-host 方向分开 fire，那么 `done` 的解释需要非常明确。错误的 `done` 可能导致 host 侧判断 target cycle 可结束或不可结束的时机与实际 leaf channel 完成状态不一致。

当前证据强度：

中等偏低。05-02 能 boot 版也大概率已经带有该语义，所以它不是最新 05-03 独有差异。但它仍是 bridge 协议层必须复核的点。

修复路径：

1. 对照 FireSim bridge driver 对 `done` 的使用，确认 `done` 是否只用于 idle/diagnostic，还是参与 token 调度。
2. 如果参与调度，改成与 target-cycle closure 等价的 sticky completion，而不是瞬时 `!(toHostFire || fromHostFire)`。
3. 用 metasim 或小 RTL 仿真验证 `done`、`targetCycleReady`、leaf fire 的时序关系。

### 候选 E：post-route violated 让 debug fanout 改动触发物理实现敏感性

证据：

- 05-02 能 boot 的 AGFI 也有 `post_route.VIOLATED.dcp`，所以 “violated” 不能单独解释失败。
- 05-03 两颗 cycle0 AGFI 同样有 `post_route.VIOLATED.dcp`，且 WNS 约 `-3.2ns`，违规路径多在 DDR/shell/reset/PIPE 相关路径。

当前证据强度：

中等。不能说“就是 timing”，但可以说在 05-02 与 05-03 datapath 逻辑接近、差异主要是 debug/CR/fanout 的情况下，物理实现敏感性是合理候选。

修复路径：

1. 不要把 timing 作为唯一根因；优先先构建一个减小 SimpleNIC debug fanout 的最小 bitstream 看是否恢复 boot。
2. 如果恢复，再逐步加观测点，并比较 post-route timing path 是否新增/变差。
3. 保留 `build_strategy: TIMING`，但不要指望它自动消除所有 F2 shell 约束违规。

## 下一步建议

1. 先不要围绕 8BP 继续排查当前普通 NIC cycle0。
2. 先做一次“05-02 能 boot 版形态”的最小硬件回归：目标是验证最新 cycle0 是否由后续 payload-heavy debug/CR/fanout 引入。
3. 若需要新硬件，应一次性加入低 fanout、cycle0 可读的 sticky snapshot，而不是只依赖 UART 后期打印。
4. 如果最小形态能 boot，再回到 05-02 的更深问题：target->host payload corruption / remote GDB RSP 数据完整性。
5. 如果最小形态仍 cycle0，则优先复核 `done` 语义、ChannelizedHostPort target-cycle closure，以及 run 使用的 driver/AGFI bundle 是否完全同源。

## 当前不能下的结论

- 不能说 NIC/gdbserver 全链路坏了；旧 AGFI 已经完整验证 remote gdb 常用功能。
- 不能说当前是 gdbserver 软件问题；当前没有进入 Linux。
- 不能说 8BP 污染了普通 single-core NIC；静态配置是分开的。
- 不能继续把最新 `agfi-018e...` 的 cycle0 单独归因为缺 grouped ready；该修复已进入 latest AGFI，而 05-02 能 boot 版也有 grouped ready。
