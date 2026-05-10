# Pipeline Runtime Lessons Learned

## 1. 探针会扰动前沿

- 热路径文本日志本身就可能改变 guest 时序。
- 结论前提必须是：
  先确认新前沿不是 probe 造成的“假前移”。
- 因此当前默认策略是：
  coarse file log + breadcrumb + 极窄条件日志。

## 2. NUL 尾和页对齐日志不等于真实停点

- 如果 guest log 停在某一行，同时：
  - heartbeat 继续前进
  - 文件大小卡在页对齐附近
  - 文件尾出现大量 `NUL`
  那更可能是文件追加/刷盘路径也在阻塞。

## 3. stale image 比想象中更常见

- host shell 里 env 已经变了，不代表 guest image 一定是新内容。
- `firemarshal.env`、runner、runtime binary 三者任何一个没更新，现场推理都会失真。

## 4. boot 早期静默不能乱判 hang

- Linux boot 阶段，只要 heartbeat 还在前进，就不要单凭 `uartlog` 静默判新 blocker。

## 5. runner / crumb 文件不要 foreground sync

- 逐条 crumb 后立即 `sync` 会把前沿假性前移到 shell 层。
- 当前正确做法是依赖 wrapper 周期性 `sync`。

## 6. guest 文件比 UART 更可信

- 当前这条主线已经多次证明：
  `uartlog` 很容易停滞或丢失细节；
  guest 文件系统日志才是主证据面。

## 7. `TracerV` 先证 host pull 再猜 trace

- `TRACEFILE-C*` 为空，
  不足以证明 `TracerV` 本身坏了。
- 更高优先级的证据是 host-side：
  `first nonzero host pull`
  和 `flush summary`。
- 只有先确认 host 是否真的拉到 token，
  后面的 `TRACEFILE` / copy-back 分析才不会跑偏。

## 8. exact trigger 先走轻量 marker 路线

- `selector=3` 是否可用，
  应先在最小 baremetal marker 样例上验证。
- 如果 marker 放在长 prefill 之后，
  即使 `TracerV` 正常，
  也会呈现出
  `TRACEFILE=0`
  和“卡点在移动”的假象。
- 因此 workload-shaped 调试的第一步，
  是缩短 prefill 或前移 marker，
  而不是先加更多日志。

## 9. doneflag 不能再被当作 DMA 完成

- 历史调试已经验证：doneflag 路径会给出误导性完成信号，不能作为 Linux DMA
  completion 的主语义。
- 看到 `dma-wait-doneflag-poll phase=done` 只能说明 flag 变为可见，不能说明
  DMA manager、TileLink 路径、SPM 写入、scope release 和后续 token cleanup 都正确完成。
- 因此所有 pipeline-runtime 主线实验必须以 `hw_dma_fence()` / blocking wait 返回为
  DMA 完成证据；doneflag 只能是旁路观测。
- 如果某轮日志显示 doneflag polling 被启用，先修 profile / image freshness，不要继续基于
  该轮结果判断 runtime 卡点。

## 10. artifact 读取的逐 chunk 长日志会自干扰

- 2026-05-09 的 no-DMA F2 bisection 显示，8 KiB artifact read chunk 本身已经返回，
  但 guest sparse log 停在 `chunk-end` 的长 path 字符串中间。
- 这种形状优先解释为 regular-file `write(O_APPEND)` / guest block path 自干扰，
  不要直接归因成 artifact `read()` 或 Gemmini compute 卡死。
- artifact loader 这类循环只能保留低频进度日志；默认不要对每个小 chunk 写两条长 path 行。
- 需要定位读 offset 时，先在本地或单次窄窗口里打开更细的 stride，不要把高频日志作为固定 profile。

## 11. gdbserver frontier 断点要薄

- 2026-05-09 的 no-DMA compute GDB 轮次证明，密集软件断点加 `info locals` / 大量
  backtrace 会把 manager-by-manager compute 路径拖到 frontier timeout。
- 如果 timeout 时线程停在普通 C 代码窗口，并且此前持续命中后续 manager，不能直接判为硬件卡死。
- no-DMA compute 二分优先用薄断点：
  marker 后只看目标 manager 的 issue、matmul call/return、fence/drain/release 和最终 return。
- `gdbserver --once` 的 guest TCP 端口仍必须由 GDB 做第一连接；不要用 `nc` / `curl` / `telnet` 探活。

## 12. no-DMA 不应影响 artifact 读取

- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE` 当前不进入 `prt_gemmini_artifacts.c`；
  no-DMA 只短路 fixed-load/export-sync 和 C1/C2/C3/C5/C6 等真实 DMA 搬运。
- 如果现象停在 artifact read chunk 附近，优先按日志/guest block path 自干扰或 read chunk/log stride
  处理，不要把 no-DMA compute 开关本身当成读取路径变化。
- 2026-05-09 `worker-export-sync -> sync_stage_export_aliases()` 薄 GDB 轮次已证明
  `segment1/stage0/subbatch3` 的 export alias sync 返回 `rc=0`；下一步应继续切
  `prt_runtime.c:4604` 之后的 post-compute pipebuf release/publish 边界。

## 13. 复用 uartlog 时必须过滤本轮启动块

- FireSim runhost 上的 `sim_slot_0/uartlog` 可能保留上一轮 run 的尾部内容。等待
  `gdbserver --once` 时不能简单 grep 第一条 `[gdbserver] phase=listening`。
- 2026-05-09 no-DMA stage2 轮次中，未按本轮 `Script started on ...` 过滤，导致把旧
  listening 行误判成新 gdbserver 已就绪；GDB 首连得到 `Connection reset by peer`。
- 后续等待 listening 必须从本轮 `Script started` 之后的字节范围内匹配，且仍然禁止用
  `nc` / `curl` / `telnet` 探测 guest 端口。

## 14. cfg32 no-DMA compute 已完整通过

- 2026-05-09 `agfi-077451484fe3b63c3`、cfg32/NIC/noTrace、batch8 pairdummy gdbserver
  低噪声 profile 上，`PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1` 完整跑到：
  `Simulation complete` / `*** PASSED *** after 22734035102 cycles`。
- 这轮结果排除了 no-DMA 路线下的 artifact 读取、Gemmini compute、SPM xlate 和普通
  pipebuf 控制流作为当前主 blocker。
- 下一轮主线应回到真实 DMA/completion：DMA submit/wait、`hw_dma_fence()` / blocking wait、
  host buffer/direct DMA，以及依赖真实 DMA 完成的 producer publish。不要再把 no-DMA 或
  artifact read 当成首要嫌疑。

## 15. 性能实验优先用 summary-only trace

- 2026-05-10 no-DMA perf 第二轮 F2 证明，`TRACE_ENABLE=1` 的事件级 trace 可能明显扰动
  long-running compute：heartbeat 停在 `18317918397, 960` 后不再推进，UART 出现
  `rerocc_pipeline` RCU stall。
- 如果实验只需要 `model_compute_ns` / `model_exec_ns` / counter summary，不要开启
  event ring；使用 `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY=1`。
- summary-only trace 仍保留最终 trace 文件和 summary 字段，但跳过 `trace_events`
  分配、cycle 校准和 event dump，适合 ours/gemmini no-DMA 编排性能对比。
- 但 2026-05-10 第三轮 F2 也证明：summary-only 只是降低 trace 扰动，不保证
  `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=0` 的 no-DMA perf profile 能完成。若 summary-only
  仍出现 RCU stall 且没有 trace 落盘，先回到 known-good 的 cache-disabled/YAML 路径；
  `preprocess_ns` 与 `model_compute_ns` 分开记录，不要为了节省预处理时间牺牲可完成性。

## 16. perf profile 不要把 runner stdout 写进 guest rootfs

- 2026-05-10 cache-disabled no-DMA perf F2 仍 early-stall：UART 到 `S99run`，wrapper 到
  `after-child-spawn`，但 runner stage 和 trace 都没有出现，heartbeat 固定在
  `18326725699, 965`。
- 该轮和 2026-05-09 完整 PASS 的显著剩余差异之一是：
  perf 使用 `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=log`，known-good 使用 `uart`。
- 性能/profile 路线应默认保留 `uart`，避免把 wrapper/runner stdout 追加到 guest rootfs
  文件；最终性能比较仍看 trace summary 的 `model_compute_ns` / `model_exec_ns`，不是
  UART/log 预处理时间。

## 17. 复用 run_farm_tag 前必须清掉旧 runworkload manager

- 2026-05-10 no-DMA perf 第五轮 F2 被旧本地 watchdog 终止：旧 session
  `...runworkload-20260510-054251` 超时后调用 `terminaterunfarm --forceterminate`，按相同
  `run_farm_tag=pairbertb8d12s64perfcfg32nt` 终止了新实例
  `i-06c55736f7b960139`。
- CloudTrail 确认 `TerminateInstances` 来自当前 manager public IP `54.68.14.45`，
  不是 AWS 自动回收，也不是 guest/runtime 自己完成。
- 下一次 `launch` / `infrasetup` / `run` 前必须先确认没有同 tag 的旧 tmux session、
  orphan `firesim runworkload` 进程、以及 running/pending F2。workflow 已加入
  `stale-runworkload-check` fail-fast 防护。
