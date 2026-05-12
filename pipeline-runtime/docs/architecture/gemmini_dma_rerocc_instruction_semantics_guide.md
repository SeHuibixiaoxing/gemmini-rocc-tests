# Gemmini / Coupled-DMA / ReRoCC 指令语义导读

更新时间：`2026-04-20 13:47 UTC`

## 这份文档解决什么问题

这份文档想回答的不是“某个 API 怎么调用”，而是更底层的问题：

- 一条 `Gemmini` / `Coupled-DMA` / `ReRoCC` 指令，**CPU 什么时候算发出成功**？
- 发出成功以后，硬件是 **同步完成**、**异步排队**，还是只做了一个**本地寄存器更新**？
- `fence` / `flush` 到底是在 **等执行完成**、**只清 TLB**，还是**只是主核侧的内存栅栏**？
- 哪些参数必须满足约束，否则硬件会断言、退化，或者根本不是你以为的语义？

目标是让你即使暂时不去翻代码，也能先建立一套正确的运行心智模型。

## 当前项目里的三层关系

在当前这套 P12 pair-manager 配置里：

- `ReRoCC` 是 **前端路由层**。
  - 软件先用 `rrcfg / rropc / rrbar` 这些 CSR，把“某个 `cfg` 槽位”绑定到某个 manager，再把某个 `custom opcode` 绑定到这个 `cfg`。
- `Coupled-DMA` 是 **`custom2` 后面的具体加速器**。
  - 它负责独立的 copy engine。
- `Gemmini` 是 **`custom3` 后面的具体加速器**。
  - 它负责 scratchpad load/store、preload、compute、loop/CISC 等。

在 pair wrapper 下，`custom2` 和 `custom3` 被同时挂到同一个 pair manager 里：

- `custom2 -> Coupled-DMA`
- `custom3 -> Gemmini`

对应代码：

- 软件入口：
  - `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_control.h`
  - `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_coupleddma.h`
  - `generators/gemmini/software/gemmini-rocc-tests/include/gemmini.h`
- pair wrapper：
  - `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMAPairWrapper.scala`
- 当前 pair-manager 配置保留原始 opcode：
  - `generators/chipyard/src/main/scala/config/GemminiLearningReRoCCPairManagerConfigs.scala`

这件事非常重要，因为它决定了：

- `RROPC2` 绑定的是 DMA 这半边；
- `RROPC3` 绑定的是 Gemmini 这半边；
- 在 pair-manager 模式里，想证明“绑定正确”，通常必须两边都测。

## 先统一五个词

后面全文都用下面这五个词，不再反复解释：

- **发射阻塞**
  - CPU 会卡在“这条指令能不能先交给硬件”这一步。
  - 常见原因是 `io.cmd.ready` 为假、resp 通道没空、CSR 被标了 `stall`。
- **执行异步**
  - 指令一旦被硬件接收，CPU 就能继续跑；真正的搬运/计算在加速器内部继续进行。
- **完成阻塞**
  - 指令或 helper 返回时，硬件工作已经被保证完成，或者至少已经到达某个明确的完成点。
- **本地寄存器更新**
  - 指令只改一个配置寄存器，不代表真正的 copy / compute 已经开始，更不代表完成。
- **完成屏障**
  - 它的意义不是“发一个命令”，而是“等前面某类工作都做完”。

## 一句话总表

先给出结论版总表。下面的长文只是把这张表展开。

| 子系统 | 软件接口 / 指令 | 本质 | CPU 返回时机 | 是否等真实执行完成 | 最该记住的点 |
| --- | --- | --- | --- | --- | --- |
| ReRoCC | `rr_acquire_cfg()` | acquire 请求 + 读回 | 读回成功后 | 是，等到 acquire ack | 这是“拿到 cfg 槽位”的阻塞操作 |
| ReRoCC | `rr_release()` | release 请求 | CSR 写完成后 | 否 | 它不是完成屏障 |
| ReRoCC | `rr_set_opc()` | 本地 opcode 绑定 | CSR 写完成后 | 否 | 只是改路由表 |
| ReRoCC | `rr_fence()` | cfg 级 drain / unbusy 屏障 | `rrbar` 完成且随后 `fence` 过关 | 是 | 这是 ReRoCC 模式下真正的“等远端 manager 干完” |
| Coupled-DMA | `set_dst()` | 记住目的地址和 completion 地址 | 指令被接收后 | 否 | 只是写 latch |
| Coupled-DMA | `set_src()` | 入队一笔 copy 请求 | copy descriptor 入队后 | 否 | 真正 copy 还在后台 |
| Coupled-DMA | `wait()` | 等 DMA 空闲并返回 | DMA 空闲时 | 是 | 这是 DMA 自带的阻塞等待 |
| Coupled-DMA | `read_monitor()` | 读监控计数器 | resp 返回后 | 不涉及 | 读的是当前计数，不是 completion barrier |
| Coupled-DMA | `sfence` | 清 xlate cache | 指令被接收后 | 否 | 这是地址翻译缓存失效，不是 copy fence |
| Gemmini | `config_*` | 配置 load/store/ex 状态 | 被接收后 | 否 | 大多只是更新控制寄存器 |
| Gemmini | `mvin/mvin2/mvin3` | 把 load 请求送入队列 | 入队后 | 否 | 真正 DMA 在后台 |
| Gemmini | `preload` | 设置 B/D 与 C 路径 | 入队后 | 否 | 只是执行流水线的一部分 |
| Gemmini | `compute_*` | 送执行控制到阵列 | 入队后 | 否 | 真正阵列计算和写回都在后台 |
| Gemmini | `mvout` | 发起 store 请求 | 入队后 | 否 | 真正回写 DRAM 在后台 |
| Gemmini | `gemmini_flush()` | TLB / translation flush | 指令被接收后 | 否 | 这不是“等 Gemmini 计算完成” |
| Gemmini | `gemmini_fence()` | 普通 RISC-V `fence` | 由 Rocket 核心决定 | 视模式而定 | 在 ReRoCC 模式下，它不能代替 `rr_fence(cfg)` |
| Gemmini | `counter` | 同步读写计数器 | resp 返回后 | 不涉及 | 管理类同步指令 |
| Gemmini | `spm_xlate_*` | 配共享 SPM 地址翻译 | 配置类立即生效，fault 查询有 resp | 仅 `fault` 为同步读 | `cfg/range/flush` 都不是完成屏障 |
| Gemmini | `LOOP_* / CISC_*` | 宏指令前端 | 被前端 FSM/tiler 接收后 | 否 | 它们最后仍展开成底层 load/preload/compute/store |

对 `pipeline-runtime` 还要额外记一条实现级约束：

- 表里写的是 **底层 raw helper** 的语义；
- `pipeline-runtime` 软件栈内部并不直接把 raw `rr_release()` 暴露给 DMA/Gemmini 调用点；
- 运行时统一经由 `prt_rr_release_scope()` 释放 scope，
  它在 `rr_release(cfg)` 后紧跟一次同 `cfg` 的 `RRCFG` 读回，
  然后才把 `scope->valid` 置 0；
- 所以对 `pipeline-runtime` 自身来说，
  之前那类 “release 返回了，但 scope 已被软件重用” 的短窗口，
  已经在包装层被封住。

## 先看 CPU 这一侧：什么叫“阻塞”

如果你不先搞清楚 Rocket 看待 RoCC 的方式，后面几乎一定会把“发射成功”和“真正执行完”混起来。

### 1. `xd=0` 与 `xd=1` 的区别

RoCC 宏本质上有两类：

- **不返回 rd 的指令**
  - 宏里会把 `rd=x0`，`xd=0`
  - 典型例子：
    - `gemmini_mvin`
    - `gemmini_compute_preloaded`
    - `rerocc_coupleddma_set_src`
    - `rerocc_coupleddma_set_dst`
- **需要返回 rd 的指令**
  - 宏里会把 `xd=1`
  - 典型例子：
    - `rerocc_coupleddma_wait`
    - `rerocc_coupleddma_read_monitor`
    - `rerocc_gemmini_spm_xlate_fault`
    - Gemmini counter 访问

对应软件宏：

- `generators/gemmini/software/gemmini-rocc-tests/rocc-software/src/xcustom.h`
- `generators/rerocc/tests/rocc.h`

### 2. 对 CPU 来说，“阻塞”首先是 `cmd.ready`

Rocket 把自定义指令送到 `io.rocc.cmd`。如果 `cmd.ready` 不成立，这条 RoCC 指令会在 WB 附近重放，CPU 不能把它当作已经发出去。

对应代码：

- `generators/rocket-chip/src/main/scala/rocket/RocketCore.scala`
- `generators/rocket-chip/src/main/scala/tile/LazyRoCC.scala`

所以，“指令是阻塞还是非阻塞”，第一层要先分成：

- **发射阶段阻塞**
  - 因为 `cmd.ready` 不给过
- **发射后异步**
  - 因为硬件只是收下命令，真正工作还没完成

### 3. `xd=1` 还会多一层“等 resp”

如果一条 RoCC 指令需要返回 `rd`，那它不只是要等 `cmd.ready`，还要等 accelerator 真的在 `io.resp` 上把值送回来。

因此：

- `rerocc_coupleddma_wait()` 是真正的阻塞等待；
- `rerocc_coupleddma_set_src()` 不是。

### 4. `fence` 为什么会和 RoCC 忙闲有关

Rocket 核心在 decode 阶段会把 `fence` 和 RoCC 的 `busy` 绑在一起。

对应代码：

- `generators/rocket-chip/src/main/scala/rocket/RocketCore.scala`

这意味着：

- 如果当前 tile 看到它自己的 `io.rocc.busy` 还在高，`fence` 不会轻易过去；
- 所以普通 `asm volatile("fence")` 在某些模式下确实会“顺带等 accelerator 不忙”。

但要特别小心：

- **它只能看见当前 tile 挂着的那个 RoCC 前端的 `busy`**
- 它不天然知道“远端 manager 里是不是还在算”

这个差别正是后面 `gemmini_fence()` 和 `rr_fence(cfg)` 语义差很多的根源。

## ReRoCC：它不是计算单元，而是“远端 RoCC 路由层”

### 先说结论

ReRoCC 本身不做 Gemmini 计算，也不做 DMA copy。它做四件事：

- 管理 `cfg` 槽位的占用关系；
- 把 `custom opcode` 绑定到某个 `cfg`；
- 把 CPU 发出的 RoCC 指令重新打包，送到远端 manager；
- 提供一个 `cfg` 级别的“等远端 manager 空闲”的屏障。

### ReRoCC 读代码时真正要看哪三层

按阅读优先级：

1. **软件接口层**
   - `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_control.h`
2. **客户端 CSR 与路由层**
   - `generators/rerocc/src/main/scala/client/CSRs.scala`
   - `generators/rerocc/src/main/scala/client/Client.scala`
3. **manager 协议层**
   - `generators/rerocc/src/main/scala/bus/Protocol.scala`
   - `generators/rerocc/src/main/scala/manager/Manager.scala`

### 当前仓库里的 CSR 版图

当前仓库状态已经是 `cfg32`：

- `rropc0..3`：`0x800..0x803`
- `rrbar`：`0x804`
- `rrcfg0..31`：
  - `0x810..0x81f`
  - `0x820..0x82f`

对应代码：

- `generators/rerocc/src/main/scala/client/CSRs.scala`

### ReRoCC 协议真正怎么走

从 CPU 发一条 ReRoCC 管理下的 `custom2/custom3` 指令，到远端 manager 真正接收，中间会发生两件事：

1. **本地 client 先看 `RROPCx`**
   - 例如 `custom3` 会先查 `RROPC3`
   - `RROPC3` 里存的是 `cfg_id`
2. **再由 `cfg_id` 找到对应 manager**
   - `rrcfg[cfg_id].mgr`

然后 client 把这条 RoCC 指令拆成 `mInst` 多拍消息：

- 第 0 拍：指令字本身
- 第 1/2 拍：`rs1` / `rs2`，如果这条指令真的用了它们

manager 收到以后再把它重组回真正的 `RoCCCommand`，交给后面的 Gemmini 或 DMA。

对应代码：

- `generators/rerocc/src/main/scala/bus/Protocol.scala`
- `generators/rerocc/src/main/scala/client/Client.scala`
- `generators/rerocc/src/main/scala/manager/Manager.scala`

### ReRoCC 不是“每发一条指令都等执行完”

这是最容易误判的点。

ReRoCC client 不是“发一条，等远端做完，再发下一条”。它是 **credit-based issue**：

- 每个 `cfg` 有一组 credits；
- 默认 `ReRoCCIBufEntriesKey = 4`；
- client 只要还有 credit，就可以继续把指令送出去；
- 远端 manager 一旦把指令收进自己的 `inst_q`，就会回 `sInstAck`；
- client 收到 `sInstAck`，就把这个 credit 加回去。

因此：

- `sInstAck` 只说明“远端 manager **接收/入队** 了指令”
- **不说明 Gemmini/DMA 已经执行完成**

对应代码：

- `generators/rerocc/src/main/scala/manager/Parameters.scala`
- `generators/rerocc/src/main/scala/client/Client.scala`
- `generators/rerocc/src/main/scala/manager/Manager.scala`

### ReRoCC 各操作的真实语义

#### 1. `rr_acquire_cfg(cfg, mgr)`

软件 helper 做了两步：

- 先往 `RRCFG[cfg]` 写 `acq=1, mgr=...`
- 再立刻读回 `RRCFG[cfg]`

硬件侧发生的事情是：

- client 发现这是“从未占用 -> 请求占用”
- 进入 `s_acq -> s_acq_ack` 状态机
- 发 `mAcquire`
- 等 `sAcqResp`
- 收到 ack 之后才把 `csr_cfg_next(cfg).acq` 真正置位

更关键的是：

- 在 acquire/release/status/PTBR 更新期间，`cfg` CSR 被打上了 `stall`
- Rocket CSRFile 会因为 `stall` 把这条 CSR 访问卡住

所以 `rr_acquire_cfg()` 的返回语义是：

- **阻塞**
- **返回时已经知道 acquire 成功或失败**

它不是“发完就不管”。

#### 2. `rr_release(cfg)`

软件 helper 只做了一步：

- 往 `RRCFG[cfg]` 写 0

硬件侧发生的事情是：

- client 进入 `s_rel -> s_rel_ack`
- 发 `mRelease`
- manager 只有在：
  - 自己 `inst_q` 为空
  - 下游 `io.busy` 为假
  才会回 `sRelResp`

但是 helper 本身没有再读回，也没有做后续等待。

所以 `rr_release(cfg)` 的返回语义是：

- **只保证 release 请求发起了**
- **不保证远端 manager 已经真的完成 release**

这就是为什么“`rr_release()` 后立刻读 `RRCFG` 或立刻改 `RROPC`”会是危险窗口。

但这段语义要和 `pipeline-runtime` 当前实现分开看：

- raw helper 仍然是上面的非阻塞 release 语义；
- `pipeline-runtime/src/prt_rerocc.c`
  里的 `prt_rr_release_scope()` 已经把
  `rr_release(cfg) + rr_read_csr(CSR_RRCFG0 + cfg)`
  绑定成一个 release wrapper；
- 因而在 `pipeline-runtime` 主线里，
  DMA / Gemmini / SPM-xlate scope release
  默认不会直接暴露这个短窗口。

#### 3. `rr_set_opc(opcode_id, cfg_id)`

这只是把 `RROPCx` 本地改掉：

- `RROPC0..3` 只是本地 client 路由表
- 不会和远端 manager 往返

所以它的语义是：

- **同步的本地路由更新**
- **不是执行完成屏障**

参数约束也很直接：

- 只支持 `opcode_id = 0..3`
- 当前 pair-manager 主要关心：
  - `RROPC2`
  - `RROPC3`

#### 4. `rr_fence(cfg)`

这是 ReRoCC 里最重要的一个操作。

软件 helper 做两步：

- 写 `RRBAR <- cfg`
- 然后执行一个普通的 `asm volatile("fence")`

硬件侧真正发生的是：

- client 把这个 `cfg` 的 `cfg_fence_state` 置为 `f_req`
- 之后 client 只有在：
  - 当前 `inst_sender` 不忙
  - 当前 tile 没有新的 RoCC 指令准备继续发
  才会向 manager 发 `mUnbusy`
- manager 只有在：
  - `inst_q` 空
  - 后端实际 RoCC `io.busy` 为假
  才会回 `sUnbusyAck`
- client 收到 `sUnbusyAck` 后，`cfg_fence_state` 才回到 `f_idle`

而 Rocket 核心又会在 `fence` 时观察本地 `io.rocc.busy`。

因此 `rr_fence(cfg)` 的真实语义是：

- **它是 ReRoCC 模式下真正的完成屏障**
- 它等的不是“client 已经把指令发完”
- 而是“这个 `cfg` 对应的远端 manager 已经空闲”

如果你只想记一句话：

- **在 ReRoCC 管理模式下，真正的‘等 Gemmini/DMA 干完’原语是 `rr_fence(cfg)`，不是 `rr_release(cfg)`。**

### ReRoCC 这一层最关键的误区

最常见的误解有三个：

1. **误以为 `sInstAck` 等于执行完成**
   - 错。它只表示远端 manager 接收了命令。
2. **误以为 `rr_release(cfg)` 是屏障**
   - 错。它只是发起 release。
3. **误以为 `gemmini_fence()` 能替代 `rr_fence(cfg)`**
   - 在 ReRoCC 模式下通常错。
   - 因为 `gemmini_fence()` 只能看到本地 ReRoCC client 的 `busy`；
   - 而 client 的 credit 还满以后，远端 manager 仍可能在执行。

## Coupled-DMA：它是一个单 copy-engine，有自己的等待指令

### 先说结论

Coupled-DMA 不是“发一条完整 copy 指令”。它的软件协议是两段式：

1. 先写目的地址和 completion 地址
2. 再写源地址和长度，真正入队 copy 请求

它还有一个单独的阻塞等待指令：

- `rerocc_coupleddma_wait()`

### 先看软件接口

对应头文件：

- `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_coupleddma.h`

主要接口：

- `rerocc_coupleddma_set_dst(dst, completion_addr)`
- `rerocc_coupleddma_set_src(src, num_bytes)`
- `rerocc_coupleddma_wait()`
- `rerocc_coupleddma_read_monitor(stat_id)`

### 硬件的真实结构

对应实现：

- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`

你可以把它想成：

- 一个 `copyReqQ`
  - 保存等待执行的 copy descriptor
- 一个单状态机
  - `sIdle`
  - `sIssueGet`
  - `sWaitGet`
  - `sIssuePut`
  - `sWaitPut`
  - `sIssueFlag`
  - `sWaitFlag`

也就是说：

- **队列里可以排多笔**
- **真正执行时一次只跑一笔**

### 每条 DMA 指令到底在做什么

#### 1. `set_dst(dst, completion_addr)`

这条指令只把两件事写进本地寄存器：

- `dstAddrReg`
- `completionAddrReg`

它不发 DMA 请求，不进队列，不等待任何数据搬运。

所以它的语义是：

- **同步本地寄存器更新**
- **不是 copy 开始**
- **不是 copy 完成**

#### 2. `set_src(src, num_bytes)`

这条指令才真正把一笔 copy 描述符塞进 `copyReqQ`：

- `src`
- `dst = dstAddrReg`
- `len = num_bytes`
- `completion = completionAddrReg`

因此：

- 如果 queue 满了，这条指令会先阻塞在发射阶段；
- 一旦成功入队，CPU 就继续走；
- 真正的 copy 以后才由后台状态机执行。

所以它的语义是：

- **发射阶段可能阻塞**
- **发射成功后异步执行**

#### 3. `wait()`

`wait()` 的硬件 ready 条件非常苛刻：

- 必须 `!respValid`
- 必须 `!dmaBusy`

只有当 DMA 当前完全空闲，它才会接受这条命令并立刻返回一个 `rd`。

所以：

- `wait()` 本质上是一个**阻塞等待直到 DMA 空闲**的同步查询
- 返回值现在基本就是 `0`
- 它的重要性不在“返回什么”，而在“只有空闲才让你返回”

#### 4. `read_monitor(stat_id)`

这是同步读监控计数器：

- `srcCmdCount`
- `dstCmdCount`
- `reqCopyBytes`
- `cycleCount`
- `effectiveBytes`

它只读取当前值：

- **不是等待完成**
- **不是 fence**

#### 5. `sfence`

在 Coupled-DMA 里，`funct=0` 对应的是共享 SPM 地址翻译缓存失效：

- 它只做 `spmInvalidate := true`

所以：

- 这是 **translation cache flush**
- 不是 DMA completion fence

### DMA 真正怎么执行一笔 copy

每笔 copy 都按下面顺序跑：

1. `Get` 读源地址
2. 等 `D` 通道回数据
3. `Put` 写目的地址
4. 如果还有剩余字节，继续下一轮
5. 最后向 `completion_addr` 写一个字节的 `1`

也就是说：

- 这不是“纯内部搬运”
- 它会真的向内存系统发 TileLink `Get/Put`
- completion flag 也是内存写，不是内部布尔位

### DMA 的性能与参数约束

#### 1. 对齐决定传输宽度

Coupled-DMA 会检查：

- `src` 是否 beat 对齐
- `dst` 是否 beat 对齐
- `remaining >= beatBytes`

如果三者都满足：

- 走一整拍宽度传输

否则：

- 退化成 **按 1 byte 搬运**

这意味着：

- 功能上没问题
- 性能上可能会差非常多

#### 2. `set_dst` 必须先于 `set_src`

因为 `set_src()` 用的目的地址并不是它自己的参数，而是之前 latch 住的：

- `dstAddrReg`
- `completionAddrReg`

如果你先调 `set_src()`：

- DMA 会用上一次留下来的 `dst/completion`
- 这是协议错误，不是硬件帮你兜底的场景

#### 3. `len = 0` 不是非法，而是“只写完成标志”

状态机在 `sIdle` 取到请求后会判断：

- `len == 0` 直接跳到 `sIssueFlag`

也就是说：

- 零长度 copy 仍会写 completion flag
- 不会读写数据主体

#### 4. 若开启 shared-spad xlate，地址可能阻塞在 PTW

如果 Coupled-DMA 打开了共享 SPM 页表翻译：

- 地址在 range 内但 TLB 没命中时，会去发 PTW 请求；
- 在 PTW 回来之前，`sIssueGet` 或 `sIssuePut` 会被挡住；
- 如果 VPN 超出 `pte_count`，硬件会直接断言。

因此：

- `sfence` 与 `spm xlate` 配置类指令只影响地址翻译；
- 它们不是 copy completion 的替代物。

## Gemmini：真正的“复杂点”在于它是多级前端 + 三条后端控制器

### 先说结论

Gemmini 不是一个“发命令就立刻算完”的黑盒。它至少有四层：

1. **RoCC front-end**
   - 收指令
2. **展开 / 微码前端**
   - `LoopUnroller`
   - `LoopMatmul`
   - `LoopConv`
   - `CmdFSM`
3. **Reservation Station**
   - 统一做 hazard/依赖/分流
4. **三条真正干活的控制器**
   - `LoadController`
   - `ExecuteController`
   - `StoreController`

核心结论只有一句：

- **大多数 Gemmini 指令在软件看来都是“发射成功后异步执行”，不是同步完成。**

### 先分三类指令看，不要一上来背 funct 编号

#### 第一类：底层 RISC 风格指令

这类是 pipeline runtime 和大多数 baremetal 测试真正常用的：

- `CONFIG_CMD`
- `LOAD_CMD / LOAD2_CMD / LOAD3_CMD`
- `STORE_CMD`
- `PRELOAD_CMD`
- `COMPUTE_AND_FLIP_CMD`
- `COMPUTE_AND_STAY_CMD`
- `FLUSH_CMD`
- `COUNTER_OP`
- `CLKGATE_EN`
- `SPM_XLATE_*`

#### 第二类：loop 指令族

- `LOOP_WS` 及其配置子命令
- `LOOP_CONV_WS` 及其配置子命令

这类不是直接干活，而是“描述一个大循环”，再由内部前端展开成底层 RISC 指令流。

#### 第三类：更高层 CISC 指令族

- `CISC_CONFIG`
- `ADDR_AB`
- `ADDR_CD`
- `SIZE_MN`
- `SIZE_K`
- `RPT_BIAS`
- `RESET`
- `COMPUTE_CISC`

这类进一步把“矩阵计算任务描述”交给前端 FSM/tiler。

### 读 Gemmini 代码时真正要看哪几份

如果你想从原理走到代码，推荐顺序是：

1. ISA 编号和字段编码
   - `generators/gemmini/src/main/scala/gemmini/GemminiISA.scala`
2. 软件宏
   - `generators/gemmini/software/gemmini-rocc-tests/include/gemmini.h`
   - `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_gemmini_spm_xlate.h`
3. 顶层前端和特殊管理指令
   - `generators/gemmini/src/main/scala/gemmini/Controller.scala`
4. 后端三控制器
   - `generators/gemmini/src/main/scala/gemmini/LoadController.scala`
   - `generators/gemmini/src/main/scala/gemmini/ExecuteController.scala`
   - `generators/gemmini/src/main/scala/gemmini/StoreController.scala`
5. 依赖/队列层
   - `generators/gemmini/src/main/scala/gemmini/ReservationStation.scala`
6. 宏指令展开层
   - `generators/gemmini/src/main/scala/gemmini/LoopUnroller.scala`
   - `generators/gemmini/src/main/scala/gemmini/LoopMatmul.scala`
   - `generators/gemmini/src/main/scala/gemmini/LoopConv.scala`
   - `generators/gemmini/src/main/scala/gemmini/CmdFSM.scala`

## Gemmini 底层 RISC 指令族：逐类理解

### 1. `config_ex / config_ld / config_st / config_norm`

这些指令的共同点是：

- 它们主要在更新 controller 或 reservation station 中的状态寄存器；
- 大多数情况下并不等待已有 load/store/compute 全部做完；
- 但某些配置会被硬件限制只能在安全窗口改。

#### `config_ex`

它更新的是执行路径的关键状态：

- dataflow
- activation
- `in_shift`
- `acc_scale`
- `a_transpose`
- `b_transpose`
- `a_stride`
- `c_stride`

对应代码：

- `generators/gemmini/src/main/scala/gemmini/ExecuteController.scala`

需要特别记住的一点：

- ExecuteController 只在 `!matmul_in_progress` 且没有待完成 ROB completion 时才接受这类配置。

所以它的语义是：

- **配置类指令**
- **发射时可能因为执行流水线没排空而阻塞**
- **返回不代表之前全部 load/store 完成**

#### `config_ld`

它配置的是某个 mvin state：

- stride
- scale
- shrink
- block mvin stride
- pixel repeats
- state id

对应代码：

- `generators/gemmini/src/main/scala/gemmini/LoadController.scala`

关键约束：

- `pixel_repeats > 1` 只有在 first-layer optimization 打开时才合法；
- 真正发 mvin 时，硬件断言 `block_stride >= rows`；
- 零字节 mvin 会断言。

所以它的语义是：

- **本地配置更新**
- **不是执行屏障**

#### `config_st`

它配置的是 mvout / pooling / activation / acc_scale：

- stride
- activation
- pooling 参数
- output rows/cols
- padding

对应代码：

- `generators/gemmini/src/main/scala/gemmini/StoreController.scala`

关键约束：

- pooling 模式下不允许 block-mvout；
- full accumulator row read 也不允许 block-mvout。

所以它同样是：

- **配置类**
- **不是完成屏障**

#### `config_norm`

这是 normalization / layernorm / igelu 相关的配置：

- `q_const`
- `qb/qc`
- stats id
- activation 高位等

它本质上也是：

- **更新 store/normalization 侧状态**
- **不是计算完成等待**

### 2. `mvin / mvin2 / mvin3`

软件宏：

- `gemmini_mvin`
- `gemmini_extended_mvin`
- `gemmini_extended_mvin2`
- `gemmini_extended_mvin3`

对应硬件：

- `LoadController.scala`

这三条指令的区别不是语义上的“同步/异步”差异，而是：

- 它们使用不同的 load state id
- 常被用来区分：
  - A
  - B
  - D / bias

从软件视角看，它们的共同语义是：

- 只是把一次 load 请求交给 LoadController / DMA tracker；
- 发射成功后，真正的内存搬运在后台完成；
- 指令返回不代表数据已经到了 scratchpad。

更准确地说：

- **它们是异步的搬运发起指令**
- **不是同步 copy**

关键约束：

- `rows/cols` 编码在 `rs2` 中；
- 实际读字节数不能为 0；
- 若 `stride == 0` 且不是 all-zeros 特例，硬件会把多个逻辑行压成一次物理行读取并用 repeats 复用；
- `local_addr` 的地址类型决定写的是普通 spad 还是 accumulator 视图；
- `shrink` 会影响宽度与缩窄方式。

### 3. `preload`

软件宏：

- `gemmini_preload`
- `gemmini_preload_zeros`

硬件：

- `ExecuteController.scala`

`preload` 不等于“开始算完一个 tile”。它更准确的意义是：

- 准备 B/D 与 C 相关的路径状态；
- 为随后的 `compute_*` 提供阵列输入和输出目的地。

从时间语义上看：

- 它通常和 compute 一样，进入 execute pipeline；
- 发射成功以后，真正送数、阵列推进、写回仍是后台进行。

所以：

- **它不是完成屏障**
- **它是执行流水线的一部分**

### 4. `compute_preloaded / compute_accumulated`

软件宏：

- `gemmini_compute_preloaded`
- `gemmini_compute_accumulated`

硬件：

- `ExecuteController.scala`

它们的本质是：

- 把 A 与 B/D 的 scratchpad/accumulator 地址和 shape 送入阵列执行控制器；
- 真正的阵列推进、hazard 检查、flush、写回由 ExecuteController 后续完成。

从软件时序看：

- 指令能发进去，只表示“这项计算被排入执行流水线”
- 不表示结果已经可读

所以：

- **异步**
- **不是完成阻塞**

### 5. `mvout`

软件宏：

- `gemmini_mvout`
- `gemmini_extended_mvout`

硬件：

- `StoreController.scala`

它的作用是：

- 从 scratchpad / accumulator 把结果搬回 DRAM

但它和 `mvin` 一样：

- 指令返回时通常只是 store 请求已经被接受；
- 真正写回 DRAM 仍在后台。

所以：

- **异步**
- **不是“结果已经在 DRAM 可见”的保证**

### 6. `gemmini_flush(skip)`：它刷的是 TLB，不是算子流水线

这是最容易被名字误导的一条。

软件宏：

- `gemmini_flush(skip)`

硬件实现：

- `Controller.scala`

它做的事情是：

- 给 Gemmini 前端 TLB / address translation logic 发一个 flush 请求；
- `skip` 会影响 `flush_skip / flush_retry` 策略。

它**没有**做下面这些事情：

- 不等待所有 `mvin/mvout` 完成
- 不等待所有 compute 完成
- 不等 execute mesh drain 完

更直白一点：

- **`gemmini_flush()` 是地址翻译/TLB flush**
- **不是“把 Gemmini 算完”**

而 ExecuteController 里确实还有一个叫 `flush` 的内部状态，但那是：

- **OS 数据流下，用来把阵列内部残余波前排空的硬件内部步骤**
- 不是软件看到的 `FLUSH_CMD` 的意思

这两个“flush”绝不能混为一谈。

### 7. `gemmini_fence()`

软件宏里它只是：

- `asm volatile("fence")`

也就是说：

- 它不是一条 Gemmini funct 指令；
- 它没有 Gemmini 专属硬件解码；
- 它是 Rocket 主核自己的 fence 指令。

那它到底有没有“等待 Gemmini”的效果？

答案分模式：

#### 直接挂在 CPU tile 上的 Gemmini

此时 Rocket 的 `fence` 会观察当前 tile 的 `io.rocc.busy`。

而直连 Gemmini 顶层的 `io.busy` 又包含：

- raw command queue
- loop unroller
- reservation station
- scratchpad busy
- 各控制器 busy

所以在**直连 Gemmini**模式下：

- `gemmini_fence()` 确实可以作为一个“等本地 Gemmini 不忙”的 host-side barrier。

#### ReRoCC 管理下的远端 Gemmini

这里就完全不一样了。

当前 CPU tile 看到的不是远端 Gemmini 自己的 `busy`，而是 **本地 ReRoCC client** 的 `busy`。

而 ReRoCC client 的 `busy` 主要来自：

- acquire/release 状态机
- cfg credit 未回满
- fence 状态

其中 credit 是在收到 `sInstAck` 时回补的；而 `sInstAck` 只代表：

- 远端 manager 已经把指令收进自己的队列

不代表：

- 远端 Gemmini 已经执行完成

所以在 **ReRoCC 模式** 下：

- **`gemmini_fence()` 不能替代 `rr_fence(cfg)`**
- 它最多只能保证主核侧的顺序性，或者等本地 client 不忙
- 它不能单独保证“远端 Gemmini 真的算完”

如果你只记一个结论：

- **直连 Gemmini：`gemmini_fence()` 很有意义**
- **ReRoCC 远端 Gemmini：真正的完成屏障是 `rr_fence(cfg)`**

### 8. `counter_op`

这是 Gemmini 的同步计数器接口。

软件上表现为：

- `counter_read()`
- `counter_configure()`
- `counter_snapshot_take()`

硬件上走：

- `Controller.scala`
- `CounterController`

语义：

- **同步管理指令**
- 对读操作来说是 **阻塞直到 resp 返回**
- 但它不是计算 completion barrier

### 9. `spm_xlate_cfg / range / flush / fault`

这些指令都属于“共享 SPM 地址翻译配置”，而不是 GEMM 本体。

软件头：

- `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_gemmini_spm_xlate.h`

硬件：

- `generators/gemmini/src/main/scala/gemmini/Controller.scala`

四条指令的语义分别是：

- `spm_xlate_cfg`
  - 写 PTBR、`pte_count`、`page_shift`、enable
  - **配置类，立即更新寄存器**
- `spm_xlate_range`
  - 写翻译生效的虚拟地址窗口
  - **配置类**
- `spm_xlate_flush`
  - 清 fault latch，并把 cache epoch 推进
  - **translation cache flush，不是完成 fence**
- `spm_xlate_fault`
  - 读当前 fault latch
  - **同步读，返回值阻塞**

关键点：

- `cfg/range/flush` 都不是等待 Gemmini/DMA 完成的屏障；
- 它们只是改共享 SPM 虚实地址翻译行为。

### 10. `CLKGATE_EN`

这条指令只控制 Gemmini 的 clock gate 使能位。

但实现里有一个很关键的约束：

- 只有 `!io.busy` 时才会真正更新 `clock_en_reg`

所以：

- **这不是随时可改的配置**
- 最好只在 Gemmini 空闲时使用

## Gemmini 高层宏指令：loop 与 CISC 不是“同步大指令”

### 1. `LOOP_WS` / `LOOP_CONV_WS`

它们看起来像“一条大矩阵乘 / 大卷积指令”，但硬件并不是这样执行的。

它们的真实角色是：

- 先用若干 `CONFIG_*` 命令把 loop 参数写到内部寄存器里；
- 再在 run 命令到来时，由内部 unroller/FSM 持续吐出底层：
  - load
  - preload
  - compute
  - store

对应代码：

- `generators/gemmini/src/main/scala/gemmini/LoopUnroller.scala`
- `generators/gemmini/src/main/scala/gemmini/LoopMatmul.scala`
- `generators/gemmini/src/main/scala/gemmini/LoopConv.scala`

所以：

- loop 指令不是“单条同步大计算”
- 它本质上还是一层“前端展开器”

### 2. CISC 指令族

`CISC_CONFIG / ADDR_AB / ADDR_CD / SIZE_MN / SIZE_K / RPT_BIAS / COMPUTE_CISC`
也是同样的思想：

- 先把任务描述写进前端 FSM；
- `COMPUTE_CISC` 只是触发 tiler 去做后续展开；
- 真正落地还是底层 load/preload/compute/store 流。

对应代码：

- `generators/gemmini/src/main/scala/gemmini/CmdFSM.scala`

所以它们的语义同样是：

- **高层描述 / 触发**
- **不是同步完成**

## “同步 / 异步 / 阻塞 / 非阻塞”真正怎么判

为了避免概念混乱，下面给出一个更严格的判定表。

### ReRoCC

| 操作 | 发射阶段是否可能阻塞 | 发射后是否异步 | 返回时是否已完成 | 备注 |
| --- | --- | --- | --- | --- |
| `rr_acquire_cfg` | 会 | 否 | 是，等 acquire ack | helper 自带读回 |
| `rr_release` | 可能短暂阻塞 | 是 | 否 | 只是发起 release |
| `rr_set_opc` | 一般不会长阻塞 | 否 | 只是本地生效 | 不是屏障 |
| `rr_read_csr` | 可能 | 否 | 是 | 读 CSR 当下值 |
| `rr_fence` | 会 | 否 | 是，等远端 manager unbusy | ReRoCC 模式下最重要 |

### Coupled-DMA

| 操作 | 发射阶段是否可能阻塞 | 发射后是否异步 | 返回时是否已完成 | 备注 |
| --- | --- | --- | --- | --- |
| `set_dst` | 几乎不阻塞 | 否 | 否，只是写 latch | 必须先于 `set_src` |
| `set_src` | 会，队列满则卡 | 是 | 否 | 真正 copy 后台执行 |
| `wait` | 会，一直等到 DMA idle | 否 | 是 | 这是 completion primitive |
| `read_monitor` | 会，等 resp | 否 | 不涉及 | 读当前统计值 |
| `sfence` | 几乎不阻塞 | 否 | 否 | 只清 xlate cache |

### Gemmini

| 操作 | 发射阶段是否可能阻塞 | 发射后是否异步 | 返回时是否已完成 | 备注 |
| --- | --- | --- | --- | --- |
| `config_ex/ld/st/norm` | 会，取决于前端与控制器状态 | 否 | 否 | 配置更新为主 |
| `mvin/mvin2/mvin3` | 会 | 是 | 否 | 真正 DMA 后台完成 |
| `preload` | 会 | 是 | 否 | 执行流水线的一部分 |
| `compute_preloaded/accumulated` | 会 | 是 | 否 | 真正阵列推进后台完成 |
| `mvout` | 会 | 是 | 否 | 真正回写 DRAM 后台完成 |
| `gemmini_flush` | 一般不会久阻塞 | 否 | 否 | 刷 TLB，不是完成屏障 |
| `gemmini_fence` | 会，取决于当前 tile 的 `io.rocc.busy` | 否 | 视模式而定 | 直连有用，ReRoCC 下不能替代 `rr_fence` |
| `counter` | 会，等 resp | 否 | 不涉及 | 同步管理类 |
| `spm_xlate_fault` | 会，等 resp | 否 | 不涉及 | 同步读 fault |
| `spm_xlate_cfg/range/flush` | 一般不会久阻塞 | 否 | 否 | 只是改翻译状态 |
| `LOOP_* / CISC_*` | 会，看前端是否接收 | 是 | 否 | 只是更高层任务描述 |

## 你真正应该怎么等“完成”

这部分是阅读和写代码时最实用的结论。

### 场景 1：你在直连 Gemmini 模式下

如果 Gemmini 就直接挂在当前 CPU tile 上：

- 常见做法是：
  - 发 `mvin / preload / compute / mvout`
  - 然后 `gemmini_fence()`

因为此时 `fence` 能观察当前 Gemmini 的 `io.rocc.busy`。

### 场景 2：你在 ReRoCC 管理的远端 Gemmini / DMA 模式下

如果工作是经由 `ReRoCC client -> manager -> pair wrapper -> Gemmini/DMA` 这条路径发出去的：

- **真正的完成屏障优先用 `rr_fence(cfg)`**

原因是：

- `rr_fence(cfg)` 等的是远端 manager 真正 unbusy；
- `gemmini_fence()` 只看本地 client busy，不足以代表远端执行完成；
- `rr_release(cfg)` 只是 release 请求，不是 completion barrier。

### 场景 3：你只关心 DMA copy 本身是否结束

有两种思路：

- 用 DMA 自己的：
  - `rerocc_coupleddma_wait()`
- 用更通用的 cfg 级屏障：
  - `rr_fence(cfg)`

前者适合：

- 你就是在盯 DMA 这个 accelerator 的空闲点

后者适合：

- 你要的是“这个 cfg 下面所有 routed work 都排空”
- 特别是 pair-manager 下同时混了 Gemmini 与 DMA 的情况

### 场景 4：你只想刷地址翻译，不想等执行

那就不要用 fence：

- Gemmini 侧：
  - `gemmini_flush(skip)` 或 `spm_xlate_flush`
- Coupled-DMA 侧：
  - `sfence`

这些都只是：

- **translation / TLB 相关的 flush**
- **不是执行完成屏障**

## 最容易踩的约束与坑

### 1. `rr_release()` 不是安全换绑点

如果你在 `rr_release(cfg)` 后立刻：

- 读 `RRCFG`
- 写 `RROPC`
- 重新 acquire / reuse

你会落入 release 仍在进行的窗口。

在 ReRoCC 语义里，更稳妥的顺序是：

- 先 `rr_fence(cfg)`，确认远端空闲
- 再做 release / restore / rebind

对 `pipeline-runtime` 当前代码，再补一条实现约束：

- 它已经在 release wrapper 里做了
  “`rr_release(cfg)` 后读回同一 `RRCFGx`”
  的握手收口；
- 这不能替代 `rr_fence(cfg)` 的 completion 语义，
  但足以避免
  “scope 已被软件标成无效/可复用，
  而底层 release ack 还没回来”
  这一类软件可见不一致窗口。

### 2. `gemmini_flush()` 不是“把阵列算完”

如果你把 `gemmini_flush()` 当成 compute fence：

- 结论会是错的

它刷的是前端翻译状态，不是后端执行流水线。

### 3. ReRoCC 模式下，`gemmini_fence()` 不能替代 `rr_fence(cfg)`

这是当前项目最容易产生“看起来没问题，实际远端还在跑”的地方。

### 4. Coupled-DMA 必须先 `set_dst` 再 `set_src`

否则 `set_src` 用的是旧的 `dst/completion` latch。

### 5. Gemmini 的 `mvin` / `mvout` 不是同步 memcpy

它们只是把请求送进 controller / DMA tracker。

### 6. Gemmini 的一些参数不满足约束会直接断言

最值得先记住的几个：

- `mvin` 不能是 0 字节
- `block_mvin_stride >= rows`
- 若没开 first-layer optimization，`pixel_repeats` 不能大于 1
- pooling 模式下不能做 block-mvout
- full accumulator row read 不能配 block-mvout

## 最后给一份“如果你还是想读代码”的顺序

### 第一步：建立 CPU 侧心智模型

- `generators/rocket-chip/src/main/scala/tile/LazyRoCC.scala`
- `generators/rocket-chip/src/main/scala/rocket/RocketCore.scala`
- `generators/rocket-chip/src/main/scala/tile/CustomCSRs.scala`
- `generators/rocket-chip/src/main/scala/rocket/CSR.scala`

你要带着的问题去看：

- `cmd.ready` 怎么让一条自定义指令重放？
- `xd=1` 怎么等待 resp？
- `fence` 为什么会看 `io.rocc.busy`？
- 自定义 CSR 的 `stall` 怎么把 CSR 访问卡住？

### 第二步：读 ReRoCC

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_control.h`
- `generators/rerocc/src/main/scala/client/CSRs.scala`
- `generators/rerocc/src/main/scala/client/Client.scala`
- `generators/rerocc/src/main/scala/bus/Protocol.scala`
- `generators/rerocc/src/main/scala/manager/Manager.scala`

带着的问题：

- `rr_acquire_cfg` 为什么是阻塞的？
- `rr_release` 为什么不是？
- `rr_fence` 等的到底是什么？
- 为什么 `sInstAck` 不等于执行完成？

### 第三步：读 Coupled-DMA

- `generators/gemmini/software/gemmini-rocc-tests/include/rerocc_coupleddma.h`
- `generators/gemmini/src/main/scala/gemmini/GemminiCoupledDMA.scala`

带着的问题：

- 为什么 `set_dst` 只是写 latch？
- `set_src` 为什么只是入队？
- `wait` 为什么是真阻塞？
- 为什么未对齐会退化成 1 byte copy？

### 第四步：读 Gemmini 底层指令

- `generators/gemmini/src/main/scala/gemmini/GemminiISA.scala`
- `generators/gemmini/software/gemmini-rocc-tests/include/gemmini.h`
- `generators/gemmini/src/main/scala/gemmini/Controller.scala`
- `generators/gemmini/src/main/scala/gemmini/ReservationStation.scala`
- `generators/gemmini/src/main/scala/gemmini/LoadController.scala`
- `generators/gemmini/src/main/scala/gemmini/ExecuteController.scala`
- `generators/gemmini/src/main/scala/gemmini/StoreController.scala`

带着的问题：

- 哪些指令只是 config？
- 哪些指令只是 enqueue？
- ExecuteController 的内部 `flush` 为什么不是软件 `gemmini_flush()`？
- 结果到底什么时候才算真正写回？

### 第五步：最后再看 loop / CISC

- `generators/gemmini/src/main/scala/gemmini/LoopUnroller.scala`
- `generators/gemmini/src/main/scala/gemmini/LoopMatmul.scala`
- `generators/gemmini/src/main/scala/gemmini/LoopConv.scala`
- `generators/gemmini/src/main/scala/gemmini/CmdFSM.scala`

带着的问题：

- 它们怎样展开成底层指令流？
- 为什么它们仍然不是同步大指令？

## 最后一句总结

如果你读完整个项目只想记住一句话，那就是：

- **ReRoCC 负责“把指令路由到哪”，Coupled-DMA 和 Gemmini 负责“真正干活”；大多数干活指令都是“发射后异步执行”，而 ReRoCC 模式下真正的完成屏障是 `rr_fence(cfg)`。**
