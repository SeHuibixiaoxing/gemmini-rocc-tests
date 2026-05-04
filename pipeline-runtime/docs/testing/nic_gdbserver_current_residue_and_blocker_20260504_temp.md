# 2026-05-04 current residue and blocker audit

类别：`gdbserver` / residue audit / current blocker

## 1. 当前默认到底是不是 1bp

是。默认 build recipe：

```text
sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml
TARGET_CONFIG: FireSimRocketNICNoTraceConfig
```

`FireSimRocketNICNoTraceConfig` 没有加 `WithNBreakpoints(8)`。

8BP 配置仍存在：

```text
FireSimRocketNICNoTrace8BPConfig
config_build_recipes_f2_rocket_singlecore_nic_notrace_8bp_30mhz.yaml
config_hwdb_f2_rocket_singlecore_nic_notrace_8bp_30mhz.yaml
config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_8bp_*.yaml
run_remote_gdbserver_8bp_smoke.sh
```

但这些不是当前 `rocket_singlecore_nic_gdbserver_smoke_workflow.sh` 的默认入口。

结论：当前 1bp 失败不能直接归因于 8BP config 仍在仓库里。

## 2. 旧 1bp 坑当前是否还在

### 已经修掉且当前仍在的坑

| 旧坑 | 当前静态状态 |
| --- | --- |
| BAR4 WC mmap 访问 CPU-managed stream FIFO | `simif_f2.cc` 仍用 `APP_PF_BAR4, 0`，打印 `non-WC` |
| F2 PCIS read 一次 `peek64` 消费 8 个 destructive FIFO beat | `read_lane_mask()` 当前只返回地址 lane |
| PCIS W-before-AW 语义漂移 | 当前 `s_axi_wready` 需要 `(wr_aw_active || wr_aw_fire)` |
| switch root `SSHPort` / `mac2port` 越界 | `MAC2PORT_SIZE`、`mac2port_oob`、`NUMUPLINKS` guard 仍在 |
| SimpleNIC empty round 清大 buffer / 唤醒 switch 过频 | `+simplenic-empty-switch-poll-interval=1024` 与 throttling 仍在 |
| `gdbserver --once` 被 nc/telnet/端口探测消耗 | software expect 脚本明确不探端口，第一连接为 GDB |
| `$pc` Tcl 转义 | expect 脚本使用 `\$pc` |

### 当前又出现风险的旧坑

05-02 成功恢复路径强调：

```text
+cpu-managed-stream-debug=0
```

当前 runtime 是：

```text
+simplenic-token-debug=32 +cpu-managed-stream-debug=1
```

这会改变初始 RSP/boot 阶段的 host-side 输出压力。它不是硬件语义，但它是 05-02 已钉死过的回归变量。

建议：

- clean regression runtime：回到 `+simplenic-token-debug=0 +cpu-managed-stream-debug=0`。
- capture runtime：可以单独保留当前 debug plusarg，但不要把它和 clean regression 结论混用。

## 3. 8BP 改动是否已经撤回

### 已隔离

- 默认 workflow 不使用 8BP。
- 默认 hwdb/build recipe 不使用 8BP。
- 当前 1bp build target 是 `FireSimRocketNICNoTraceConfig`。

### 未删除但不生效

- `FireSimRocketNICNoTrace8BPConfig` 仍存在。
- 8BP YAML 和 `run_remote_gdbserver_8bp_smoke.sh` 仍存在。

这不是当前默认 1bp 卡点，但如果目标是彻底清理仓库，可以后续单独删；本轮不建议把删除 8BP 文件当成修 1bp 的必要动作。

### 8BP 期间的观测面残留

已撤掉：

```text
to_host_first
nicbig_ntht_first
nicbig_pcie_first
FIRST16
write_w_before_aw
write_aw_wait_w
write_w_wait_aw
write_simul_aw_w
write_wlast_mismatch
```

当前源码没有这些 marker。

但当前存在新的 05-03/05-04 观测面：

```text
pcieOutQ
wide_data / wide_valid
readRequestActive
minobs_build_marker = 0x05040003
大量 SimpleNIC progress/block/queue/adapter CSR
```

这些不是 8BP 残留，而是回到 1bp 后继续调 target->host corruption / cycle0 的新改动。

## 4. 当前回到 1bp 后的改动是否真的有效

需要分成“源码有效”“generated collateral 有效”“AGFI 有效”三层。

### 源码层

当前源码包含：

- `CPUManagedStreamEngine.scala` 中的 `readRequestActive` gate。
- `SimpleNICBridge.scala` 中的 `fromHostChannelReady = hPort.nicIn.ready`。
- `SimpleNICBridge.scala` 中的 `pcieOutQ`。
- `SimpleNICBridge.scala` 中的 `minObsBuildMarker = 0x05040003`。

源码层存在这些改动。

### generated collateral 层

当前本地 generated collateral 是：

```text
FireSim-generated.sv      5a53b2216938aa55b12aa21e2b8e9e2f0ed582a4161f941421464b056b954f07
FireSim-generated.const.h 296da51367143a0d7508293e85acf05ebc6595de491254a54206f9db129e61a2
FireSim-f2                89869f6a75ca295150026b08d525071be5b7dd8bf9d47cbb2a4a84e04b4556e0
```

其中已经包含：

- `targetCycleReady = toHostReadyDrive & hPort_nicIn_ready`
- `readRequestActive = grant & ar_valid`
- to-host FIFO dequeue gated by `readRequestActive`

但 generated RTL 里的 marker 是：

```text
minObsBuildMarker <= 32'h5030003
```

而当前源码是：

```text
0x05040003
```

结论：

- 06:09 的功能修复已进入当前 generated collateral。
- 10:12 的 `0x05040003` 观测扩展没有进入当前 generated collateral。
- 下一轮 build 前必须重新 `replace-rtl`，不能把当前 generated-src 当作当前源码的完整 freshness 证据。

### AGFI 层

最新包含 06:09 功能修复的 build 尝试：

```text
results-build/2026-05-04--06-11-50-firesim_rocket_singlecore_nic_notrace_30mhz/
AFI:  afi-0bdec41c0172e3a41
AGFI: agfi-0a5630fba817a9c81
```

`AGFI_INFO`：

```text
State.Code: failed
Message: UNKNOWN_BITSTREAM_GENERATE_ERROR
```

因此这些改动没有形成可用于 F2 run 的 AGFI。

当前 hwdb：

```text
config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml
agfi: agfi-05336229a1326f9ff
```

而 `debug_records/20260504T060924Z.md` 已写明该 AGFI 是 stale negative-control，不含当前 source fix。

结论：当前回到 1bp 后的功能修复尚未被可用 AGFI 验证。最多只能说它们已通过源码/生成物静态 gate，不能说“FPGA 上有效”。

## 5. 当前卡点在哪里

当前卡点分两层。

### 最近一次运行卡点

最近一次 F2 run 使用 stale `agfi-05336229a1326f9ff`，现象：

```text
debug_cycle = 0
target_cycle_ready = 0
all relevant ready/valid signals = 0
no OpenSBI/Linux UART
```

记录：

- `debug_records/20260504T060924Z.md`

该 run 的结论是：stale AGFI 不含 06:09 source fix，不能继续当当前源码判据。

### 当前工程卡点

最新 fixed-source build：

```text
2026-05-04--06-11-50
```

没有得到 usable AGFI。AFI/AGFI 创建失败：

```text
afi-0bdec41c0172e3a41
agfi-0a5630fba817a9c81
UNKNOWN_BITSTREAM_GENERATE_ERROR
```

timing report 中存在大量 violated slack，例如 post-route `Slack (VIOLATED) : -3.190ns`。

所以当前工程卡点是：

```text
当前源码/生成物尚未产生 available 1bp AGFI
```

不是：

```text
remote gdbserver 软件断点失败
8BP 仍在影响 1bp
old AGFI 的 NIC 不通
```

## 6. 建议的下一步

先做一次“构建前冻结”，不要继续追加观测点。

候选路线：

1. 最小功能路线：
   - 保留 `readRequestActive`
   - 保留 `targetCycleReady = toHostReadyDrive & hPort.nicIn.ready`
   - 保留 `pcieOutQ` 或评估是否保留
   - 缩减 `0x05040003` 大观测面，降低 timing/AFI 失败风险

2. 观测完整路线：
   - 接受 `0x05040003`
   - 先 `replace-rtl`
   - 确认 generated RTL / const.h / driver SHA 与当前源码一致
   - 再 buildbitstream

无论选哪条，build 成功后：

- 更新 `config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml`。
- 用 clean software-breakpoint runtime 先跑 05-01/05-02 能力矩阵。
- 如果 clean run 失败，再用 debug plusarg runtime 抓现场。
