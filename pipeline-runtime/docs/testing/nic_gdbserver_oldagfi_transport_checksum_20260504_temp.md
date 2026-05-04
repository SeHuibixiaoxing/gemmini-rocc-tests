# 2026-05-04 old AGFI clean1bp transport checkpoint

类别：`gdbserver` / old AGFI control / transport blocker / temporary note

## 1. 这轮对照在验证什么

目标不是验证新的 1bp RTL，而是先回答一个更基础的问题：

```text
当前 repo/runtime/rootfs/switch 组合，是否还能用 05-01/05-02 已知通过的 old AGFI
跑通同一套 software-breakpoint remote gdbserver 能力测试。
```

对照使用：

- AGFI：`agfi-0079cbbca617eca4e`
- `FireSim-f2` sha256：
  `01e76334e3c0e1d2d8d8d1f570285abd80ab480d62a82930c5d6d3f87fd37b8b`
- driver bundle sha256：
  `f902490ed206831fd195ee3844717b4259972e9c3a0d31e97d562ed3eeee66a7`
- switch binary sha256：
  `8d9b544cab8b128d851dcd87235686c9d56994acb6ddf56ab38f53934fde3e9a`
- rootfs image sha256：
  `b67b5d39171cf373af492ad20f783f5a95d217a1cb0d9e0c846cf78d968f0e4c`
- workload bootbinary sha256：
  `d670325990ab4885d2fd38ec58798d58c00a985b57d45be0962cf3a0ead09137`

现场：

```text
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/artifacts/
  20260504T141200Z_oldagfi_control_tcp_checksum/
```

## 2. 与 05-02 成功记录的关系

05-02 通过记录：

- `debug_records/20260502T123027Z.md`
- `tmp/firesim-aws-f2/gdbserver-tests/remote-swbreak-recover2-20260502T122754Z-192_168_1_221/remote_gdb_multibp.expect.log`

05-02 已验证：

- `target remote` 成功停在 `_start`
- 多个 software breakpoint 可设置并命中
- `continue` / `next`
- `info threads`
- `thread apply all bt`
- 寄存器读取
- 反汇编
- 变量/内存读写
- 线程切换
- `Ctrl-C`
- `detach`

05-02 的末尾失败只是 expect 脚本里 Tcl `$pc` 没有转义；这不是 NIC / gdbserver 失败。

本轮 05-04 对照复用了同一个 old AGFI 和同 SHA `FireSim-f2`，但不代表完全复用了 05-02 环境：

- switch binary 是当前 infrasetup 产物；
- rootfs / bootbinary 是当前 FireMarshal 产物；
- guest IceNet driver 是当前 firemarshal image 里的版本；
- host switch/simplenic 代码经过多轮调试改动。

因此 old AGFI 能否通过，是“当前外部软件栈是否仍兼容 old AGFI”的对照，不是单纯的 AGFI 自检。

## 3. 05-04 old AGFI 对照现象

UART 证明 target 启动正常：

```text
IceNet TX coherent bounce enabled; checksum offload and SG disabled
Registered IceNet NIC 00:12:6d:00:00:02
[gdbserver] phase=prelaunch ... endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening ... endpoint=172.16.0.2:2345 pid=112
```

GDB 失败点：

```text
target remote :32346
Remote debugging using :32346
Remote communication error.  Target disconnected: Connection reset by peer.
```

SSH tunnel 同时记录：

```text
channel 2: open failed: connect failed: Connection timed out
```

UART 中没有看到 05-02 那种：

```text
Remote debugging from host 172.16.0.1
[gdbserver] phase=exit ...
```

这说明当前失败早于 GDB 能力矩阵本身：还没有稳定进入 RSP 会话。

## 4. switchlog 中的 TCP 线索

switchlog 证明 host->guest SYN 进入 switch，guest->host SYN-ACK 也从 target 出来。

SYN-ACK 代表性事件：

```text
event 8 tap_send bytes:
8e 6b 35 04 00 00 00 12 6d 00 00 02 08 00 45 00
00 3c 00 00 40 00 40 06 e2 98 ac 10 00 02 ac 10
00 01 09 29 de 40 83 99 bb 49 b7 ff c8 92 a0 12
7c 70 0d d8 00 00 02 04 05 b4 04 02 08 0a 08 ba
7b 93 84 f0 b5 64 01 03 03 07 00 00 00 00 00 00
```

解析：

- IPv4 total length：`0x003c`，即 60 字节 IP 包；
- Ethernet 实际有效长度应为 `14 + 60 = 74` 字节；
- switch 当前打印/写 TAP 的长度是 78 字节，因为它按 flit 对齐后扣 `NET_IP_ALIGN`；
- IP checksum 对 05-04 记录中的 SYN-ACK 是自洽的；
- TCP checksum 用 IPv4 total length 约束计算时不自洽。

已观察到的 TCP checksum 差异：

| switch event | packet TCP checksum | recomputed checksum | verify residue |
| --- | --- | --- | --- |
| 8 | `0x0dd8` | `0x0dda` | `0x0002` |
| 9 | `0x0dd6` | `0x0dd8` | `0x0002` |
| 10 | `0x09e3` | `0x09ed` | `0x000a` |

注意：多出的 TAP padding 本身不是充分根因。TCP checksum 应按 IPv4 total length 覆盖 TCP segment，不覆盖 Ethernet padding；即使忽略 padding，05-04 抓到的 TCP checksum 仍不匹配。

## 5. 当前判断边界

能确定的结论：

- old AGFI 仍能启动 Linux、IceNet、gdbserver；
- 当前失败不是 8BP 数量问题；
- 当前失败不是 GDB 脚本 `$pc` 转义问题；
- 当前失败不是 `gdbserver --once` 被 `nc`/`telnet` 探测消耗；
- 当前失败卡在 TCP/RSP 会话建立前后，重点在 host/switch/rootfs/guest driver 这些 AGFI 外部变量。

不能直接下的结论：

- 不能说 old AGFI 失效；05-04 没有复用完整 05-02 软件环境；
- 不能说新 1bp RTL fix 无效；新 fixed source 尚未产出 usable AGFI；
- 不能把 TAP flit padding 直接当根因；它最多是需要清理/排除的传输变量。

## 6. 下一步优先级

1. 先把 old AGFI 对照恢复到 05-02 语义。
   重点比对/固定 switch binary、rootfs/bootbinary、guest IceNet driver、expect 第一连接路径。

2. 若 old AGFI + 当前 rootfs 仍失败，优先做 host/switch 侧最小实验：
   - 在 `SSHPort::send()` 按 Ethernet/IP 实际长度裁剪 TAP write；
   - 增加轻量 TCP checksum 观测，确认 corruption 是 target 产出前、switch 重组时、还是 TAP/host 内核之后出现。

3. 新 1bp AGFI 只在 build 成功、`AGFI_INFO` 变为可用后进入同一套 clean smoke；不要用 stale negative-control AGFI 判断源码修复。

4. expect 脚本已经补上 `target remote` reset/断连失败判定，避免再出现“已经断线但脚本继续下断点”的假阳性。

