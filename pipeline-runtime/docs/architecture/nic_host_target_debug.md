# NIC host-target 通信机制与当前调试假设

本文整理当前 `Rocket + NIC + Linux gdbserver smoke` 调试线里，
host 与 target 如何通信、SimpleNIC 的数据路径、目前对问题的判断，
以及下一轮 FPGA run 应该如何保留失败现场。

## 当前对象

当前调试目标是最小 Rocket NIC gdbserver smoke：

- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_30mhz.yaml`
- Hardware config:
  `firesim_rocket_singlecore_nic_notrace_30mhz`
- Workload:
  `sims/firesim/deploy/workloads/rocket-singlecore-nic-gdbserver-smoke.json`
- 主要硬件 bridge:
  `generators/firechip/goldengateimplementations/src/main/scala/SimpleNICBridge.scala`
- host-side bridge driver:
  `generators/firechip/bridgestubs/src/main/cc/bridges/simplenic.cc`

这条线目前应按 NIC 专项问题处理。此前 UART 与 TSI 路径有健康证据；
除非后续出现新的反证，不应把主因回退到 UART/TSI。

## FireSim host-target 通信模型

FireSim 在 FPGA 上运行的不是一个自由运行的 target SoC，而是 Golden Gate
转换后的 tokenized FAME1 模型。host 侧 `FireSim-f2` driver 通过各个 bridge
widget 与 FPGA 交换 token，推动 target cycle 前进。

当前目标里和通信相关的 bridge 包括：

- UART bridge：guest 串口输出。
- Block device bridge：rootfs / block IO。
- TSI bridge：程序装载和 target 控制。
- SimpleNIC bridge：guest 以太网收发。
- FASED memory timing model：内存时序模型。
- target-cycle 协调逻辑：只有当前 target cycle 需要的 bridge channel 都满足
  valid/ready 语义时，才允许该 target cycle 完成。

关键点是：一个 bridge 卡住，可以让整个 target cycle 卡住。比如 NIC input
channel 没有给出 `valid`，或者 NIC output channel 没被消费，target-cycle
debug 里可能同时显示其他 bridge group 的 blocker。这种现象不等于 UART/TSI
本身坏了，而是全局 target cycle 在等所有 channel 同步推进。

## 当前 FireSim 网络拓扑

gdbserver smoke 使用 networked topology：

```yaml
target_config:
  topology: example_1config
  no_net_num_nodes: 1
```

`example_1config` 会启动 FireSim switch model。为了 host 能访问 guest，
运行前需要把 `switch0` 准备成绑定 host `tap0` 的 `SSHPort(1)` 版本。

预期链路如下：

```text
host 上的 ping / nc / gdb / tcpdump
  <-> run host 的 tap0，通常是 172.16.0.1/16
  <-> switch0
  <-> FireSim network shared-memory link
  <-> FireSim-f2 进程内的 SimpleNIC host bridge
  <-> FPGA 上的 target NIC bridge channels
  <-> target SoC 内的 IceNIC
  <-> Linux guest network stack
  <-> guest 内 gdbserver 0.0.0.0:2345
```

对 `+macaddr0=00:12:6D:00:00:02`，guest 预期 IP 是：

```text
172.16.0.2
```

历史证据已经证明过 gdbserver 路径本身可以进入 Linux 用户态，并打印
`[gdbserver] phase=listening`。因此当前问题不是“Linux 是否永远进不了
boot”，也不是“rootfs 里有没有 gdbserver”，而是当前最小 Rocket NIC smoke
的 NIC 传输链路仍有断点。

## SimpleNIC 数据路径

SimpleNIC bridge 有两个方向。以下名称按 bridge 代码里的命名。

### host -> target

该方向把 host/tap/switch 收到的以太网帧注入 target NIC：

```text
tap0 / switch 输入
  -> simplenic.cc host driver
  -> StreamFromHostCPU DMA stream
  -> BigTokenToNICTokenAdapter
  -> htnt_queue
  -> ChannelizedHostPortIO OutputChannels:
       nicInValid
       nicInBitsData
       nicInBitsKeep
       nicInBitsLast
       macAddr
       rlimitInc / rlimitPeriod / rlimitSize
       pauserThreshold / pauserQuanta / pauserRefresh
  -> target IceNIC receive path
```

这里 11 个 leaf channel 共同表达一个 host-to-target NIC token group。它们
在语义上不是 11 条互不相关的通道，而是一组必须同周期描述同一份 token 的
字段。如果部分 leaf 提前 fire，另一些 leaf 没 fire，target NIC 可能看到
错位或畸形 token。

### target -> host

该方向把 target NIC 发出的帧送回 host/tap/switch：

```text
target IceNIC transmit path
  -> ChannelizedHostPortIO InputChannels:
       nicOutValid
       nicOutBitsData
       nicOutBitsKeep
       nicOutBitsLast
  -> ntht_queue
  -> NICTokenToBigTokenAdapter
  -> StreamToHostCPU DMA stream
  -> simplenic.cc host driver
  -> switch0 / tap0
```

这里 4 个 `nicOut*` leaf channel 共同表达一个 target-to-host NIC token group。

## bridge 为什么需要 group-level 处理

`ChannelizedHostPortIO` 把每个字段暴露成独立 decoupled leaf channel。这在
Golden Gate 实现上是合理的，但 SimpleNIC 的 payload 语义不是按 leaf 独立的。
因此 bridge 必须满足两个条件：

1. 一个逻辑 NIC token group 不能只消费一部分 leaf。
2. host-to-target `OutputChannel` 的 `valid` 不能等待 `ready`。

第二点是当前问题的核心。对 host-to-target `OutputChannel` 来说，它们在 FAME1
语义里是 target input channel。其 `ready` 来自全局 target-cycle 协调逻辑；
而协调逻辑可能需要先看到 bridge 端给出 `valid`，才能认定本轮 target cycle
可以完成。

如果 bridge 写成：

```scala
valid := queue_has_token && all_leaf_ready
```

就会形成互等：

```text
bridge valid 等 target-cycle ready
target-cycle ready 又等 bridge valid
```

这就是 ready/valid dependency loop。

当前正在构建的 `validdrive` 版本使用：

```scala
val fromHostValidDrive = htnt_queue.io.deq.valid
val fromHostFire = fromHostValidDrive && fromHostChannelReady
htnt_queue.io.deq.ready := fromHostFire
```

含义是：

- 只要 `htnt_queue` 有 token，就向所有 host-to-target leaf channel 给 `valid`。
- 只有所有 leaf channel 都 ready 时，才真正 `deq` 这枚 token。
- 因此 `valid` 不再等 `ready`，但 token 消费仍保持 group-level 同步。

target-to-host 方向仍保持 4 个 `nicOut*` leaf 一起 fire 后再进入 `ntht_queue`。

## 上一份完整 copyback 失败现场

最近一份完整 copy 回本地的 `uartlog` 是：

```text
sims/firesim/deploy/results-workload/2026-04-26--00-29-22-rocket-singlecore-nic-gdbserver-smoke-f2-rocket-singlecore-nic-gdbserver-smoke-notrace/rocket-singlecore-nic-gdbserver-smoke0/uartlog
```

关键片段：

```text
CPU_STREAM DEBUG push_blocked stream=SIMPLENICBRIDGEMODULE_0_from_cpu_stream count=3072 fpga_buffer_size=3072 space_available=0 requested_bytes=58560 required_bytes=58560
SIMPLENIC DEBUG [tick_push_mismatch] requested_bytes=58560 actual_bytes=0 currentround=0
SIMPLENIC DEBUG REGS [tick_push_mismatch] chan_masks from_host valid=0x7ff ready=0x0 fire=0x0 blocked=0x7ff
SIMPLENIC DEBUG REGS [tick_push_mismatch] drives local_queues_ready=1 target_cycle_ready=0 to_host_ready_drive=1 from_host_valid_drive=1 queue_raw htnt(enq_ready=0 deq_valid=1) ntht(enq_ready=1 deq_valid=0)
ERR MISMATCH! on writing tokens in. actually wrote in 0 bytes, wanted 58560 bytes.
```

这说明 host bridge 想把一整批 SimpleNIC token 推进 FPGA stream，但 stream
没有接受任何字节。现场还显示 host-to-target 11 个 leaf 的 `valid` 已经全组
拉高为 `0x7ff`，但 ready 全低，不能完成 fire。

后续 `agfi-038e74ee0655e3a2a` 的 live run 又把前沿推进了一步：

- 能进入 OpenSBI 和 Linux early boot。
- `tap0` 进入 `UP,LOWER_UP`。
- `tcpdump` 能看到 host 侧广播。
- `tap0 RX` 仍是 0。
- channel partial-fire 计数保持为 0。
- target-cycle debug 反复指向 NIC input valid 缺失，尤其是
  `wire_input_missing_valid[5] ep_3_nicInValid`。

这组证据排除了上一轮主要怀疑的 “leaf channel partial fire 导致 token skew”；
新的问题变成：sync-gated 版本把 host-to-target `valid` 错误地依赖到了
host-to-target `ready`。

## 当前怀疑

当前最强怀疑是：

```text
SimpleNIC host-to-target bridge 必须让每个 leaf 的 valid 独立于 ready；
但真正 dequeue htnt_queue 时仍必须等待所有 leaf ready。
上一版 sync-gated 修补把 fromHostValidDrive 写成了
htnt_queue.io.deq.valid && fromHostChannelReady，
因此形成 FAME1 ready/valid 互等。
```

当前 `validdrive` bitstream 预期修正后应看到：

- `from_host_token_available=1` 时，`from_host_valid_drive=1`。
- host-to-target 有 token 时，`chan_from_host_valid_mask=0x7ff`。
- `chan_from_host_partial_fire_count=0`。
- `chan_to_host_partial_fire_count=0`。
- 如果该 ready/valid loop 是最后 blocker，host 注入流量应该能到 guest NIC，
  随后 `tap0` 应出现 RX，能看到 ARP/ICMP/TCP 回包。

如果修正后仍失败，下一层分叉点是：

- host 包进了 SimpleNIC host driver，但没有进入 `htnt_queue`；
- `htnt_queue` 到了 target input leaf，但 IceNIC 没消费；
- IceNIC 消费了 host 流量，但 Linux guest 没出现可用 netdev/IP；
- guest 发出了包，但 target-to-host `nicOut*` 或 switch/tap 路径丢失。

当前已加入的 debug regs 是为了不用再构建 bitstream 就能区分这些分叉。

## 上一轮 runworkload 现场是否完整

不完整。

上一轮 session：

```text
rocket-singlecore-nic-syncgated-runworkload-20260426-0505
```

manager 退出原因是：

```text
Error reading SSH protocol banner
```

manager 退出前还在报告：

```text
Sim running: True
Jobs complete: False
```

因此这轮没有走到 FireSim 正常 copyback 路径，也没有生成完整
`results-workload` 失败现场。这轮可用证据主要来自当时的 live inspection，
以及前一轮已经 copy 回来的完整 `uartlog`。它不能当作完整 postmortem。

当前 runtime config 里还有：

```yaml
workload:
  terminate_on_completion: yes
```

FireSim networked run 的正常流程是：manager 轮询远端 screen；等 simulation
screen 退出后，才 copy job results；随后 kill switch/pipe；如果
`terminate_on_completion: yes`，再终止 run host。若 manager 在 copyback 前
SSH 断开，本地 `results-workload` 就可能缺失或不完整。

## 下一轮现场保留策略

下一轮 NIC debug run 应使用“保留现场”策略。

1. 使用保留现场版 runtime config：

   ```text
   sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_preserve_30mhz.yaml
   ```

   该配置相对普通 smoke runtime 的关键区别是：

   ```yaml
   workload:
     terminate_on_completion: no
   ```

   这样即使 manager 正常识别到 workload 完成，也不会自动终止 run host。

2. 仍然通过 `scripts/firesim-tmux-run.sh` 启动 `runworkload`，但不要把
   manager copyback 当作唯一证据来源。

3. runworkload 开始后，先记录 run host private IP，并尽早启动远端抓包：

   ```bash
   generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/capture_firesim_nic_debug_scene.sh \
     start-tcpdump <run-host-ip> validdrive
   ```

4. 如果 manager 退出、SSH 抖动、或者 workload 卡住，先手动连上远端看现场，
   不要立刻终止 run farm：

   ```bash
   ssh -i /home/ubuntu/firesim.pem ubuntu@<run-host-ip> \
     'screen -ls; ls -lah /home/ubuntu/sim_slot_0 /home/ubuntu/switch_slot_0; tail -200 /home/ubuntu/sim_slot_0/uartlog; tail -50 /home/ubuntu/sim_slot_0/heartbeat.csv; ip -s link show tap0; ip neigh show dev tap0'
   ```

5. 手动打包并拷回完整现场：

   ```bash
   generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/capture_firesim_nic_debug_scene.sh \
     capture <run-host-ip> validdrive /home/ubuntu/chipyard/tmp/firesim-aws-f2/nic-scenes
   ```

6. 确认本地拿到压缩包后，再执行 FireSim 终止，并检查 EC2 进入
   `shutting-down` / `terminated`：

   ```bash
   cd sims/firesim/deploy
   firesim terminaterunfarm --forceterminate \
     -c config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_30mhz.yaml \
     -a config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml \
     -r config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml
   ```

这会增加一些人工步骤和 F2 运行时间，但能避免 manager SSH/copyback 出问题时
丢掉唯一完整失败现场。
