# FireSim SimpleNIC / gdbserver 静态审计（2026-04-24）

更新时间：`2026-04-24 10:47 UTC`

## 1. 为什么需要这份文档

最近这条 `1 CPU + NIC + Linux + gdbserver` 最小 smoke，不再是简单的：

- `push()==0`
- 但不知道为什么

这一轮拿到的新日志已经足够说明更多东西：

- `to_cpu` 队列不是空，而是在持续增长
- `host driver` 不是没有尝试拉数据，而是坚持要等满一整批
- 最终失败发生在 `from_cpu` 队列已经满了的时候

但如果只看日志本身，仍然有两个容易混淆的点：

1. 这是不是单纯的 host driver 阈值设计问题
2. 还是 target-side `SimpleNICBridge` 让两个方向的 token 流动不同步，最后把 host driver 顶死了

这份文档的目标，就是把这条推理链完整展开。

## 2. 这轮运行的最小事实

对应运行：

- runtime config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_30mhz.yaml`
- workload 结果目录：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-24--10-37-58-rocket-singlecore-nic-gdbserver-smoke-f2-rocket-singlecore-nic-gdbserver-smoke`

关键 `uartlog` 里出现了三段证据。

### 2.1 init 时 `to_cpu` 为空

最开始先看到：

- `pull_blocked ... count=0 ... required_bytes=0`

这只说明刚启动时 `to_cpu` 没东西。

### 2.2 开始仿真后，`to_cpu` 在持续积累

随后是一长串：

- `count=8`
- `count=22`
- `count=36`
- ...
- `count=911`

这里要注意：

- 这些不是“每次都空”
- 恰恰相反，这是 `to_cpu` 在慢慢涨
- host driver 之所以还没有真的 `pull`，只是因为它这轮想要的是整批 `915` 个 big token

### 2.3 最后失败在 `from_cpu` 已满

最后出现：

- `push_blocked ... count=3072 fpga_buffer_size=3072 space_available=0`
- `ERR MISMATCH! on writing tokens in. actually wrote in 0 bytes, wanted 58560 bytes.`

这里最重要的信息是：

- `3072 / 3072`

这不是“差一点满”，而是已经满了。

## 3. `SimpleNIC` host driver 实际在做什么

关键代码：

- `/home/ubuntu/chipyard/generators/firechip/bridgestubs/src/main/cc/bridges/simplenic.cc`

### 3.1 当前一轮 bulk 大小不是任意值，而是固定由 `link_latency` 决定

代码里有三条常量关系：

- `TOKENS_PER_BIGTOKEN = 7`
- `SIMLATENCY_BT = LINKLATENCY / TOKENS_PER_BIGTOKEN`
- `BUFBYTES = SIMLATENCY_BT * 64`

本轮 `link_latency = 6405`。

所以：

- `SIMLATENCY_BT = 6405 / 7 = 915`
- `BUFBYTES = 915 * 64 = 58560`

也就是说，这个 driver 的“一个 round”不是若干个 token，而是固定的：

- `915` 个 big token
- `58560` 字节

### 3.2 init 会先往 `from_cpu` 预塞一整轮

`simplenic_t::init()` 会：

1. 先确认 `to_cpu` 开始时为空
2. 再往 `from_cpu` `push` 一整轮 `915 * 64`

这不是异常行为，而是它的设计本意：

- 先给 target-side NIC-local 域准备出一个 link latency 窗口

### 3.3 steady-state `tick()` 只接受“整轮满足”

`tick()` 里：

- `pull(... requested_bytes, required_bytes=requested_bytes)`
- `push(... requested_bytes, required_bytes=requested_bytes)`

这表示：

- 如果这轮不够 `915` 个 big token
- host 就完全不做

所以从 host driver 视角，它的工作模型是：

- “我一次只处理完整 round”
- “半轮不算”

这与 FireSim 文档里对 NIC bridge 的描述是一致的：host driver 以 bulk 方式搬 token。

## 4. `count` 到底表示什么

这个问题必须单独讲清楚，因为后面的推理全靠它。

关键代码：

- `/home/ubuntu/chipyard/sims/firesim/sim/midas/src/main/scala/midas/core/CPUManagedStreamEngine.scala`
- `/home/ubuntu/chipyard/sims/firesim/sim/midas/targetutils/src/main/scala/midas/Utils.scala`

`count_addr` 绑定的是 FPGA 侧 `Queue.io.count`。

而 `Queue.io.count` 的语义就是：

- 当前 queue occupancy

不是：

- free slots
- credit
- “已经被 host 读过的数量”

因此当前日志的解释是直接的：

- `to_cpu count=911`：
  `to_cpu` 里已经真实堆了 `911` 个 big token
- `from_cpu count=3072`：
  `from_cpu` 已经真实满了

这一步排除了“我们把寄存器读反了”的解释。

## 5. `HostPort` 对 bridge 的根本语义要求

关键代码：

- `/home/ubuntu/chipyard/sims/firesim/sim/firesim-lib/src/main/scala/bridgeutils/HostPort.scala`
- `/home/ubuntu/chipyard/sims/firesim/sim/midas/src/main/scala/midas/core/FPGATop.scala`

`HostPort` 的注释写得很明确：

- 它服务的是“consume a single input token and produce a single output token”

这句话的中文含义就是：

- 一个 target cycle 的 bridge 级推进，应该同时对应：
  - 消费一份来自 host 的输入 token
  - 产生一份发给 host 的输出 token

`FPGATop.scala` 还会把 target-side所有 `toHost` 和 `fromHost` 通道折叠成：

- `hp.toHost.hValid`
- `hp.fromHost.hReady`

所以 `HostPort` bridge 不是两个完全独立的单向口，而是一种“这一步输入/输出 token 应该成对发生”的结构。

## 6. 当前 `SimpleNICBridge` 为什么有“漂移风险”

关键文件：

- 本地：
  `/home/ubuntu/chipyard/generators/firechip/goldengateimplementations/src/main/scala/SimpleNICBridge.scala`
- 官方主线：
  `https://raw.githubusercontent.com/ucb-bar/chipyard/main/generators/firechip/goldengateimplementations/src/main/scala/SimpleNICBridge.scala`

### 6.1 upstream 主线的问题不在“参数错”，而在 fire 条件太松

`SimpleNICBridge` 里，`tFireHelper` 只要求：

- `hPort.toHost.hValid`
- `hPort.fromHost.hReady`

但没有把以下条件一起纳入：

- `ntht_queue.io.enq.ready`
- `htnt_queue.io.deq.valid`

这会带来一个静态风险：

- target 这一步虽然“整体上看”可以 fire
- 但 bridge 本地承接输出 token 的小队列不一定真 ready
- bridge 本地提供输入 token 的小队列也不一定真 valid

换句话说，bridge 允许：

- 输出方向先跑一些
- 输入方向后补一些

这就违反了前面第 5 节讲的 `HostPort` 单步语义。

### 6.2 本地已有 patch 的意图

本地当前未提交 patch 做的事情是：

- 把 `ntht_queue.io.enq.ready`
- 把 `htnt_queue.io.deq.valid`

都并入 `tFireHelper`

同时还把：

- `ntht_queue.io.enq.valid`
- `htnt_queue.io.deq.ready`
- `target.in.valid`

都显式地绑到同一个 `tFire`

这个 patch 的语义不是“调性能”，而是：

- 强制一轮 target fire 必须真地同时完成这一步输入/输出 token 交换

### 6.3 为什么这和当前日志吻合

当前看到的是：

1. `to_cpu` 在持续增长
2. host 暂时不拉，因为不到 `915`
3. 一旦达到 `915`，host 会整批拉
4. 如果 target-side 输入方向没有按同样速率消费 `from_cpu`
5. host 再整批往 `from_cpu` 回推
6. 很快就能把 `from_cpu` 顶到 `3072`

这正是“两个方向不同步推进”会出现的典型形状。

## 7. 为什么这轮 run 还不能当成“RTL fix 已验证”

这里必须和“静态方向已经很强”分开看。

### 7.1 `infrasetup` 会用本地源码重新生成 host driver

从：

- `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-24--10-30-56-infrasetup-C79LBCRP7S9KV4RU.log`

可以看到：

- manager 会重新跑 `make ... f2`
- 会重新执行 Golden Gate
- 会重新编译本地 `FireSim-f2`

也就是：

- 当前这轮实际运行的 host driver，确实来自当前本地源码

### 7.2 但当前实际加载的 AGFI 是一份既有 bitstream

AGFI 描述里记录了：

- `agfi-06c821df02283a12e`
- `firesim-commit:9920b2919337411b8ade6d0c2e1f836666422504`

所以当前运行方式是：

- host driver 来自本地现状
- FPGA bitstream 来自已构建完成的 AGFI

### 7.3 结论

因此，这轮 run 的证据边界是：

- 对 host-only 修改：
  - 有效
- 对 target-side RTL patch：
  - 只能说“当前现象与该 patch 要修的问题高度一致”
  - 不能直接说“该 patch 已在 FPGA 上验证”

若要做真正验证，必须：

- 用与 patch 匹配的源码重建 bitstream
- 然后再跑同样的最小 smoke

## 8. 当前最合理的结论

不是：

- “host driver 随机坏了”

也不是：

- “NIC 参数配错了”

而是：

- 当前 `SimpleNIC` host driver 的 bulk round 设计很严格
- 当前 `SimpleNICBridge` upstream 写法又给了两个方向不同步推进的空间
- 两者叠加后，很容易出现：
  - `to_cpu` 先慢慢积累
  - host 一旦达到阈值就整批搬运
  - `from_cpu` 很快被补推到满
  - 最后死在 `push()==0`

## 9. 下一步最稳妥的动作

### 9.1 如果只是继续观察旧 AGFI

- 继续做 host-only、低扰动观测
- 不要把结果写成“RTL fix 已验”

### 9.2 如果要验证 lockstep patch

- 先重建一版带 NIC 的匹配 bitstream
- 先跑同样的最小 `1 CPU + NIC + Linux + gdbserver` smoke
- 确认最小 smoke 正常后，再上 `pairdummy` 主线

### 9.3 如果还想再补一层低扰动证据

可以只加一条很轻的 success-path round 日志：

- 每成功处理一轮 `915` big token，就打一条 round 号

这样就能把“`from_cpu` 为什么会到 `3072`”直接量化出来，而不需要继续扩大热路径日志面。

## 10. 相关文档

- `gdbserver` 接入 SOP：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/testing/gdbserver_integration_sop.md`
- 本轮详细 debug 记录：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260424T104706Z.md`
