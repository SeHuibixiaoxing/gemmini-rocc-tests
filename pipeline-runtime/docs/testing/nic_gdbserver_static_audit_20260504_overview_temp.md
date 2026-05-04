# 2026-05-04 single-core NIC gdbserver static audit overview

类别：`gdbserver` / single-core NIC / 1bp rollback / static audit

## 背景

本轮只做静态排查，不启动新的 F2 run。入口约束来自：

- `pipeline-runtime/README.md`：当前调试遵循 static-first，遇到卡点先读 artifact / debug_records / change_records，再决定是否新跑。
- `docs/constraints/hard_constraints.md`：FireSim run 必须走 manager workflow；`buildbitstream` 前必须冻结源码、观测点和 freshness gate；构建启动后不能继续把后补硬件观测点算进该 AGFI。
- `debug_records/20260501T045108Z.md` 与 `debug_records/20260502T123027Z.md`：旧通过 AGFI `agfi-0079cbbca617eca4e` 是当前判断基线。

旧通过基线：

```text
AGFI: agfi-0079cbbca617eca4e
AFI:  afi-0ee7774f829acd4de
build: sims/firesim/deploy/results-build/2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz/
config: FireSimRocketNICNoTraceConfig + BaseF2Config
type: single-core Rocket + NIC + no TraceIO + 30MHz
not: FireSimRocketNICNoTrace8BPConfig
```

该基线已验证：

- `target remote`
- 多个 software breakpoint
- `continue` / `next`
- `info threads` / `thread apply all bt`
- 寄存器、反汇编、变量、内存读写
- 线程切换
- `Ctrl-C` 抢回控制
- `detach`

未通过的是 hardware watchpoint；因此后续 `8BP` 不是当前 pipeline-runtime 卡死调试的必要前提。

## 总结结论

1. 旧 1bp 成功路径的核心软件/bridge 修复多数仍在当前源码中。

   当前仍能静态看到：

   - F2 BAR4 以 non-WC 方式 attach：
     `sims/firesim/sim/midas/src/main/cc/simif_f2.cc`
     中 `fpga_pci_attach(... APP_PF_BAR4, 0, ...)` 和
     `Attached to BAR4 (PCIS, non-WC)`。
   - F2 PCIS 512->64 read lane mask 仍是单 lane，写通道 `s_axi_wready`
     已回到 `(wr_aw_active || wr_aw_fire)` 约束。
   - switch `MAC2PORT_SIZE` / root `SSHPort` / `mac2port_oob` 防越界修复仍在。
   - SimpleNIC empty-round throttling 仍在，runtime 仍带
     `+simplenic-empty-switch-poll-interval=1024`。
   - software-breakpoint expect 脚本默认不使用 `nc` / `telnet` 探端口，第一条 TCP
     连接仍是真实 GDB。

2. 当前 runtime 又偏离了 05-02 成功恢复路径。

   `sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_relaxed_noprint_preserve_30mhz.yaml`
   当前是：

   ```text
   +simplenic-token-debug=32 +cpu-managed-stream-debug=1
   ```

   但 `20260502T123027Z.md` 明确记录：old AGFI 恢复 software-breakpoint 路径时需要显式
   `+cpu-managed-stream-debug=0`，避免 RSP 初始握手被 `CPU_STREAM DEBUG` 刷屏拖慢。
   所以这份 runtime 适合“下一轮失败现场取证”，不适合作为干净复测 05-01/05-02 成功路径的 control。

3. 8BP 没有被完全删除，但已经和当前默认 1bp flow 隔离。

   `FireSimRocketNICNoTrace8BPConfig`、8BP hwdb/build recipes/runtime 和
   `run_remote_gdbserver_8bp_smoke.sh` 仍留在仓库中；它们只有显式选择
   `FireSimRocketNICNoTrace8BPConfig` 或 8BP YAML 时才参与。当前默认 workflow / build recipe
   使用的是 `FireSimRocketNICNoTraceConfig`，Rocket 默认 `nBreakpoints=1`。

4. 8BP 调试期间混入的 first16 / payload-heavy / PCIS debug 观测面经历过多轮回退。

   当前源码不再有 `to_host_first`、`nicbig_ntht_first`、`nicbig_pcie_first`、`FIRST16`
   这批 05-01/05-02 first16 观测名；也没有 `write_w_before_aw` /
   `write_wlast_mismatch` 等 PCIS 写顺序 debug marker。

   但是当前源码又加入了新的 05-03/05-04 观测面和功能改动：

   - `NICTokenToBigTokenAdapter` 前的 `pcieOutQ`
   - `CPUManagedStreamEngine` wide-to-narrow latch
   - to-host dequeue `readRequestActive` gate
   - `SimpleNICBridge` target-cycle readiness 改成只看 `hPort.nicIn.ready`
   - `minobs_build_marker = 0x05040003` 的大规模观测面

   这些不是 04-30 通过 AGFI 的形态，属于“当前回到 1bp 后的新路线”，不是纯回退。

5. 当前真正卡点不是 remote GDB，也不是 8BP，而是没有可用的新 1bp AGFI。

   最新尝试构建：

   ```text
   results-build/2026-05-04--06-11-50-firesim_rocket_singlecore_nic_notrace_30mhz/
   AFI:  afi-0bdec41c0172e3a41
   AGFI: agfi-0a5630fba817a9c81
   ```

   `AGFI_INFO` 中状态为：

   ```text
   failed
   UNKNOWN_BITSTREAM_GENERATE_ERROR
   ```

   build log 也显示 FireSim buildbitstream 最终失败。当前
   `config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml` 和 built-hwdb entry 仍指向：

   ```text
   agfi-05336229a1326f9ff
   ```

   而 `20260504T060924Z.md` 已明确：该 AGFI 是 stale negative-control，不包含 06:09 的
   `fromHostChannelReady = hPort.nicIn.ready` 修复，运行时停在 pre-Linux / target cycle 0。

## 当前状态一句话

当前不是“8BP 还在导致 1bp 失败”。当前是：1bp 默认配置已恢复为非 8BP，但源码已经走到新的
`readRequestActive + pcieOutQ + 0x05040003 observability` 路线；最新含 06:09 功能修复的
bitstream 生成失败，hwdb 仍绑定到已知 stale/failing AGFI，因此没有一个可用于验证当前源码的
fresh 1bp AGFI。

## 建议的下一步静态纪律

下一次 build 前不要继续追加硬件观测点。先冻结一个明确版本：

- 要么回到最小 `readRequestActive + nicIn-only targetCycleReady`，减少 `0x05040003` 观测面；
- 要么承认需要 `0x05040003`，先跑 `replace-rtl` 并做 attach/struct/order/freshness gate。

然后再启动新的 `FireSimRocketNICNoTraceConfig` build。成功拿到 available AGFI 之后，再更新 hwdb 并跑 software-breakpoint-only gdbserver 回归。
