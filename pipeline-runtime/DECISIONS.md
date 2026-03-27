# Pipeline Runtime 冻结决策

## 1. 总体原则

1. 不引入 `mudnacsim/gemmini` 运行时分叉；只在当前 ISA/软件栈上实现需求。
2. 编排期决定逻辑布局，运行时只兑现物理资源与地址。
3. 一个 action 必须拥有独立 alias window、独立 shared-spad 页表、独立执行态。
4. 不再使用全局 shared-spad VA 切片去复用多个 action。

## 2. Artifact 与布局

5. canonical exporter 仍是 `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`。
6. canonical artifact 根目录仍是 `conference/HybridMapper/output/pipeline_runtime/bertmini/`。
7. `pipeline-runtime` 必须直接消费 pre-orchestrated mapping，不在执行期重建逻辑 buffer/tensor 位置。

## 3. Runtime 与硬件接口

8. shared-spad xlate 继续复用 Gemmini controller 现有 `CFG/RANGE/FLUSH/FAULT` 接口。
9. 每个 action 把自己的一整段 alias VA window 安装到所分配的所有 Gemmini manager 上。
10. PTE 查找语义按 `(vaddr - range_base)` 解释；因此 alias window 必须是一段连续区间。
11. 物理页分配策略当前保持 all-bank 语义。

## 4. 多 action 方向

12. 本轮已经冻结“topology/execution state 归 action 所有”，不再回到 runtime 全局单例。
13. 但当前仍未宣称已经支持“多个 active action 同时执行”。
14. 在真正并发前，不允许再把 runtime-global trace/fatal/worker lifecycle 误写成“已经 action 化”。

## 5. ReRoCC/Gemmini 并发边界

15. 现有软件栈下，Gemmini 指令固定走 `custom3`，DMA 固定走 `custom2`。
16. 因此单 hart 当前稳定拥有的是：
  - 1 条 Gemmini live route lane
  - 1 条 DMA live route lane
17. `cfg` 数量不等于 Gemmini 并发 issue lane 数量。
18. 未来如果要实现 6 个 action、6 个 CPU 并发管理，还需要单独设计 cfg/opcode 竞争策略，不能靠当前软件路径自然得到。

## 6. 调试规则

19. Linux/F2 卡点判断不能只看 process log，必须联查 `uartlog` 和 `heartbeat.csv`。
20. guest `stdout/stderr` 必须保持 unbuffered；必要时可以加额外 filler log 逼出关键 marker。
21. 若 runfarm 已确认卡死，先停 farm 再分析。
22. 等 Linux 启动要有耐心；heartbeat 前进时不要过早误判 boot hang。
    Linux 启动阶段，只要没有明确 boot error / panic / crash，就不要仅凭 UART 静默窗口把它记成新的卡点。
    慢启动本身不是 blocker，至少要等到明确报错，或已经进入用户态 workload 后再次停住。

## 7. 当前临时策略

23. activation 当前统一按 `RELU` 处理，这是调试期策略，不代表最终 artifact contract 已补齐。
24. 旧协作文档不再是规范来源；它们已归档，仅供追溯。
