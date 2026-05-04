# 2026-05-04 single-core NIC gdbserver change timeline: 1bp / 8BP / rollback

类别：`gdbserver` / change timeline / temporary audit

## A. 1bp 成功路径改过什么

这里的 1bp 指默认 `FireSimRocketNICNoTraceConfig`，即不显式加
`WithNBreakpoints(8)`。Rocket 默认 `nBreakpoints=1`。

### 1. F2 PCIS / CPU-managed stream 基础修复

记录：

- `debug_records/20260429T090408Z.md`
- `change_records/20260429T095028Z.md`
- `change_records/20260429T145226Z.md`
- `change_records/20260429T221723Z.md`

改动：

- 新增/修正 F2 shell 512-bit PCIS 到 generated 64-bit `F1Shim.io_pcis` 的桥。
- `read_lane_mask()` 从“按 `ARSIZE=6` 一次消费 8 个 64-bit lane”改为“只消费地址对应的单个 lane”。
- `simif_f2.cc` 中 BAR4 从 `BURST_CAPABLE` 改为 `0`，避免 CPU-managed stream FIFO 被 WC mmap 乱序访问。

当前状态：

- 当前 `cl_firesim.sv` 仍保留单-lane read mask。
- 当前 `s_axi_wready` 是旧通过语义：

  ```text
  (wr_aw_active || wr_aw_fire)
  ```

- 当前 `simif_f2.cc` 仍打印 `Attached to BAR4 (PCIS, non-WC)`。

判断：这类 1bp 基础坑当前没有回退。

### 2. switch / root SSHPort 修复

记录：

- `debug_records/20260429T230748Z.md`
- `change_records/20260429T230748Z.md`

改动：

- `switch_model_config.py` 增加 `MAC2PORT_SIZE`。
- root switch 的 `NUMDOWNLINKS` / `NUMUPLINKS` 重新定义，root `SSHPort` 算入 uplink。
- `flit.h` 对 `mac2port` 做边界检查，unknown MAC suffix 可路由到 first uplink，避免越界和 `rand()%0`。

当前状态：

- `MAC2PORT_SIZE`、`mac2port_oob`、`NUMUPLINKS` guard 仍在。

判断：这类 1bp 网络坑当前没有回退。

### 3. IceNet checksum / DMA 判别与当前软件形态

记录：

- `change_records/20260429T235921Z.md`
- `debug_records/20260430T035726Z.md`
- `debug_records/20260430T162000Z.md`

改动：

- 临时关闭 `CONFIG_ICENET_CHECKSUM`，排除 checksum offload 作为唯一根因。
- 后续 driver 走 coherent bounce / SG disabled 路线，workflow freshness 期望字符串是：

  ```text
  IceNet TX coherent bounce enabled; checksum offload and SG disabled
  ```

当前状态：

- `software/firemarshal/boards/firechip/drivers/icenet-driver/icenet.c` 仍是
  `#undef CONFIG_ICENET_CHECKSUM`。
- workflow 仍检查 coherent bounce / checksum offload disabled 字符串。

判断：当前保留了旧 1bp 成功路径使用的软件网络形态。

### 4. SimpleNIC empty-round throttling

记录：

- `debug_records/20260430T225901Z.md`
- `change_records/20260501T023200Z.md`
- `debug_records/20260501T015352Z.md`
- `debug_records/20260501T022742Z.md`

改动：

- 新增 `+simplenic-empty-switch-poll-interval=<N>`。
- relaxed 模式下 target->host 无 payload 时，不再每个 empty round 都清 915 个 bigtoken header 并发布 empty shmem round。
- 当前 gdbserver smoke 采用 `+simplenic-empty-switch-poll-interval=1024`。

当前状态：

- `simplenic.cc` / `simplenic.h` 仍保留 empty-round throttling 和
  `empty_rounds_skipped_switch`。
- runtime 仍带 `+simplenic-empty-switch-poll-interval=1024`。

判断：这类 1bp boot 变慢/host driver 热点坑当前没有回退。

### 5. GDB 使用流程修复

记录：

- `debug_records/20260501T045108Z.md`
- `debug_records/20260502T123027Z.md`

改动/约束：

- `gdbserver --once` 的第一条 TCP 连接必须是真实 `gdb target remote`。
- 不使用 `nc` / `telnet` 探测端口。
- `run_remote_gdbserver_software_expect_smoke.sh` 默认不注入静态 ARP / host ARP。
- Tcl 中 `$pc` 已转义为 `\$pc`。
- software-breakpoint-only 回归不依赖 `hbreak` / watchpoint。

当前状态：

- expect 脚本仍写明不探端口，默认 ARP 注入关闭。
- `x/12i \$pc` 等转义保留。

判断：GDB 脚本层旧坑当前没有回退。

### 6. 当前重新出现的 1bp 路径偏离

记录：

- `debug_records/20260502T123027Z.md`
- `change_records/20260504T022833Z.md`

05-02 恢复 old AGFI 时，runtime 显式使用：

```text
+cpu-managed-stream-debug=0
```

当前 runtime 变成：

```text
+simplenic-token-debug=32 +cpu-managed-stream-debug=1
```

这是 05-04 为“下一轮失败现场低容量观测”打开的，不是 05-01/05-02 clean regression 形态。

判断：如果目标是复测旧 1bp 成功能力矩阵，这个 runtime 是偏离项；如果目标是拿失败现场，它是有意的观测开关。

## B. 8BP 调试过程中改过什么

### 1. 真正的 8BP 改动

记录：

- `change_records/20260501T161752Z.md`
- `debug_records/20260501T161752Z.md`
- `debug_records/20260502T054938Z.md`

改动：

- 新增：

  ```scala
  class FireSimRocketNICNoTrace8BPConfig extends Config(
    new freechips.rocketchip.rocket.WithNBreakpoints(8) ++
    new FireSimRocketNICNoTraceConfig)
  ```

- 新增/使用 8BP build recipes、hwdb、runtime。
- 新增 `run_remote_gdbserver_8bp_smoke.sh`。

结果：

- 8BP AGFI 多轮出现 timing / freshness mismatch / prelaunch 或 cycle0 问题。
- `gdbserver`/Linux/RISC-V 实测不支持当前链路下的 hardware watchpoint，pipeline-runtime 主调试能力并不需要 8BP。

当前状态：

- 8BP 配置仍在，但默认 workflow/build recipe 已经不是 8BP。
- 当前 `config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml` 使用
  `TARGET_CONFIG: FireSimRocketNICNoTraceConfig`。

判断：8BP 未删除，但已隔离；不会影响默认 1bp flow，除非显式使用 8BP YAML / config。

### 2. 8BP 期间混入的 NIC 观测改动

记录：

- `change_records/20260501T141954Z.md`
- `change_records/20260501T161752Z.md`
- `debug_records/20260501T141954Z.md`

改动：

- 在 8BP build 正在跑时，又给 SimpleNIC target->host 路径加了
  `nicbig_ntht_first_*` / `nicbig_pcie_first_*` first16 snapshot。
- 随后出现 driver/AGFI mismatch：AGFI 是旧 RTL，driver 是新寄存器表。

当前状态：

- 当前源码没有 `to_host_first` / `nicbig_ntht_first` / `nicbig_pcie_first` / `FIRST16`。

判断：这批 8BP 期间的 first16 观测已经撤掉。

## C. 回到 1bp 后改过什么

### 1. 05-02 回到普通 NoTrace 后，先清理 OCL debug

记录：

- `debug_records/20260502T164142Z.md`
- `change_records/20260502T164947Z.md`

改动：

- 移除 `SimulationMaster HEALTH_*`、Clock/PeekPoke synthetic debug、OCL ctrl sampling。
- 删除 `firesim_top.cc` 中已不存在的 `log_runtime_state` / `log_debug_state` 调用。

判断：

- 这是合理回退。当前未见 `HEALTH_` / `FIRESIM MASTER DEBUG` / `FIRESIM CLOCK DEBUG` /
  `FIRESIM PEEKPOKE DEBUG` 作为当前核心路径。

### 2. 05-02/05-03 试图恢复旧通过 payload snapshot

记录：

- `debug_records/20260502T233311Z.md`
- `debug_records/20260502T234445Z.md`
- `debug_records/20260503T032845Z.md`
- `debug_records/20260503T035648Z.md`
- `debug_records/20260503T051319Z.md`

过程：

- 先恢复 04-30 通过版中存在的 payload snapshot：
  `bigtoken_current_slot_data`、`bigtoken_last_latched_word*`、
  `nicbig_last_pcie_out_word*`、`to_host_hist*`。
- 后来发现 first16 是过头观测，删除。
- 后来发现 PCIS write-order debug/status 残留，删除。
- 后来发现 `fromHostAllReady/configChannelReady` debug 聚合不符合 04-30 通过版，删除。
- 后来发现 snapshot CR 顺序不符合 04-30 通过版，调整顺序。

结果：

- 这条“恢复旧形态”没有最终回到稳定通过。后续又出现 cycle0 和 payload corruption。

### 3. 05-03 之后从“旧 snapshot 形态”转向“真实数据通路修复”

记录：

- `change_records/20260503T140301Z.md`
- `debug_records/20260503T191457Z.md`
- `change_records/20260503T191457Z.md`
- `debug_records/20260503T232710Z.md`
- `change_records/20260503T232710Z.md`

改动：

- 删除 payload-history OCL taps，改成 min-observability marker。
- `NICTokenToBigTokenAdapter` 输出端加 `pcieOutQ = Queue(UInt(512.W), 2)`。
- `StreamWidthAdapter` wide-to-narrow 路径加 `wide_data` / `wide_valid` latch。
- 增加 `pcie_out_enq/deq` snapshot。

结果：

- `pcieOutQ` 版本能走到 Linux/gdbserver，但 GDB RSP payload 固定位 bit clear 仍存在。
- stream latch 第一版引入 prefetch deadlock：adapter 把 token 从 FIFO count 里藏起来，host 不读，启动前卡住。

### 4. 05-04 修复 stream latch prefetch，再改 target-cycle readiness

记录：

- `debug_records/20260504T022833Z.md`
- `change_records/20260504T022833Z.md`
- `debug_records/20260504T060924Z.md`
- `change_records/20260504T060924Z.md`

改动：

- `CPUManagedStreamEngine.elaborateToHostCPUStream` 中增加：

  ```scala
  val readRequestActive = grant && axi4.ar.valid
  ser_des.io.wide.in.valid := outgoingQueueIO.deq.valid && readRequestActive
  outgoingQueueIO.deq.ready := ser_des.io.wide.in.ready && readRequestActive
  ```

- `SimpleNICBridge` 的 target-cycle readiness 改成只依赖实际 NIC input channel：

  ```scala
  fromHostChannelReady = hPort.nicIn.ready
  targetCycleReady = toHostReadyDrive && fromHostChannelReady
  targetBlockedFromHostChannels = fromHostTokenAvailable && !hPort.nicIn.ready
  ```

判断：

- 这些改动已经进了本地 generated collateral 和 05-04 06:11 build 输入。
- 但 05-04 06:11 build 的 AFI creation failed，所以没有可用 AGFI 验证它们是否能恢复 remote GDB。

### 5. 05-04 06:38/07:33/10:12 又追加观测面

记录：

- `debug_records/20260504T063838Z.md`
- `change_records/20260504T063838Z.md`
- `debug_records/20260504T073301Z.md`
- `change_records/20260504T073301Z.md`
- `debug_records/20260504T101241Z.md`
- `change_records/20260504T101241Z.md`

改动：

- `minobs_build_marker` 从 `0x05040001` 到 `0x05040002`，再到 `0x05040003`。
- 新增大量 SimpleNIC CSR/MCR 观测。
- 同时新增 build-before-freeze 约束，明确这些观测不属于已启动的
  `rocket-singlecore-nic-readgatefix-buildbitstream-20260504-0616`。

当前静态状态：

- 当前源码是 `0x05040003`。
- 当前 `sim/generated-src` 和 F2 CL 目录里的 generated RTL 仍是旧 SHA：

  ```text
  FireSim-generated.sv      5a53b2216938aa55b12aa21e2b8e9e2f0ed582a4161f941421464b056b954f07
  FireSim-generated.const.h 296da51367143a0d7508293e85acf05ebc6595de491254a54206f9db129e61a2
  FireSim-f2                89869f6a75ca295150026b08d525071be5b7dd8bf9d47cbb2a4a84e04b4556e0
  ```

  生成物里 marker 是 `32'h5030003`，不是当前源码的 `0x05040003`。

判断：

- 当前源码和 generated collateral 已经不完全对应。
- 这不一定是 bug，因为 10:12 记录说这些观测是下一轮候选；但下一轮 build 前必须重新 `replace-rtl` 并做 freshness gate，不能拿现有 generated-src 当当前源码证明。
