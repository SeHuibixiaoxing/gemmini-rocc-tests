# F2 NIC 静态排查：四个问题的把握与解释

本文是 2026-04-29 的临时静态 review 说明，目标是把最近指出的四个问题说清楚：

- 这个点是不是代码里真实存在的问题。
- 它有多大概率解释当前 NIC 失败。
- 不懂 FireSim/F2 的人也能理解它为什么危险。

这里把两个概念分开：

- **问题存在把握**：只看代码、接口约束和本地文档，我有多大把握认为这个设计点确实不对。
- **当前主因把握**：它有多大概率就是这轮 NIC runworkload 失败的主因。

## 背景：host 和 target 之间这条 NIC 数据路

FireSim 上 NIC 的 host 侧驱动需要不断把数据送进 FPGA，也要从 FPGA 拉出数据。F2 平台没有沿用 F1 的 XDMA 文件读写路径，而是通过 AWS F2 的 AppPF BAR4，也就是 PCIS AXI4 接口来做 CPU-managed stream 访问。

当前相关链路可以简化成这样：

```text
host C++ simif_f2.cc
  fpga_pci_poke64 / fpga_pci_peek64
      |
      v
AWS F2 shell AppPF BAR4 / PCIS, 512-bit AXI bus
      |
      v
cl_firesim.sv: axi_clock_converter_512_wide
      |
      v
cl_firesim.sv: firesim_pcis_width_bridge_512_to_64
      |
      v
F1Shim / CPUManagedStreamEngine, 64-bit AXI beat
      |
      v
FireSim bridge streams, 512-bit token payload
```

关键矛盾是：F2 shell 暴露给 CL 的 PCIS 总线是 512-bit；但当前生成出来的 `CPUManagedStreamEngine` 被配置成 64-bit AXI beat。中间这个自定义 `firesim_pcis_width_bridge_512_to_64` 负责把 512-bit PCIS transaction 拆成 64-bit beat。

## 总览

| 编号 | 问题 | 问题存在把握 | 当前主因把握 | 结论 |
| --- | --- | --- | --- | --- |
| 1 | PCIS 读路径一次 `peek64` 会消耗 8 个 64-bit stream beat | 95% | 75%-85% | 最像当前 NIC 数据流失败主因，优先修 |
| 2 | partial write strobe 会违反 CPUManagedStreamEngine 的 full-strobe 契约 | 90% | 15%-30% | 真实的防御性 bug，但不太像当前主路径 |
| 3 | `cl_sh_status*` 直接跨时钟组合输出，debug status 不可靠 | 90% | 5%-15% | 真实 CDC/debug bug，主要影响观测和 timing 噪声 |
| 4 | PCIS ID 从 16 bit 接到 6 bit F1Shim，内部有截断 | 80% | 5%-10% | 截断存在，但当前 bridge 又把原始 ID 返回 shell；我下调它的严重性 |

## 1. PCIS 读路径过度消费 stream 数据

### 我有多大把握

- 问题存在把握：**95%**
- 当前主因把握：**75%-85%**

这是四个点里最强的怀疑对象。它不是“可能有点不优雅”，而是读路径的单位理解错了：host 以为自己读 8 字节，硬件却按 64 字节处理，并从 stream 里实际取走 64 字节。

### 代码证据

F2 HDK 文档明确说 PCIS shell 实际只支持 `size=6`，也就是 64 字节、512-bit 传输：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/docs-rtd/source/hdk/cl/examples/cl-dram-hbm-dma/verif/README.rst:279`

F2 BFM 模型也把 PCIS 的 `awsize/arsize` 强行设成 `3'h6`：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/common/verif/models/sh_bfm/sh_bfm.sv:669`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/common/verif/models/sh_bfm/sh_bfm.sv:752`

host 侧现在每次读 CPU-managed stream 时用的是 64-bit MMIO load：

- `sims/firesim/sim/midas/src/main/cc/simif_f2.cc:235`
- `sims/firesim/sim/midas/src/main/cc/simif_f2.cc:243`

但是当前 bridge 的读路径按 `s_axi_arsize` 决定要读几个 64-bit lane：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:158`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:169`

当 `s_axi_arsize == 3'd6` 时，`read_lane_mask()` 返回 `8'hff`。这表示 bridge 会依次向下游发 8 个 64-bit read。

CPUManagedStreamEngine 的 to-host stream read 是会消费 FIFO 的 destructive read。相关逻辑是把 `outgoingQueueIO.deq` 接到 serializer，并在 read helper fire 时让 `ser_des.io.narrow.out.ready` 拉高：

- `sims/firesim/sim/midas/src/main/scala/midas/core/CPUManagedStreamEngine.scala:231`
- `sims/firesim/sim/midas/src/main/scala/midas/core/CPUManagedStreamEngine.scala:251`

所以一次 host `fpga_pci_peek64()` 不是只窥探一个 word，而是让硬件从 to-host stream 里取走 8 个 64-bit word。

### 用小白能懂的话解释

可以把 FPGA 里的 NIC 返回数据想成一条排队出餐的队伍。host 每次只拿一个餐盒，预期是拿 1 份。可是当前桥接器看到 F2 shell 报上来的包装规格是“大箱子”，于是它一次从队伍里拿走 8 份，只把其中一份交给 host，剩下 7 份直接丢了。

NIC 数据包是有顺序的。只要中间丢了 beat，后面的 token 边界、valid/last 标志、数据 payload 都会错位。错位以后，表现就可能是 NIC 双向通信卡死、driver 一直等不到合理 token、后续 ready/valid 停在奇怪位置。

### 为什么它像当前主因

当前 NIC 问题只在带 NIC 的硬件上出现，UART/TSI 不带 NIC 时没问题。这个 bug 正好只影响 CPU-managed stream，也就是 NIC host/FPGA 交换 token 的路径。更重要的是，它主要打坏 FPGA 到 host 的读回方向；这和之前反复看到“通信没有形成闭环/某一侧等不到对方响应”的现象吻合。

### 推荐修复

对 F2 PCIS CPU-managed bridge，不应该把 `arsize=6` 理解成“读全部 8 个 lane”。因为 host 实际调用是 `peek64`，语义是读一个 64-bit stream beat。修法应是：

- 读请求只按 `araddr[5:3]` 选一个 64-bit lane。
- 对 CPUManagedStreamEngine 只发 1 次 64-bit read。
- 返回给 shell 的 512-bit `rdata` 里，只把这个 64-bit word 放到对应 lane，其它 lane 置 0。
- `s_axi_rid` 仍返回原始 shell ID。

同时新增可综合 debug：

- wide read fire 计数。
- narrow read fire 计数。
- 如果一次 wide read 导致 narrow read 计数增加超过 1，置 sticky error。
- 记录 last `araddr/arsize/lane`。

修好后，运行时应看到 `narrow_r_fire_count` 和 host `peek64` 次数基本一一对应，而不是 8 倍关系。

## 2. Partial write strobe 违反 CPUManagedStreamEngine 契约

### 我有多大把握

- 问题存在把握：**90%**
- 当前主因把握：**15%-30%**

这是一个真实的接口契约漏洞，但它是否触发取决于 host 有没有发 partial write。当前 `simif_f2.cc` 主路径已经改成 `fpga_pci_poke64()`，正常情况下应该产生某个 64-bit lane 内 `8'hff` 的 strobe，所以它不像第一项那么像当前主因。

### 代码证据

CPUManagedStreamEngine 明确断言：只要 W channel valid，`strb` 必须全 1：

- `sims/firesim/sim/midas/src/main/scala/midas/core/CPUManagedStreamEngine.scala:121`
- `sims/firesim/sim/midas/src/main/scala/midas/core/CPUManagedStreamEngine.scala:122`

生成后的 Verilog 里也保留了同样的 assertion：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config/design/FireSim-generated.sv:47761`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config/design/FireSim-generated.sv:47864`

当前 bridge 却把选中 64-bit lane 的 strobe 原样传给下游：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:296`

也就是说，如果 shell 给某个 lane 的 strobe 是 `8'h0f`、`8'hf0` 或其它非 `8'hff`，bridge 会把它送进 CPUManagedStreamEngine。下游的契约是不接受这种写法。

### 用小白能懂的话解释

CPUManagedStreamEngine 像一个只收整箱货的仓库。每次必须送满 8 字节。现在 bridge 遇到半箱货时没有拒收，而是把半箱货递给仓库。仓库的规则里写了“不能这样送”，仿真里会触发 assertion，综合到 FPGA 后则可能表现为数据 token 被破坏。

### 为什么它不太像当前主因

当前 host 写路径已经从旧的 32-bit burst 改成了 `fpga_pci_poke64()`：

- `sims/firesim/sim/midas/src/main/cc/simif_f2.cc:257`
- `sims/firesim/sim/midas/src/main/cc/simif_f2.cc:265`

只要 BAR4 的 64-bit store 最终在 shell 侧表现为某一个 lane 的 `8'hff` strobe，这个问题就不会在正常路径触发。它仍然值得修，因为：

- 未来有人误用 32-bit/8-bit BAR4 写会立即踩雷。
- 如果 host 地址不按 8 字节对齐，也可能产生 partial lane。
- 当前 debug counter 已经有 `write_partial_strobe_count`，说明我们本来就认为这类情况需要观测。

### 推荐修复

bridge 不应把 partial strobe 送入 CPUManagedStreamEngine。更稳的行为是：

- 如果选中 lane 的 strobe 不是 `8'hff`，不发 narrow AW/W。
- 返回 `SLVERR`。
- 增加 sticky error 和计数器。
- 记录 last bad `awaddr/wstrb/lane`。

这样即使 host 写错，错误也会停在 bridge 这一层，不会污染 NIC token stream。

## 3. `cl_sh_status*` 跨时钟组合输出

### 我有多大把握

- 问题存在把握：**90%**
- 当前主因把握：**5%-15%**

这是一个真实的 CDC/debug 问题，但不像当前 NIC 数据流主因，因为 status 输出没有反馈到 NIC datapath。

### 代码证据

`cl_sh_status0/1/2` 和 `cl_sh_status_vled` 是 shell 可读的状态输出：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/common/shell_stable/design/interfaces/cl_ports.vh:31`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/common/shell_stable/design/interfaces/cl_ports.vh:45`

当前 `cl_firesim.sv` 直接把 `firesim_internal_clock` 域下的 valid/ready/counter/debug state 组合接出去：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:2272`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:2307`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:2314`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:2325`

这里混用了至少两个时钟域：

- `clk_main_a0` 域：shell/OCL 主时钟侧。
- `firesim_internal_clock` 域：FireSim target/bridge 内部时钟侧。

多 bit counter/state 直接跨时钟，没有双触发同步、snapshot 或握手。

### 用小白能懂的话解释

这像让两个人同时改同一张纸，一个人在 A 房间按 A 的节奏写，一个人在 B 房间按 B 的节奏读，中间没有“我写完了你再读”的约定。读的人可能读到半新半旧的数字。例如一个计数器从 `0x0fff` 变到 `0x1000`，读的时候可能看到拼出来的怪值。

### 为什么它不太像当前主因

这些 status 信号主要给 `fpga-describe-local-image -M` 或虚拟 LED 看，不参与 NIC ready/valid 的反压逻辑。它会导致：

- debug 读数偶发不一致。
- timing/CDC 报告更难看。
- 如果 shell 对 status 有同步假设，可能增加局部时序噪声。

但它本身不会直接让 NIC packet 丢失，所以当前主因把握较低。

### 推荐修复

把 status 输出改成 `clk_main_a0` 域下的稳定寄存器：

- 单 bit 状态用 2-flop synchronizer。
- 多 bit counter 不做逐 bit 同步；改成 sticky bit、toggle event，或在 `firesim_internal_clock` 域先压缩成小的事件标志，再同步到 `clk_main_a0`。
- 如果确实要看 counter，做 snapshot/握手，或者承认只是 debug 粗略值，不拿它当精确计数。

## 4. PCIS ID 宽度截断

### 我有多大把握

- 截断存在把握：**80%**
- 当前主因把握：**5%-10%**

我对这个点的严重性已经下调。上一轮把它列为“可能 bug”是对的，但进一步看 bridge 后，它不像当前会造成功能错误。

### 代码证据

F2 shell PCIS port 是 16-bit ID：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/common/shell_stable/design/interfaces/cl_ports.vh:174`

当前自定义 bridge 的 shell-facing 和 narrow-facing ID 都声明成 16 bit：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:20`
- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:48`

但是 F2Config 里 CPUManaged AXI ID 仍是 6 bit：

- `sims/firesim/sim/midas/src/main/scala/midas/Config.scala:140`
- `sims/firesim/sim/midas/src/main/scala/midas/Config.scala:149`

生成的 F1Shim 端口也确实是 6 bit：

- `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config/design/FireSim-generated.sv:65657`

所以从 bridge 接到 F1Shim 时，高 10 bit ID 会被截掉。

### 为什么我下调严重性

当前 bridge 对 shell 的响应 ID 并不是从 F1Shim 传回来的 ID，而是用自己保存的原始 16-bit ID：

- 写响应：`s_axi_bid = wr_awid_reg`
  - `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:286`
- 读响应：`s_axi_rid = rd_arid_reg`
  - `sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv:302`

这意味着即使 F1Shim 内部只看到低 6 bit，shell 最终拿到的 response ID 仍然是原始 16-bit ID。因此它不像一个会直接导致 host 等不到 response 的 bug。

### 它仍然为什么值得记录

这仍是一个设计不一致：

- shell 是 16-bit ID。
- bridge 内部保存 16-bit ID。
- F1Shim/CPUManaged 内部只有 6-bit ID。

如果未来 bridge 改成使用 `m_axi_bid/m_axi_rid` 返回，或者 CPUManaged 侧开始依赖完整 ID，这个截断会重新变成实质问题。

### 推荐处理

短期可以不作为当前主修目标，但建议加 debug：

- 如果 `s_axi_awid[15:6] != 0` 或 `s_axi_arid[15:6] != 0`，计数并置 sticky flag。
- 继续用原始 16-bit ID 返回 shell。

长期更干净的修法是把 F2 的 `CPUManagedAXI4Params.idBits` 改成 16，并重新生成 F1Shim。但这会改变生成 RTL 接口，影响范围比修 bridge 读路径大，不建议抢在第一优先级。

## 下一步优先级

1. **先修问题 1：PCIS read 只消费一个 64-bit lane。** 这是当前最像 NIC 主因的问题。
2. 同一轮顺手修问题 2：partial strobe 不下传，返回 `SLVERR` 并记录现场。
3. 同一轮可以加问题 1/2 的 sticky debug 输出，让下一个 bitstream 一次性看清楚读写比例、bad strobe、last addr/lane。
4. 问题 3 作为 debug/status 清理项处理，避免后续状态读数误导。
5. 问题 4 先加高 ID sticky counter，不作为当前主因修复。

我目前对“下一轮硬件修复应该围绕问题 1 展开”的把握最高。问题 2 和问题 3 是应该补上的护栏；问题 4 是结构不一致，但当前 bridge 已经把 response ID 保住了，所以不应把主要精力放在那里。
