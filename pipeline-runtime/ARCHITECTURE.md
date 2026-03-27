# Pipeline Runtime 架构

## 1. 目标边界

当前主线目标不是做一个抽象的通用 runtime，而是把 `conference/HybridMapper` 导出的 pre-orchestrated mapping 落到 Gemmini/ReRoCC/CoupledDMA 执行路径上。

当前边界：

- 模型主线：`bertmini`
- 运行时不重新决定逻辑拓扑，只兑现编排结果
- 最终验收仍看 Linux on FireSim F2
- baremetal 和 host closure 主要用于快速回归与定位

## 2. 编排期与运行时的职责分割

### 2.1 编排期决定的内容

HybridMapper/exporter 现在应当决定：

- segment 需要的 accelerator 数量
- segment 需要的 SPM 页数
- stage 的逻辑执行视图
- 每个 tensor 的逻辑 SPM 位置
- 每类 pipeline buffer 的逻辑布局与 slot 关系
- ring/shared/isolate/bypass 等语义

也就是：

- `segmentSpmPageSpan`
- `bufferBinding*List`
- stage `execBaseVPage`
- stage `localSpm*List`
- `entry/export buffer id`
- `tensorStride/tensorSize`

### 2.2 运行时决定的内容

运行时只决定：

- action 实际拿到哪些 Gemmini/DMA manager
- action alias window 的实际 base VA
- action 私有页表的 backing 位置
- 逻辑页映射到哪些真实 all-bank 物理页

运行时不关心用户是否记住 DRAM 物理地址；这些由 runtime 内部维护。

## 3. Action 模型

当前一个 pipeline segment 对应一个 `prt_schedule_action_t`。

每个 action 现在拥有：

- `acc_source`
  编排要求对应到真实 manager 的分配结果
- `spm_source`
  该 action 需要的 weight/pipe/ring 页绑定
- `spm_xlate`
  该 action 私有 shared-spad 页表上下文
- `alias_base_va/alias_bytes`
  该 action 独占的 shared-spad alias VA window
- `exec`
  该 action 私有的拓扑和执行态

## 4. Action 私有执行态

本轮重构后，下面这些状态已经从 `prt_runtime_t` 搬到 `action->exec`：

- stage 线程上下文和 stage 数量
- stage 到 layer/acc/dma/tile/split/manager 的映射
- stage SPM shadow / lazy-load / bounce buffer
- pipebuf / ringbuf / isolate/shared pair
- action-local weight page bindings
- action-local topology alloc keys

这意味着：

- 不同 action 不再共享同一份 pipebuf/ringbuf 拓扑
- 不同 action 不再共享同一套 stage shadow/bounce 状态
- 当前线程通过 `prt_runtime_current_action()` 和 `prt_runtime_current_exec()` 解析自己正在服务的 action

## 5. Buffer 拓扑

当前仍支持所有 `C1..C8` 类型：

- `C1/C2`
  DRAM or dependent entry/export
- `C3`
  isolate without ring pair
- `C4`
  shared without ring pair
- `C5/C6`
  isolate with ring
- `C7/C8`
  all-ring entry/export

这些 buffer 统一被物化为：

- `prt_pipebuf_t`
- `prt_ringbuf_t`
- `prt_isolate_pair_t`
- `prt_shared_pair_t`

## 6. Shared-Spad Xlate

每个 action 当前拥有：

- 一个独立 alias window
- 一个独立页表上下文
- 一组安装到该 action 所获 Gemmini manager 上的 `range/PTBR/PTE`

硬件查表语义仍然是：

- 用 `(vaddr - range_base)` 得到 action-local vpage
- 再到该 action 的页表里查 PTE

因此 action 之间不再共享同一个全局 VA 空间切片。

## 7. 当前并发边界

这次重构完成后，“action 私有资源与执行态”已经具备基础。

但当前还没有完成：

- 多 active action 同时推进的调度器
- 多 action 同时运行的 worker 生命周期管理
- runtime-global trace/fatal/state 的 action 化
- ReRoCC cfg/opcode lane 竞争管理

所以当前真实结论是：

- “多 action 私有上下文”已经落地
- “多 active action 同时执行”还未落地

## 8. 与历史卡点的关系

之前 Linux/F2 卡点的一个重要混淆因素是：

- shared-spad xlate 已经 action 私有
- 但 topology/pipebuf/stage shadow 仍是 runtime 全局单例

本轮已经把这个混淆因素去掉了。是否因此修复了历史 Linux/F2 卡死，仍需要新的 baremetal/Linux 实证。
