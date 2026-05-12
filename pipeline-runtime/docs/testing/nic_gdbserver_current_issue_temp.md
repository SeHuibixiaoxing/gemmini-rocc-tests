# NIC 当前问题与 gdbserver 使用临时说明

更新时间：`2026-04-30 04:55 UTC`

本文面向不熟悉 FireSim/NIC/gdbserver 的读者，解释当前调试线到底遇到了什么问题、已经证明了什么、以及后续如何使用 remote `gdbserver` 调试 target 程序。

## 1. 当前在调试什么

当前对象是一个最小硬件/软件组合：

- 硬件：single-core Rocket + IceNet NIC
- 平台：AWS F2 / FireSim
- workload：`rocket-singlecore-nic-gdbserver-smoke`
- guest OS：Buildroot Linux
- guest IP：通常是 `172.16.0.2`
- host tap IP：通常是 `172.16.0.1`
- gdbserver 端口：`2345`

目标不是跑完整 `pipeline-runtime`，而是先证明：

1. target Linux 能 boot。
2. IceNet NIC 能收包和发包。
3. host 能通过 NIC 连接 guest 上的 `gdbserver`。
4. gdb 能断到 target 用户程序，比如 `main()`。

这个 smoke 路线跑通后，后续 `pipeline-runtime` 程序才有条件走 remote gdb 调试。

## 2. FireSim 里 host 和 target 怎么通信

在 FireSim FPGA 模式下，target SoC 不是直接跑在真实网卡上，而是通过 FireSim bridge 和 host 进程交互。

网络路径可以简化理解为：

```text
host 上的 gdb / ping / tcpdump
  <-> run host 的 tap0，通常是 172.16.0.1/16
  <-> FireSim switch0
  <-> FireSim-f2 进程里的 SimpleNIC host bridge
  <-> FPGA 上的 SimpleNIC bridge token 接口
  <-> target SoC 内的 IceNet NIC
  <-> guest Linux 网络栈
  <-> guest 用户态 gdbserver
```

也就是说，host 上的 `gdb` 并不是直接连 FPGA，而是先连 run host 的 `tap0` 网络，再经过 FireSim switch 和 SimpleNIC bridge 进入 target Linux。

## 3. 之前的主要问题

NIC 调试线不是只遇到一个问题，而是逐层推进过来的。已经处理或排除过的包括：

- Linux boot 已经能进入。
- UART / TSI 原先是健康的，不是当前主因。
- host-target 双向通信已经建立过。
- FireSim switch 的 `mac2port` / `SSHPort` crash 修过。
- TAP 侧 crash 修过。
- F2 BAR4 non-WC attach 和 64-bit `peek64/poke64` 修过。
- SimpleNIC ready/valid 早期卡死和 leaf channel 同步问题已经逐步推进过。
- IceNet checksum offload 关闭后，payload corruption 仍复现，所以 checksum offload 不是最终根因。
- BIGToken host parser、StreamWidthAdapter lane order、switch/TAP egress 路径已经被排除。

最新真正钉住的问题是：

```text
target -> host 方向的以太网 payload 会被改坏。
```

典型现象是 host 发 `ping -p ff`，target 回 echo reply 时，payload 里本应是 `0xff` 的字节会变成 `0xfe` 或 `0xfd`，也就是某些 bit 被清掉。

关键点：checksum 字段看起来像是根据 CPU 视角的正确数据算出来的，但线上真正发出的 payload 被改坏。这说明问题很像“设备 DMA 读到的内存内容”和“CPU 看到的内存内容”不一致。

## 4. 已钉死的根因

当前根因判断是：

```text
IceNet Linux driver 在 RISC-V non-coherent DMA 环境下绕过了 Linux DMA API。
```

证据链如下：

1. 生成 DTS 里的 `ice-nic@10016000` 节点没有 `dma-coherent`。
2. Linux 配置启用了 non-coherent DMA：
   - `CONFIG_RISCV_DMA_NONCOHERENT=y`
   - `CONFIG_ARCH_HAS_SYNC_DMA_FOR_DEVICE=y`
   - `CONFIG_ARCH_HAS_SYNC_DMA_FOR_CPU=y`
   - `CONFIG_SWIOTLB=y`
3. 原 IceNet driver 直接用：
   - `virt_to_phys(skb->data)`
   - `page_to_phys(...)`
4. 原 driver 没有在 TX/RX 路径使用：
   - `dma_map_single`
   - `skb_frag_dma_map`
   - `dma_unmap_single`
   - `dma_unmap_page`

在 non-coherent DMA 平台上，CPU cache 和设备 DMA 视角不会自动一致。driver 必须用 Linux DMA API 做 cache sync / bounce / mapping。否则 CPU 写完 skb 后，设备可能读到旧 cacheline 或没有同步过的内存。这正好解释 target 发包 payload 被部分改坏。

## 5. 本轮修复了什么

修改文件：

```text
software/firemarshal/boards/firechip/drivers/icenet-driver/icenet.c
```

核心修复：

- TX linear skb 使用 `dma_map_single(..., DMA_TO_DEVICE)`。
- TX skb fragments 使用 `skb_frag_dma_map(..., DMA_TO_DEVICE)`。
- TX completion 里用 `dma_unmap_single` / `dma_unmap_page` 释放映射。
- RX buffer 发布给设备前使用 `dma_map_single(..., DMA_FROM_DEVICE)`。
- RX completion 中先 `dma_unmap_single(..., DMA_FROM_DEVICE)`，再交给 Linux 网络栈。
- TX 改成两阶段提交：先 map 所有 segment，全部成功后才写 `ICENET_SEND_REQ`，避免 map 失败时已经向硬件提交半包。
- 为减少变量，本轮临时关闭 checksum offload 和 scatter-gather。

新 driver 启动时会打印：

```text
IceNet DMA API mappings enabled; checksum offload and SG disabled
```

这是确认当前镜像确实带了本轮修复的重要标记。

## 6. 本轮 F2 实测结论

本轮使用：

```text
AGFI: agfi-019e0b22099a5214b
```

已经确认：

- guest 成功 boot。
- 新 driver 被加载。
- `Starting network: OK`。
- host 能 ping 到 `172.16.0.2`。
- remote gdb 可以连接 guest `gdbserver`。

ICMP payload 测试：

```bash
ping -c 2 172.16.0.2
ping -c 2 -s 16 -p ff 172.16.0.2
ping -c 2 -s 56 -p ff 172.16.0.2
ping -c 2 -s 80 -p ff 172.16.0.2
ping -c 2 -s 80 -p 00 172.16.0.2
ping -c 2 -s 80 -p 0123456789abcdef 172.16.0.2
```

结果全部是：

```text
2 packets transmitted, 2 received, 0% packet loss
```

pcap 中 target 回包 payload 保持正确：

- `0xff` payload 没有再出现 `ff -> fe` 或 `ff -> fd`。
- `0x00` payload 保持全 0。
- `0123456789abcdef` payload 保持原 pattern。

结论：

```text
IceNet DMA API 修复后，之前稳定复现的 target-to-host payload corruption 没有复现。
```

因此这个 corruption 当前不需要继续构建新 bitstream 来验证。

## 7. 当前仍要注意的问题

本轮 `runworkload` 没有自然结束，原因不是 NIC payload corruption 复现，而是手工 gdb 操作方式造成的：

1. gdb 成功连上。
2. gdb 成功断到 `main()`。
3. 我执行了 `detach`。
4. guest workload 没有自然走到脚本最后的 `poweroff`。

所以 manager 看到 simulation 还在跑。

后续如果希望 `runworkload` 自动 PASS，不要在断点处 `detach` 后就结束。应该让被调试程序继续执行到正常退出，或者使用 local-gdb batch 路线。

## 8. gdbserver 是什么

`gdbserver` 是运行在 target Linux 里的小程序。它负责：

1. 启动或 attach 一个 target 程序。
2. 暴露一个 TCP 端口。
3. 等 host 上的 `gdb` 连接。
4. 把断点、单步、读寄存器、读内存等调试请求转发到 target 进程。

本项目的 smoke 里，guest 大致执行：

```bash
gdbserver --once 0.0.0.0:2345 /root/gdbserver-smoke/gdbserver-smoke
```

含义：

- `0.0.0.0:2345`：监听 guest 所有网卡的 2345 端口。
- `--once`：只接受一个 gdb 连接；连接结束后 gdbserver 也退出。
- `/root/gdbserver-smoke/gdbserver-smoke`：被调试的 target 程序。

## 9. 为什么需要 SSH tunnel

host 上的 gdb 通常不直接位于 F2 run host 上。target IP `172.16.0.2` 只在 run host 的 `tap0` 网络里可达。

所以推荐从 manager 机器开 SSH 本地端口转发：

```bash
ssh -N -L 32345:172.16.0.2:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip>
```

之后 manager 机器上的 `localhost:32345` 就等价于 target 的 `172.16.0.2:2345`。

注意：

- `<run-host-private-ip>` 必须用 AWS private IP，不要用 public IP。
- 当前 smoke 中 guest IP 通常是 `172.16.0.2`。
- 如果 target MAC 改了，IP 可能随 MAC 变，需要看 UART 里的 `[gdbserver]` 行。

## 10. gdbserver 使用 SOP

### 10.1 启动 FireSim run

必须从 `sims/firesim` source manager 环境：

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
```

标准流程：

```bash
firesim launchrunfarm -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>
firesim infrasetup    -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>
firesim runworkload   -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>
```

长任务应使用：

```bash
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh <command> ...
```

如果改了 workload、rootfs、binary 或 AGFI，必须重新跑 `infrasetup`。

### 10.2 等 guest 网络和 gdbserver 启动

看远端 UART：

```bash
ssh -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip> \
  'grep -a -n "IceNet DMA API\\|Registered IceNet\\|Starting network\\|\\[gdbserver\\]" /home/ubuntu/sim_slot_0/uartlog | tail -80'
```

关键标记：

```text
IceNet DMA API mappings enabled; checksum offload and SG disabled
Registered IceNet NIC 00:12:6d:00:00:02
Starting network: OK
[gdbserver] phase=prelaunch ... endpoint=172.16.0.2:2345
```

有些现场里 UART 未必打印 `phase=listening`，但只要 `ping` 和真正的 `gdb target remote` 成功，就说明通路可用。

### 10.3 先做网络健康检查

在 run host 上：

```bash
ping -c 2 172.16.0.2
```

如果怀疑 payload corruption，做 pattern 测试：

```bash
ping -c 2 -s 80 -p ff 172.16.0.2
ping -c 2 -s 80 -p 00 172.16.0.2
ping -c 2 -s 80 -p 0123456789abcdef 172.16.0.2
```

注意：不要用 `nc` / `telnet` 随手探测 `2345`。当前 gdbserver 使用 `--once`，一个无意义 TCP 连接可能消耗掉唯一连接机会。

### 10.4 在 manager 上开 SSH tunnel

推荐单独开一个终端：

```bash
ssh -N -L 32345:172.16.0.2:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip>
```

如果需要后台运行：

```bash
ssh -f -N -L 32345:172.16.0.2:2345 \
  -i /home/ubuntu/firesim.pem \
  -o ExitOnForwardFailure=yes \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip>
```

### 10.5 用 RISC-V cross-gdb 连接

当前 smoke 的 gdb 路径：

```bash
GDB=/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb
BIN=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke
```

交互式：

```bash
$GDB "$BIN"
```

进入 gdb 后：

```gdb
set pagination off
set confirm off
target remote :32345
break main
continue
bt
info registers
x/16i $pc
continue
quit
```

如果希望 workload 自然结束，最后要让程序继续执行到退出，不要停在断点处直接 `detach`。

### 10.6 一条命令批处理

用于 smoke 验证：

```bash
/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex 'target remote :32345' \
  -ex 'break main' \
  -ex 'continue' \
  -ex 'bt' \
  -ex 'info registers' \
  -ex 'continue' \
  -ex 'quit'
```

这里第二个 `continue` 是为了让程序从 `main()` 继续跑完。对长时间运行的 `pipeline-runtime`，不要盲目用这个命令；应改成在你关心的断点处收集信息，或者让程序跑到一个可控退出点。

## 11. 调试 pipeline-runtime 时怎么套用

要把 remote gdbserver 用到正式 `pipeline-runtime`，需要满足：

1. rootfs 里有 `/usr/bin/gdbserver`。
2. target 程序带符号，至少编译时有 `-g`，不要 strip。
3. workload runner 不直接启动程序，而是用 gdbserver 包起来：

```bash
gdbserver --once 0.0.0.0:2345 /root/rerocc-linux-tests/rerocc_pipeline_runtime-linux <args...>
```

4. host 侧用同一个 target binary 或带符号的 host-side copy：

```bash
riscv64-unknown-linux-gnu-gdb /path/to/rerocc_pipeline_runtime-linux
```

5. `target remote :32345` 后设置断点。

常用断点可以是：

```gdb
break main
break prt_runtime_run
break prt_dma_submit
break prt_rerocc_issue
```

具体函数名要以当前 binary 符号为准，可以先：

```bash
riscv64-unknown-linux-gnu-nm -n /path/to/rerocc_pipeline_runtime-linux | grep prt_
```

## 12. gdbserver 常见坑

### 12.1 `--once` 只能连一次

不要先用 `nc` 探测端口。第一次 TCP 连接应该是真正的 gdb。

如果连接断了，guest 里的 gdbserver 可能已经退出。此时需要 guest 重新启动 gdbserver，或者重新跑 workload。

### 12.2 `detach` 不等于 workload 自动结束

如果你停在断点处 `detach`，manager 可能仍然看到 simulation 在跑，runworkload 不会自然 copy-back。

想自动收尾时，用 `continue` 跑到 inferior 正常退出。

想保留现场时，可以 `detach` 或停住，但要手动 copy 回：

```text
/home/ubuntu/sim_slot_0/uartlog
/home/ubuntu/switch_slot_0/switchlog
/home/ubuntu/sim_slot_0/heartbeat.csv
tap0 pcap
```

然后再 terminate runfarm。

### 12.3 UART 里的 `phase=prelaunch` 不一定代表 gdbserver 不可用

本轮现场里 UART 只明确打印到：

```text
[gdbserver] phase=prelaunch ... endpoint=172.16.0.2:2345
```

但实际 gdb 能成功连接并断到 `main()`。所以最终判据应是：

```text
target remote 成功 + 能读寄存器/下断点/继续运行
```

而不是只看 UART 是否有 `phase=listening`。

### 12.4 先保存现场，再关 runfarm

如果 runworkload 异常或手工调试后不退出，不要直接关机器。先拉现场：

```bash
mkdir -p tmp/firesim-aws-f2/nic-scenes/<name>
scp -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip>:/home/ubuntu/sim_slot_0/uartlog tmp/firesim-aws-f2/nic-scenes/<name>/
scp -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip>:/home/ubuntu/switch_slot_0/switchlog tmp/firesim-aws-f2/nic-scenes/<name>/
scp -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip>:/home/ubuntu/tap0*.pcap tmp/firesim-aws-f2/nic-scenes/<name>/ 2>/dev/null || true
```

然后：

```bash
firesim terminaterunfarm --forceterminate \
  -c <runtime.yaml> \
  -a <hwdb.yaml> \
  -r <build-recipes.yaml>
```

最后用 AWS 确认 instance 已经 `terminated`。

## 13. 当前建议

当前 NIC payload corruption 已经有动态验证支持“driver DMA API 修复有效”。下一步建议：

1. 不为这个 corruption 继续构建新 bitstream。
2. 把 remote gdbserver smoke 改成能自动跑完的测试：不要在 `main()` 处 `detach`，而是继续到 inferior 正常退出。
3. 如果要调正式 `pipeline-runtime`，先做一个专门的 gdbserver workload 变体，保留原正常 workload 不动。
4. 每次 remote gdb 调试前先做 `ping` 和 pattern ping，确认 NIC 通路健康。
5. 如果 NIC 又出现异常，优先保存 pcap、uartlog、switchlog，再关 runfarm。
