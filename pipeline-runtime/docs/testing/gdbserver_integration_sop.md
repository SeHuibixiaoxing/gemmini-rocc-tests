# Pipeline Runtime gdbserver Integration SOP

更新时间：`2026-05-05 13:20 UTC`

## 1. 目标

- 给 `pipeline-runtime` 主线提供一条比 `TraceV` 更直接的 user-space 调试链路。
- 目标不是“再加一层日志”，而是直接拿到：
  - 当前停住线程的 `PC`
  - 指令窗口
  - 调用栈
  - 多线程状态
- 若 `gdb` 已能直接回答“卡在哪条指令 / 哪个函数 / 哪个线程”，本主线可不再依赖 `TraceV`。

## 2. 适用范围

- 适用于当前 `pairdummy / sbus128 / bertmini / Linux / FireSim FPGA` 主线。
- 不改共享 `br-base`，只对 workload-local rootfs 变体启用 `gdbserver`。
- 不适用于 baremetal `TraceV` bring-up；那条线仍看
  [`tracerv_integration_sop.md`](tracerv_integration_sop.md)。

## 3. 方案概述

这一条链路是：

`Buildroot gdbserver -> guest runner 用 gdbserver 拉起 rerocc_pipeline_runtime-linux -> workflow 在 run 前自动准备 example_1config + SSHPort switch + tap0 -> guest 通过 UART 宣告 endpoint -> host 通过 run host private IP 建隧道 -> cross-gdb 直接 target remote`

当前接入点：

- workflow：
  [`pairdummy_sbus128_gdbserver_workflow.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh)
- runtime config：
  [`config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver.yaml`](/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver.yaml)
- workload：
  [`rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json)
- guest runner：
  [`run_rerocc_pipeline_runtime_bertmini.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh)
- wrapper：
  [`run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh)

当前必须按目标分清三条路线：

1. `single-core Rocket + NIC + no TraceIO + 1BP` smoke 路线。它用于证明
   FireSim NIC、guest TCP、`gdbserver --once`、host cross-gdb 和基本 GDB
   操作是健康的。
2. `pairdummy / 12p4c128sbus32cfg / current NIC / optimized DMA` 路线。它是
   pipeline-runtime 当前要调试的目标路线，必须使用 `cfg32_nic` 构建和运行配置，
   不能把旧非 NIC 或陈旧 HWDB 当成已验证 bitstream。
3. local-gdb 路线。它用于 guest 内本地调试，不等价于 remote gdbserver；remote
   gdbserver 的关键验证是跨 NIC TCP attach、interrupt 和 detach。

已恢复的 single-core 1BP 基线：

- AGFI：`agfi-03d9518415ec82449`
- AFI：`afi-0bf1f9a2bdacaab09`
- top-level commit：`e884f5b8`
- `generators/gemmini` commit：`7b02d19`
- `generators/gemmini/software/gemmini-rocc-tests` commit：`797326d`
- 证据：
  - [`20260505T111031Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260505T111031Z.md)
  - [`20260505T121059Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260505T121059Z.md)
  - [`nic_gdbserver_current_1bp_recovered_20260505.md`](nic_gdbserver_current_1bp_recovered_20260505.md)

该基线已经覆盖：`target remote`、多个 software breakpoint、`continue`、`next`、
`info threads`、`thread apply all bt`、寄存器读取、反汇编、变量/内存读写、线程切换、
`Ctrl-C` 抢回控制和 `detach`。如果这条路线继续通过，而 `pairdummy cfg32_nic`
失败，优先把问题分到目标硬件、目标 rootfs/workload、pipeline-runtime 或
cfg32_nic 专属 FireSim 路径，不要回头怀疑通用 GDB 软件栈。

## 4. 关键约束

- 仍然必须走固定 FireSim workflow：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- 仍然必须通过：
  [`scripts/firesim-tmux-run.sh`](/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh)
- 不要改全局 watchdog；`pairdummy` 本地 profile 已把 live-idle 上限收敛到 `3600s`
- `gdbserver` 只是 workload-local rootfs 变体，不要把它塞回共享 `br-base`
- `gdbserver` 监听的是 guest 内网地址；host 侧要先通过 run host private IP 做 SSH 隧道
- 当前 gdbserver 专用 runtime config 已切到 `topology: example_1config`
- FireSim manager 当前实现仍会无条件读取 `target_config.no_net_num_nodes`；
  即使实际 topology 是 `example_1config`，YAML 里也要保留该字段作为 parser 兼容项
- `pairdummy_sbus128_gdbserver_workflow.sh run` 现在会自动执行：
  - 本地重写/重编 `switch0` 为 `SSHPort(1)` 版本
  - 下发到 run host `/home/ubuntu/switch_slot_0/switch0`
  - 在 run host 上建立 `tap0=172.16.0.1/16`
- `prepare_firesim_networked_gdbserver.sh` 默认还会在 run host 上执行
  `sudo pkill -x hw_server || true`，避免 Xilinx UDP discovery broadcast
  被 `tap0` 注入 guest NIC RX 路径；`virtual_jtag` 保留
- 但 “host 链路起来” 只解决了 FireSim topology 侧的 blocker；
  后文所有 live attach 仍然要求 guest 自己真的枚举出了可用 netdev，并配置出 IPv4
- `example_1config` 只是在 manager 侧生成
  `switch -> server` 的拓扑，并不会替 target 自动补一个 NIC；
  target 必须来自 `WithNIC_...` 硬件配置，DTS/地址图里必须能看到 `ice-nic@10016000`
- 历史上不带 `WithNIC` 的 `pairdummy 4c12p12 sbus128` 生成物已被静态证实不含 IceNIC；
  对这类旧 AGFI 不要继续尝试 guest TCP `gdbserver`
- 当前 `cfg32_nic` HWDB 指向 `agfi-02e18c6f7a7a95096`。该 AGFI 不能直接写成
  “当前源码 + optimized DMA + current NIC + cfg32_nic 已验证”，因为 AWS image
  名称和历史记录显示它可能是较早的 `WithNIC` 构建产物。
- 2026-05-01 `cfg32_nic` 主线构建目录：
  `sims/firesim/deploy/results-build/2026-05-01--02-36-56-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic/`
  已失败于 Vivado placement：
  `ERROR: [Place 30-99] Detail Placement failed` 和
  `ERROR: [Common 17-69] Command failed: Placer could not place all instances`。
  该轮没有产生可用新 AGFI。
- 因此，pipeline-runtime remote gdbserver 调试需要重新构建
  `12p4c128sbus32cfg + optimized DMA + current NIC`，并在 build 完成后更新/确认 HWDB。
- `firesim infrasetup` 会按本地源码重新生成并编译 host driver；
  如果本地 `SimpleNICBridge.scala` 等 target-side bridge 源码已经改过，
  但实际运行的仍是旧 AGFI，
  则这轮 run 只能用于验证 host-only 改动或观察旧 AGFI 的现象，
  不能直接被写成“RTL 修复已经在 FPGA 上验证”
- 当前围绕 `SimpleNIC` bulk 语义、`HostPort` 单步语义与 token 漂移风险的详细静态推理，
  见：
  [`firesim_simplenic_static_audit_20260424.md`](firesim_simplenic_static_audit_20260424.md)

## 5. 当前实现细节

### 5.1 Rootfs

workload-local Buildroot kfrag：

- [`linux_gdbserver.kfrag`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/linux_gdbserver.kfrag)

当前使用：

```text
BR2_PACKAGE_GDB=y
BR2_PACKAGE_GDB_SERVER=y
```

原因：

- 现有 external toolchain 里没有可直接复制的 target-side `gdbserver`
- 因此不能走 `BR2_TOOLCHAIN_EXTERNAL_GDB_SERVER_COPY=y`
- host 侧 cross-gdb 仍使用现成工具链：
  `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb`

### 5.2 Guest 运行方式

默认环境变量：

- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR=0.0.0.0`
- `PIPELINE_RUNTIME_GDBSERVER_PORT=2345`
- `PIPELINE_RUNTIME_GDBSERVER_INFO_PATH=/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info`
- `PIPELINE_RUNTIME_GDBSERVER_LOG_PATH=/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.log`

runner 在 `gdbserver` 模式下执行：

```text
gdbserver --once 0.0.0.0:2345 /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux ...
```

效果：

- 进程会先在 guest 内等待 gdb attach
- attach 之后再真正开始执行 runtime 用户程序
- 因此可以在程序最早阶段就抓住线程、PC 和调用栈

### 5.3 Live announcement

runner 会额外做两件事：

- 把 endpoint / guest IPv4 / 当前 pid 写到
  `/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info`
- 通过 `/dev/console` 往 UART 打一条极短 announcement：

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.x.y endpoint=172.16.x.y:2345 pid=...
```

目的：

- 不依赖 guest 文件 copy-back 才知道 guest IP
- run 还活着时，就能直接从 run host 上看到应该连哪个 guest IP
- BusyBox / Buildroot 场景下，`S99run` 的 `PATH` 不一定覆盖 `/sbin`
- 因此当前 runner 的 `guest_ipv4` 探测已经做了两层增强：
  - 裸 `ip` / `/sbin/ip` / `/bin/ip`
  - 裸 `ifconfig` / `/sbin/ifconfig` / `/bin/ifconfig`
  - 若未显式指定 `PIPELINE_RUNTIME_GDBSERVER_NET_DEV`，
    则优先选 `eth0`；
    否则在 `/sys/class/net/*` 中选择满足以下条件的真实以太网设备：
    `device` 存在、`type=1`、MAC 非全零
  - 最后再按 FireSim 默认规则，从 `/sys/class/net/<dev>/address` 推导 `172.16.<machigh>.<maclow>`

### 5.4 Inferior 跟踪

因为 `bin_pid` 在 `gdbserver` 模式下首先是 `gdbserver` wrapper 自己，
runner 还会轮询：

`/proc/<gdbserver_pid>/task/<gdbserver_pid>/children`

一旦 inferor 真实拉起，会补记：

- `gdbserver-inferior ...`
- `phase=inferior`

这样后续 `/proc` 快照不至于永远只盯着 `gdbserver` 自己。

### 5.5 静态 NIC 前置审计

新增 helper：

- [`audit_pairdummy_networked_gdbserver_target.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pairdummy_networked_gdbserver_target.sh)

用途：

- 在真正 `launchrunfarm` / `runworkload` 之前，先检查当前 `pairdummy` 生成目标是否真的把 IceNIC 接进了 target
- 如果生成 DTS 里完全没有 `ice-nic` / `nic@10016000` / `ethernet` 节点，则直接 fail-fast

当前静态结论要按目标区分：

- 旧的非 NIC 目标目录：
  `sims/firesim-staging/generated-src/firechip.chip.FireSim.WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  只含 `blkdev-controller@10015000`，没有 `ice-nic` / `nic@10016000` / `ethernet`。
  这类 AGFI 即使 host `tap0` / `switch0` 正确、guest `gdbserver` 已经 listen，也不会在 Linux 中出现可配置 IPv4 的真实 NIC。
- 新的 NIC 目标目录：
  `sims/firesim-staging/generated-src/firechip.chip.FireSim.WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  已确认 DTS/JSON/memmap 中包含 `ice-nic@10016000`。

因此，`network_target_audit_status=fail` 时应停止本轮 FPGA 测试；
`network_target_audit_status=pass` 只说明硬件前提具备，仍要继续看 Linux driver、`S40network`、guest IPv4 和 `gdbserver` 是否真正起来。

### 5.6 cfg32 + NIC 构建状态

标准 `12p4c128sbus32cfg` 路线使用以下 `WithNIC` F2 build 配置：

- build config：
  [`config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml`](/home/ubuntu/chipyard/sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml)
- recipe：
  [`config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`](/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml)
- build EC2：`z1d.3xlarge`
- build strategy：`TIMING`
- target frequency：`20MHz`

2026-05-01 的一次构建已确认吃到了 NIC 和 DMA 优化相关 RTL，但最终 placement
失败，没有可用 AGFI。失败证据：

- manager log：
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-01--02-36-56-buildbitstream-M0F3Z2HY8GA258HG.log`
- build result：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-05-01--02-36-56-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic/`
- failure：
  `ERROR: [Place 30-99] Placer failed with error: 'Detail Placement failed please check previous errors for details.'`
  followed by `ERROR: [Common 17-69] Command failed: Placer could not place all instances`

后续重新构建时仍先用 `TIMING`，因为这条路线历史上更接近能稳定产出可用 F2
bitstream 的策略。只有当新失败日志明确指向 placement/timing 之外的可解释原因时，
才考虑策略变体；策略变化必须单独记录，不能和 RTL 或 workload 改动混在一个结论里。

推荐启动命令：

```bash
cd /home/ubuntu/chipyard
SESSION=pairdummy-cfg32-nic-mainline-$(date -u +%Y%m%dT%H%M%SZ)
scripts/firesim-tmux-run.sh --session-name "${SESSION}" \
  buildbitstream \
  -b config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml \
  -r config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml
```

启动后必须记录：

- `SESSION`
- pane log：`tmp/firesim-aws-f2/tmux/${SESSION}.pane.log`
- build host instance id/private IP
- config-specific CL 目录
- RTL freshness gate 结果

## 6. 标准执行步骤

### 6.0 buildbitstream 启动前冻结

NIC / gdbserver 相关 bitstream 构建时间长，启动前必须先把本轮硬件改动想清楚并冻结。
不要在构建已经开始后再继续思考“还应该加哪些硬件观测点”，更不要把构建中的 AGFI
当成包含后补源码改动的版本。

启动任何新的 NIC/gdbserver `buildbitstream` 前，必须完成：

1. 复核历史记录：
   `debug_records/CATEGORY_INDEX.md`、`change_records/CATEGORY_INDEX.md` 以及最近 NIC/gdbserver 记录，
   确认历史已修 bug 没有被回退，当前源码包含上一轮成功测试所需的修复。
2. 静态排查当前卡点：
   同时读 target bridge、FireSim stream engine、host driver、switch/topology、runtime config 和 workload，
   先判断是否能用软件、rootfs、gdbserver、metasim 或旧 AGFI 复现，不能直接跳到新 bitstream。
3. 冻结可综合观测面：
   一次性加入本轮需要的低扇出 CSR/计数器/sticky snapshot，覆盖从 host switch、
   CPU-managed stream、BigToken adapter、SimpleNIC queues、ChannelizedHostPort leaf、
   IceNIC target 端 ready/valid/fire 的完整链路。
4. 冻结源码 marker：
   每轮硬件观测面变化必须更新可读 marker，例如 `minobs_build_marker`，
   并在 debug/change record 里写明该 marker 对应哪些信号。
5. 完成静态校验：
   Scala `attach(...)` 顺序与 C++ `SIMPLENICBRIDGEMODULE_struct` 顺序必须完全一致；
   host bridge C++ 至少跑 `g++ -std=c++17 -fsyntax-only`；
   再跑 `git diff --check`。
6. 写清楚本轮 bitstream 预期回答的问题：
   例如“target cycle 是否推进”“host->target payload 是否进入 adapter”
   “哪个 leaf ready/valid 长期卡住”“是否出现 PCIS/stream backpressure”。

可以一次性规划多个并行 `buildbitstream`，例如 A/B 配置对比或不同规模配置回归；
但每个构建都必须在启动前独立冻结配置、源码 marker、观测点、预期问题和 freshness gate。
禁止先启动一个构建，等待过程中又发现同一卡点没想清楚，再临时修改硬件并启动“补丁版”
构建。构建开始后如果又发现新的硬件观测需求，只能记录到下一轮候选清单；是否启动下一轮，
必须先说明当前已启动构建能回答什么、不能回答什么，以及为什么无法等测试结果。

### 6.0.1 buildbitstream 启动后的 RTL freshness gate

每次启动 `buildbitstream` 后，必须立刻再做一次 RTL freshness 检查。原因是 FireSim F2 构建会先把通用 CL 目录：

```text
sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim
```

复制成当前 build 对应的 config-specific CL 目录，例如：

```text
sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config
```

真正进入该轮 bitstream 的是 config-specific 目录里的 Verilog。只检查源码目录不够；如果 build 已经在修改前启动，或者复制出来的目录仍是旧内容，就会出现“以为构建了新修复，实际 AGFI 还是旧 RTL”的错误。

强制步骤：

1. 用 `scripts/firesim-tmux-run.sh buildbitstream ...` 启动构建后，先看 tmux pane log，确认已经出现 `cp -rf ... cl_firesim -T ... cl_<deploytriplet>`，并记录本轮 `cl_<deploytriplet>` 目录。
2. 在本地 config-specific CL 目录中检查最新 RTL 改动。以 F2 NIC read-lane 修复为例：

```bash
CL_DIR=/home/ubuntu/chipyard/sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config

rg -n "fpga_pci_peek64|read_lane_mask = 8'b0000_0001 << lane" \
  "$CL_DIR/design/cl_firesim.sv"

cmp -s \
  /home/ubuntu/chipyard/sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv \
  "$CL_DIR/design/cl_firesim.sv"
```

3. 如果本轮修改涉及 generated RTL，也必须检查实际 config-specific generated 文件，例如：

```bash
rg -n "<本轮新增 signal/module/assertion/常量名>" \
  "$CL_DIR/design/FireSim-generated.sv" \
  "$CL_DIR/design/FireSim-generated.defines.vh"
```

4. build host 启动后，尽量再 SSH 到 build host private IP 检查远端副本。路径通常是：

```text
/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/<cl_deploytriplet>/design/
```

如果远端 `cl_firesim.sv` / generated Verilog 不含本轮改动，立即停止该 buildbitstream tmux，并终止对应 build EC2；不要等它完成，也不要把该 AGFI 当成新修复结果测试。

NIC cycle-0 deadlock 修复的 freshness gate 还必须额外检查以下 generated RTL：

```bash
rg -n "configChannelReady|fromHostChannelReady|fromHostChannelValid|targetCycleReady|targetBlockedFromHostChannels" \
  "$CL_DIR/design/FireSim-generated.sv"
```

期望至少看到：

```text
configChannelReady = hPort_macAddr_ready & hPort_rlimit_ready & hPort_pauser_ready
fromHostChannelReady = hPort_nicIn_ready & configChannelReady
targetCycleReady = toHostReadyDrive & fromHostChannelReady
targetBlockedFromHostChannels = ~fromHostChannelReady
```

如果只看到 `fromHostChannelReady = hPort_nicIn_ready` 或
`targetCycleReady = ... hPort_nicIn_ready`，说明构建仍在使用会导致 cycle 0
deadlock 的旧 generated RTL，必须停止该轮构建。

对 `12p4c128sbus32cfg + optimized DMA + current NIC` 构建，freshness gate
还必须检查：

```bash
rg -n "SimpleNICBridgeModule|ice-nic@10016000|bytes_written_per_beat|write_shift|MAX_CFGS|cfg32" \
  "$CL_DIR/design/FireSim-generated.sv" \
  "$CL_DIR/design/FireSim-generated.defines.vh" \
  "$CL_DIR" \
  2>/dev/null
```

最小期望：

- generated RTL 中含 `SimpleNICBridgeModule`
- DTS/metadata 中含 `ice-nic@10016000`
- DMA RTL 中含 optimized DMA marker，例如 `bytes_written_per_beat`、`write_shift`
- ReRoCC/cfg 相关生成物能证明使用的是 cfg32 路线，而不是旧 cfg 槽数量

如果 freshness gate 找不到这些 marker，不要等 Vivado 完成；先停止构建并记录为
“构建输入不新鲜/目标不对”。

每次汇报 buildbitstream 已启动时，都要同时汇报：

- tmux session 名称；
- build EC2 instance id / private IP；
- 本轮 config-specific CL 目录；
- freshness gate 检查的关键 `rg/cmp` 结果。

### 6.0.2 长构建无人值守监控

当目标是“构建完成后继续调试，必要时再自动启动下一轮构建”时，不要在
`buildbitstream` 尚未完成时给出最终结论并停止本轮工作。正确做法是进入低频
轮询循环：

```bash
SESSION=<buildbitstream-tmux-session>
while true; do
  sleep 1200
  EXITFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.exitcode
  LOGFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.pane.log

  if [ -f "${EXITFILE}" ]; then
    echo "exitcode=$(cat "${EXITFILE}")"
    tail -n 160 "${LOGFILE}"
    break
  fi

  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "[monitor] ${SESSION} still running at $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    tail -n 80 "${LOGFILE}"
  else
    echo "[monitor] ${SESSION} missing and no exitcode file"
    tail -n 160 "${LOGFILE}"
    break
  fi
done
```

轮询原则：

- 默认间隔是 `1200s`，除非用户明确要求更短；
- 每轮只看 `exitcode`、tmux 是否还在、pane log 尾部，不做高频日志刷新；
- 在 Codex/统一 exec 环境里，不要长期保留很多 `sleep 1200`、
  `tail -f`、`tmux attach` 或 SSH tunnel 会话。长时间等待应由 tmux wrapper
  或 detached monitor 写日志承载，交互侧只用短命令读取 `exitcode` 和 log tail；
- 如果发现失败，先读完整错误链路，静态排查并修改；需要新硬件时再启动下一轮
  `buildbitstream`；
- 如果发现成功，先识别最新 AGFI / AFI，并校验 HWDB 指向该 AGFI，再进入
  `launchrunfarm -> infrasetup -> runworkload`；
- 下一轮 `runworkload` 失败或卡住时，先保留现场、手动搬回日志和抓包，再决定是否
  `terminaterunfarm`；
- 如果又需要硬件修改，启动下一轮 `buildbitstream` 后继续使用本节的 `1200s`
  轮询策略。

监控期间如果本地出现大量遗留进程，优先清理无用的 `tmux attach`、旧
`runworkload`、旧 SSH tunnel 或旧 `sleep` 监控进程；不要杀正在运行的
`buildbitstream` tmux session、Vivado build host 或仍需保留的失败现场。

如果确实需要无人值守地记录构建状态，优先启动一个单独的 detached tmux monitor，
而不是让交互式 `exec` 一直占着进程槽。例如：

```bash
MONITOR=monitor-${SESSION}
tmux new-session -d -s "${MONITOR}" "bash -lc '
  set -euo pipefail
  while true; do
    sleep 1200
    date -u +%Y-%m-%dT%H:%M:%SZ
    EXITFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.exitcode
    LOGFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.pane.log
    if [ -f \"${EXITFILE}\" ]; then
      echo exitcode=\$(cat \"${EXITFILE}\")
      tail -n 160 \"${LOGFILE}\" || true
      break
    fi
    tail -n 80 \"${LOGFILE}\" || true
  done
'"
```

这个 monitor 只负责落日志和停止在构建完成点；发现失败后的静态排查、修改和是否
启动下一轮 bitstream，仍然需要人工/AI 读取日志后执行，不能用脚本盲目自动重构。

### 6.0.3 完整自动调试循环

本项目当前推荐的闭环是：

1. 启动 `buildbitstream`，立刻执行 6.0 的 RTL freshness gate。
2. 构建期按 6.0.1 每 `1200s` 低频轮询。
3. 构建成功后，确认 HWDB 使用最新 AGFI，重新执行 `infrasetup`。
4. 启动 `runworkload` 后，等待 guest boot、网络起来和 `[gdbserver] phase=listening`。
5. 只用真正的 `gdb target remote` 消费 `gdbserver --once` 连接，不用
   `nc`/`telnet` 预探测端口。
6. 能 attach 时，记录 `bt`、`info threads`、寄存器和关键断点结果。
7. 不能 attach、通信损坏或 workload 卡死时，先把 run host 的
   `uartlog`、`heartbeat.csv`、switch 目录、pcap、guest debug 文件和本地 tmux
   log 搬回本地。
8. 现场保存后，静态排查错误边界；如果是 host-only 或软件问题，直接修复并重跑；
   如果必须改 RTL 且 metasim/baremetal 无法覆盖，再启动下一轮 bitstream。

该循环的停止条件是：remote `gdbserver` 能稳定 attach 到目标程序，能在运行中用
`Ctrl-C`/`interrupt` 抢回 prompt，并能读取实时调用栈、线程、寄存器和指令窗口。
如果目标程序继续暴露 pipeline-runtime 自身问题，应把问题转入软件/runtime 调试，
而不是继续把它归为 NIC/gdbserver 链路问题。

### 6.1 构镜

```bash
cd /home/ubuntu/chipyard
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh image-closure
```

`12p4c128sbus32cfg + NIC` 专用路径必须使用 cfg32 wrapper：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh image-closure
```

构镜完成后，可做本地 freshness：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh local-freshness
```

至少应确认：

- guest image 含 `/usr/bin/gdbserver`
- `/firemarshal.env` 里已经带上 `PIPELINE_RUNTIME_GDBSERVER_*`
- runner / wrapper / runtime binary 都是最新版本

### 6.2 FireSim 启动

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh run
```

`cfg32_nic` bitstream 验证对应命令：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh run
```

说明：

- `launch` / `infrasetup` / `run` 现在都会先做静态 NIC 审计。
- 对 `cfg32_nic`，构建成功后必须先把
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml`
  更新到新 AGFI 和匹配的 `driver_tar`，并提交该状态；当前旧 HWDB 指向
  `agfi-02e18c6f7a7a95096`，只能作为历史记录，不能作为新 bitstream 验收输入。
- 如果它们在本地直接报
  `network_target_audit_status=fail`，
  不要继续耗费 FPGA 机时；这代表当前 bitstream 不满足 guest TCP `gdbserver` 的基本前提。
- `run` 会自动做 network prepare。
- 若只想单独验证 switch/tap0 准备，也可执行：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh network-audit
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh network-prepare
```

`cfg32_nic` 单独验证：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh network-audit
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh network-prepare
```

### 6.3 取 run host private IP

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh current-private-ip
```

### 6.4 观察 live gdbserver announcement

从 manager 机器 SSH 到 run host private IP，
直接看 live `uartlog`：

```bash
ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@<run-host-private-ip> \
  'grep -n "\[gdbserver\]" /home/ubuntu/sim_slot_0/uartlog | tail -n 20'
```

如果只想持续观察：

```bash
ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@<run-host-private-ip> \
  'tail -f /home/ubuntu/sim_slot_0/uartlog'
```

拿到 `guest_ipv4=172.16.x.y` 之后，再继续。

如果 announcement 仍是 `guest_ipv4=unknown`，不要直接推断 “host 拓扑没起来”。
先区分三种情况：

- `network-audit` 已经 fail
  这说明 blocker 在 bitstream 根本没带 NIC；
  此时不应再继续做 guest netdev 枚举或 host 侧连 `172.16.x.y`
- `tap0` 仍是 `NO-CARRIER`
  这说明 topology / switch / tap0 链路本身还没拉起
- `tap0` 已是 `LOWER_UP`，但 `gdbserver.info` 里 `net_dev=` 与 `guest_ipv4=` 都为空
  这说明当前 blocker 已经转成 guest 侧没有可用 netdev
- `tap0` 已是 `LOWER_UP`，`net_dev` 有值，但 `guest_ipv4` 为空
  这说明 guest 枚举到了接口，但 IPv4 没配上

## 7. 建隧道与 attach

### 7.1 本地 SSH 隧道

在 manager 机器上执行：

```bash
ssh -N \
  -L 32345:172.16.x.y:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip>
```

说明：

- 左边 `32345` 是 manager 本地端口；
  这里故意不用 `2345`，避免和本机已有服务或上一轮残留隧道冲突
- 右边 `172.16.x.y:2345` 是 guest announcement 给出的 endpoint
- 这个 SSH 会保持前台，调试期间不要退出
- 只有当 host 能实际到达 guest 时，这条隧道才会成功
- 不要用 `nc` / `telnet` 去“试一下端口”：
  本项目 runner 使用 `gdbserver --once`，第一个 TCP 连接会被当成唯一的 gdb 会话；
  `nc` / `telnet` 会消耗这次连接，导致后续真正的 `gdb target remote` 失败
- run host 上的 Xilinx `hw_server` 会周期性发 UDP discovery broadcast。
  在 NIC smoke 实测里，这些包会穿过 `tap0` 进入 SimpleNIC host-to-target 路径，
  造成大量早期网络噪声。当前 network prepare helper 已默认执行
  `sudo pkill -x hw_server || true`；如果绕过 helper 手工跑 FireSim，
  必须自己在 `infrasetup` 后、`runworkload` 前执行同一条命令。
  保留 `virtual_jtag` 不影响这条 gdbserver smoke 路线。
- 如果本地已有旧 tunnel，先杀掉旧进程再开新 tunnel：

```bash
pgrep -af 'ssh .*32345' || true
kill <old-ssh-pid>
```

- tunnel 只能证明 manager 能把 TCP 转发到 run host；
  真正的 guest 端口是否可用，以 `gdb target remote` 为准。
  不要改用 `nc`/`telnet` 预探测。

最新实测边界：

- single-core Rocket + NIC + no TraceIO + 1BP smoke 已经在当前恢复后的 AGFI
  `agfi-03d9518415ec82449` 上重复验证过。guest 能 boot、网络能起来，remote `gdb`
  能连接 `gdbserver`，并能完成软件断点、线程/栈/寄存器/内存读写、`Ctrl-C` 和
  `detach`。
- 旧 single-core NIC AGFI `agfi-019e0b22099a5214b` 还暴露出一个空 RX 路径问题：
  如果 run host 完全没有 host-to-target 包，guest 可能长时间停在早期 Linux boot，
  UART 同时反复出现
  `CPU_STREAM DEBUG pull_blocked ... SIMPLENICBRIDGEMODULE_0_to_cpu_stream ... count=0 ... required_bytes=0`。
  注入少量 `ping 172.16.0.2`/ARP 包可以把旧 AGFI 推进到 IceNet probe 和 gdbserver prelaunch。
  这只是旧 bitstream 的诊断/推进手段，不应写成新 RTL 的正确行为。
- 旧的 `pairdummy` 非 NIC AGFI 不能用于 guest TCP `gdbserver`，因为 target 本身没有 IceNIC
- 2026-05-01 `cfg32 + WithNIC` pairdummy build 已失败于 placement，没有可用新 AGFI。
  后续 pipeline-runtime remote gdbserver 调试必须先重新构建并更新 HWDB。

### 7.2 启动 host cross-gdb

新开一个 manager shell：

```bash
/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

进入 gdb 后：

```gdb
set pagination off
set confirm off
target remote :32345
info threads
thread apply all bt
info registers
x/16i $pc
disassemble /m $pc-32, $pc+32
```

如果要继续跑到下一个可疑点：

```gdb
continue
```

如果要直接停在某个函数：

```gdb
break prt_schedule_action_submit
break prt_dma_submit_tokenized
break prt_rr_release_and_fence
continue
```

控制语义：

- `continue` 会让 target 程序继续跑；如果程序开始长时间等待，host gdb 看起来会像“卡住”，这是正常的
- 在 gdb 里按 `Ctrl-C` 会向 `gdbserver` 发 interrupt，通常可以重新拿回 prompt，然后执行 `info threads` / `thread apply all bt`
- `Ctrl-C` 抢回 prompt 后，如果只是想观察现场再让程序继续跑，优先用
  `signal 0` 或确认不传递 `SIGINT` 后再 `continue`；不要在信号停住状态下直接
  `detach` 并假设 workload 一定自然 PASS
- `detach` 会让 inferior 脱离 gdb 继续跑；如果 guest 脚本还在等程序退出，runworkload 可能继续占着 runfarm
- `kill` 会终止 inferior；这适合验证调试链路，不适合让 workload 自然 PASS
- 如果希望 runworkload 自然收尾，应在 gdb 中 `continue` 到程序正常退出，并观察 guest wrapper 是否继续执行到 `poweroff`

### 7.3 single-core NIC smoke 实测命令

旧 single-core NIC AGFI：

```text
agfi-019e0b22099a5214b
```

已实测通过的最小链路：

```bash
export ROCKET_SINGLECORE_NIC_BUILD_NAME=firesim_rocket_singlecore_nic_notrace_30mhz
export ROCKET_SINGLECORE_NIC_RUNTIME_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_relaxed_noprint_preserve_30mhz.yaml
export ROCKET_SINGLECORE_NIC_BUILD_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_30mhz.yaml
export ROCKET_SINGLECORE_NIC_BUILD_RECIPES_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml
export ROCKET_SINGLECORE_NIC_HWDB_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml

generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh infrasetup
RUN_HOST_PRIVATE_IP="$(generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh current-private-ip)"
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh run "${RUN_HOST_PRIVATE_IP}"

ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@"${RUN_HOST_PRIVATE_IP}" \
  'grep -n "\[gdbserver\]" /home/ubuntu/sim_slot_0/uartlog | tail -n 20'

ssh -f -N \
  -L 32345:172.16.0.2:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  ubuntu@<run-host-private-ip>

/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke
```

进入 gdb 后：

```gdb
set pagination off
set confirm off
target remote :32345
break main
continue
bt
info registers pc sp ra a0 a1
x/16i $pc
```

注意：上面 SSH tunnel 只能在 UART 已经出现 `[gdbserver] phase=listening`
之后建立并交给 gdb 使用。若只看到 `phase=prelaunch`，继续等；如果旧
AGFI 没有入包而停在 early boot，可临时从 run host 侧低频 ping
`172.16.0.2` 推进旧 RX 路径，但仍要等 `phase=listening` 后再 attach。
旧 AGFI 上 console 输出可能严重滞后；如果已经看到 `Starting network: OK`、
run host 能稳定 ping 通 guest，且 `prelaunch` 后长时间没有新 announcement，
唯一允许的端口探测方式仍然是“一次真实 gdb attach”。不要改用 `nc`/`telnet`，
因为它们会消费 `gdbserver --once` 的唯一连接，而且拿不到调试状态。

2026-05-01 在 `agfi-0079cbbca617eca4e` + `+simplenic-empty-switch-poll-interval=1024`
上重新实测，结论更进一步：

- `target remote :32345` 成功
- 初始停在 `_start`
- `break main` 成功
- `continue` 后命中 `main()`
- `bt` 返回 `#0 main ()`
- `info registers` 能读出 `pc/sp/ra/a0/a1`
- `x/12i $pc` 能读出当前指令窗口
- `print smoke_counter` 能读出 guest 全局变量
- run host 到 guest `172.16.0.2` 的 `ping` 成功，ARP 解析为
  `00:12:6d:00:00:02`

这说明 remote gdbserver 链路已经能执行常规源码级调试动作：attach、断点、
运行到断点、调用栈、寄存器、指令窗口和变量读取。它还没有证明“任意断开后可
重连”，因为当前 guest runner 使用 `gdbserver --once`。

2026-05-02 复测 old AGFI 时补充了一条必须固定的约束：

```text
+cpu-managed-stream-debug=0
```

原因是同一颗 `agfi-0079cbbca617eca4e` 如果配到会默认打印
`CPU_STREAM DEBUG` 的 driver，guest 仍能进入 Linux / IceNet / `gdbserver`，
但 `target remote` 初始 RSP 握手可能在 240s 内拿不到 gdb prompt。`gdbserver.log`
会显示远端已经收到来自 `172.16.0.1` 的调试连接，随后本地 expect 超时断开，
`gdbserver --once` 杀掉 inferior。这不是“NIC 完全不通”，而是回归路径相对
05-01 成功状态多了高扰动 console 输出。

复测通过的最小判据：

- UART 没有 `CPU_STREAM DEBUG` 刷屏；
- UART 到达 `[gdbserver] phase=listening`；
- 第一次 TCP 连接必须是真实 `gdb target remote`；
- GDB 能停在 `_start`，并用软件断点命中 `main`、worker heartbeat、
  `smoke_iteration_hook`；
- `info threads` 和 `thread apply all bt` 能返回 5 个线程；
- guest 最后出现 `[gdbserver] phase=exit`、`Simulation complete`、`PASSED`。

自动回归脚本：

```bash
GDBSERVER_SMOKE_EXPECT_TIMEOUT=240 \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_remote_gdbserver_software_expect_smoke.sh \
  <run-host-private-ip> 32345
```

默认不注入静态 ARP / host ARP；只有遇到明确的 host-to-target 邻居问题时，再显式设置：

```bash
export GDBSERVER_SMOKE_STATIC_NEIGH_MAC=00:12:6d:00:00:02
export GDBSERVER_SMOKE_ADVERTISE_HOST_ARP=1
```

### 7.4 不要把 prelaunch 当成 listening

`[gdbserver] phase=prelaunch` 只说明 guest runner 已经算出了 endpoint，
还没有证明 `gdbserver` 已经完整进入 RSP 监听状态。

2026-04-30 的旧 AGFI 实测中，在只有 `phase=prelaunch`、还没有
`phase=listening` 时强行执行 `target remote :32345`，TCP/RSP 已能到达 target，
但 gdb 报：

```text
warning: unrecognized item "timeout" in "qSupported" response
Remote replied unexpectedly to 'vMustReplyEmpty': timeout
```

所以 SOP 里应按这个顺序操作：

- 优先等 UART 出现 `[gdbserver] phase=listening`
- 如果旧 AGFI 一直停在早期 boot 或 `Starting network:`，可以临时从 run host 低频执行
  `ping 172.16.0.2` 注入 host-to-target 包，推进旧 NIC RX 路径
- 如果当前 AGFI 已经出现 `phase=prelaunch`，并且 run host 能 `ping 172.16.0.2`，
  但 UART 长时间没有 `phase=listening`，允许用**一次真实 gdb attach**验证；
  2026-05-01 实测中这种状态下 `target remote` 仍然可以成功断到 `_start/main`。
- 不要用 `nc` / `telnet` / 端口扫描在 `phase=prelaunch` 时“试一下端口”；
  它们可能消费 `gdbserver --once` 的唯一连接，却不给出任何调试状态。

### 7.5 `gdbserver --once` 的一次连接限制

当前 runner 用的是：

```bash
gdbserver --once 0.0.0.0:2345 <target-program>
```

含义是：第一个 TCP 连接就是唯一会话。实测中，如果第一次 gdb 会话被
`timeout` 从外部杀掉，或者本地 gdb 在 target running 状态下异常退出，
后续再执行：

```gdb
target remote :32345
```

会得到类似结果：

```text
Remote communication error. Target disconnected: Connection reset by peer.
```

这不是 NIC 链路重新坏了，而是那次 `--once` 监听已经被消费。处理方式：

- 想继续调试同一个 guest 程序：不要让本地 gdb 被 `timeout` 或外部信号杀掉；
  在 gdb 内用 `Ctrl-C` / `interrupt` 拿回 prompt，然后 `bt` / `info registers`
- 想获得一条全新的 gdbserver 连接：重新跑 workload，让 guest 重新启动 `gdbserver --once`
- 若后续需要“断开后还能反复连”，应把 workload-local runner 改成可配置模式，
  例如对 smoke 负载去掉 `--once`，或另做 `gdbserver --multi` / `extended-remote` 流程

### 7.6 Batch gdb 的安全写法

不要在 batch 命令里机械写：

```bash
-ex 'continue' -ex 'quit'
```

原因：如果 target 仍在 running，`quit` 可能报：

```text
Cannot execute this command while the target is running.
Use the "interrupt" command to stop the target and then try again.
```

更稳的 smoke 方式是只验证 attach、断点、栈和寄存器，然后显式 detach：

```bash
timeout 180 /home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex 'target remote :32345' \
  -ex 'break main' \
  -ex 'continue' \
  -ex 'bt' \
  -ex 'info registers pc sp ra a0 a1' \
  -ex 'detach' \
  -ex 'quit'
```

如果目标是让 workload 自然 PASS，不要在断点处 `detach` 后马上结束本地操作；
应交互式 `continue` 到 inferior 正常退出，并从 UART 看到 guest runner 进入
`[gdbserver] phase=exit` 和 `poweroff`。

## 8. 推荐 first-response 命令

当 run 看起来“卡住”时，第一批命令建议固定成：

```gdb
interrupt
info threads
thread apply all bt
thread 1
info registers
x/16i $pc
```

如果某个线程停在 syscall / futex / poll，
说明不是用户程序 hot loop 卡住；
如果停在 runtime 某个自旋或等待点，
就能直接看到对应函数和指令地址。

失败现场处理原则：

- `runworkload` 失败或长时间不返回时，先不要立刻 `terminaterunfarm`
- 先从 manager log、run host live 文件和 guest debug 文件抓现场：

```bash
ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@<run-host-private-ip> \
  'ls -lah /home/ubuntu/sim_slot_0 &&
   tail -200 /home/ubuntu/sim_slot_0/uartlog &&
   tail -50 /home/ubuntu/sim_slot_0/heartbeat.csv 2>/dev/null || true'
```

- 如果 manager 没有自动 copy-back，就手动从 run host 拷回
  `/home/ubuntu/sim_slot_0/uartlog`、`heartbeat.csv`、driver/switch logs 和 workload 输出
- 只有在确认现场已经搬回本地后，再执行 `firesim terminaterunfarm --forceterminate`
- `terminaterunfarm` 之后必须立刻用 `aws ec2 describe-instances` 查所有相关
  `f2.*` 实例；若仍有 `running`，直接 `aws ec2 terminate-instances` 手工回收，
  并再次确认不再处于 `running`

### 8.1 single-core smoke 现场打包

旧 single-core AGFI 调 gdbserver 时，建议每轮结束前固定搬回一份现场，
避免 manager 没有自动 copy-back 时丢失失败证据：

```bash
SCENE_DIR=/home/ubuntu/chipyard/tmp/firesim-aws-f2/nic-scenes/remote-gdb-$(date -u +%Y%m%d-%H%M%S)
mkdir -p "${SCENE_DIR}"

ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@"${RUN_HOST_PRIVATE_IP}" \
  'tar czf /tmp/sim-slot-0-gdbserver-scene.tgz \
     -C /home/ubuntu/sim_slot_0 \
     uartlog heartbeat.csv 2>/dev/null || true;
   tar czf /tmp/switch-slot-0-gdbserver-scene.tgz \
     -C /home/ubuntu/switch_slot_0 . 2>/dev/null || true'

scp -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no \
  ubuntu@"${RUN_HOST_PRIVATE_IP}":/tmp/sim-slot-0-gdbserver-scene.tgz "${SCENE_DIR}/" || true
scp -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no \
  ubuntu@"${RUN_HOST_PRIVATE_IP}":/tmp/switch-slot-0-gdbserver-scene.tgz "${SCENE_DIR}/" || true

cp /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/rocket-singlecore-nic-gdbserver-*.pane.log \
  "${SCENE_DIR}/" 2>/dev/null || true
```

若这轮 gdb 会话已经成功 attach，另外把本地 gdb transcript 放进同一个
`SCENE_DIR`。之后再 terminate runfarm，并确认 EC2 进入
`shutting-down` 或 `terminated`。

## 9. 何时可以替代 TraceV

满足以下任一条件，就可以优先用 `gdb`，不必再开 `TraceV`：

- 已经拿到明确的 `PC`，能定位到具体 `rerocc_pipeline_runtime-linux` 指令窗口
- 已经通过 `thread apply all bt` 看清线程间谁在跑、谁在等
- 已经确认 stall 发生在 Linux 用户态，而不是 RTL / bridge / boot 路径

只有当怀疑点回到：

- `custom` 指令与硬件握手
- RoCC/bridge/RTL 时序
- guest user-space 已经看不到更深的因果

才回退到 `TraceV`

## 10. 失败分流

### 10.1 image-closure 失败

优先检查：

- `linux_gdbserver.kfrag` 是否被 workload JSON 正确引用
- Buildroot 是否因为 `gdb` 依赖不满足而拒绝配置

### 10.2 guest 无 gdbserver announcement

优先检查：

- `local-freshness` 是否确认 `/usr/bin/gdbserver` 在 image 里
- `/firemarshal.env` 是否带上 `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`
- wrapper / runner 是否已经是最新镜像内容

### 10.3 隧道已通但 `target remote` 连不上

优先检查：

- UART announcement 里的 `guest_ipv4`
- SSH 隧道右侧是否写成了 guest IP，而不是 run host private IP
- `gdbserver.log` 是否显示正在监听对应端口
- 是否曾经用 `nc` / `telnet` 连过 `gdbserver --once` 的端口；
  如果连过，重启 guest 侧 `gdbserver` 或重跑 workload，不要继续复用那次监听

### 10.4 `tap0` 已 `LOWER_UP` 但 `guest_ipv4=unknown`

优先检查：

- `/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info`
  是否已经写出 `net_dev=`
- `S40network` 是否停在 `Starting network:`，
  或者已回到 `Starting network: OK`
- guest 侧接口筛选是否误选了虚拟接口；
  当前应只接受：
  - `/sys/class/net/<if>/device` 存在
  - `type=1`
  - MAC 非 `00:00:00:00:00:00`
- 若 `net_dev` 仍为空，
  下一轮应优先把 `/sys/class/net` 枚举结果直接导出到 guest 文件，
  而不是继续只试图 host 侧连端口

### 10.5 已连上但没符号

优先检查：

- host gdb 打开的 binary 是否是
  `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- 是否误用了 image 内 stripped binary

## 11. 当前结论

- `gdbserver` 路径仍然是当前 `pipeline-runtime` 主线最直接的 user-space 调试办法：
  它能把 inferior 冻结在 attach 前，并能直接读 PC、寄存器、线程和调用栈
- single-core NIC smoke 已经证明 remote `gdbserver` 链路本身可用；
  之前的 target-to-host payload corruption 已由 IceNet driver 的 Linux DMA API 修复解释并实测消失
- `pairdummy` 主线过去的关键误区是复用了不含 IceNIC 的旧 AGFI；
  这不是 `gdbserver` 方法本身失败，而是 target 没有 guest 可见 NIC
- 2026-04-30 的 `cfg32 + WithNIC` pairdummy build 已经静态确认吃到了：
  `ice-nic@10016000`、`SimpleNICBridgeModule`、NIC debug CSR 和 DMA 非对齐拷贝优化
- 下一步等该 AGFI 完成后，优先验证：
  1. guest `dmesg` 中 IceNet driver 是否加载，并打印 `IceNet DMA API mappings enabled`
  2. `S40network` 是否 `OK`
  3. `[gdbserver]` announcement 是否给出非空 `guest_ipv4`
  4. SSH tunnel + `target remote :32345` 是否能断到 `rerocc_pipeline_runtime-linux`
  5. 若 workload 不返回，先保留 runfarm 并手动搬回完整现场，再终止远端实例
