# Gemmini/ReRoCC/DMA 自定义指令调试策略

更新时间：`2026-05-10 15:10 UTC`

本文记录 `pipeline-runtime` 在 Linux/F2 上调试 Gemmini、ReRoCC、CoupledDMA
自定义指令时的推荐方法。目标不是增加更多热路径日志，而是在少用 F2、少扰动程序轨迹的前提下，
把卡点稳定分到软件调度、DMA completion、ReRoCC scope、Gemmini fence 或硬件不可观测等待。

2026-05-10 更新：程序可能在软件 sentry 或 printf 来得及输出前就已经卡在 custom instruction。
因此后续卡死定位的主证据必须来自 FireSim 硬件侧观测；软件 marker、GDB 和 guest 文件日志只作为
辅助关联证据。硬件观测也不能直接从 12-pair/6-pair 大设计开始，应先按下面顺序验证观测本身。

相关入口：

- [`gdbserver_integration_sop.md`](gdbserver_integration_sop.md)
- [`observability.md`](observability.md)
- [`dma_completion_static_note_20260505.md`](dma_completion_static_note_20260505.md)
- [`pipeline_runtime_optimization_actions_20260505_draft.md`](pipeline_runtime_optimization_actions_20260505_draft.md)
- [`../constraints/lessons_learned.md`](../constraints/lessons_learned.md)
- [`../../debug_records/20260509T193535Z_no_dma_compute_full_pass_cfg32_gdbserver.md`](../../debug_records/20260509T193535Z_no_dma_compute_full_pass_cfg32_gdbserver.md)
- [`../../debug_records/20260510T121300Z_no_dma_segment2_stage2_worker_entry_any_hit.md`](../../debug_records/20260510T121300Z_no_dma_segment2_stage2_worker_entry_any_hit.md)

## 1. 背景判断

当前困难来自三类事实：

1. `hw_dma_fence()`、`gemmini_fence()`、部分 ReRoCC fence / CSR 访问会进入硬件等待。
   如果硬件不接受、不返回或内部 busy 不清零，软件 timeout 很难包住这条指令。
2. `gdbserver` 是有用的控制流工具，但会明显扰动运行轨迹。软件断点、all-stop、条件断点、
   `thread apply all bt`、`Ctrl-C` 和 TLS 表达式都可能改变时序或直接中断自动化脚本。
3. FireSim/F2 每轮成本高。尤其是重新构建这个规模的 bitstream 可能需要约一天，因此硬件观测必须
   先经过软件证据门槛，不能为了单个猜测临时加一批一次性信号。

因此推荐顺序改为：

```text
硬件观测 RTL 本地审计
  -> 1C1P baremetal/metasim 验证观测功能
  -> 1C1P Linux/F2 小规模验证可用性
  -> 6P2C metasim routing smoke
  -> 6P2C Linux/F2 复现与窄窗口采样
  -> 仍不足时才构建 AutoILA 版本
```

## 0. 硬件优先观测路线

新增硬件观测覆盖自定义指令链路，而不是全 CPU 指令流：

- ReRoCC client：RoCC cmd 接收、credit/status/PTBR/acquire 阻塞、`mInst` beat、`sInstAck`/`sWrite`/`sUnbusyAck`。
- ReRoCC manager：`mInst` 接收、`inst_q` enqueue/dequeue、wrapped RoCC cmd fire、release/unbusy 等待。
- `GemminiCoupledDMAPairWrapper`：custom2/custom3 路由、Gemmini/DMA cmd ready/fire、resp、busy。
- `GemminiCoupledDMA`：dest/src/fence/monitor/sfence 接收、`readyFence` 阻塞原因、DMA FSM、TL A/D 进展、SPM PTW。

输出机制：

- `SynthesizePrintf` 只打印事件、状态切换和 watchdog；不打印每周期状态。
- `PerfCounter.identity` 低频采样 packed state；默认 `autocounter.read_rate=10000` 或 `100000`。
- TracerV 保留 TraceIO，先用 instruction trigger 或宽 cycle trigger；拿到 hang cycle 后改成窄 cycle window。
- AutoILA 暂不默认启用。只有 printf/AutoCounter/TracerV 仍不能定位时，才构建 `ILADepth2048_WithAutoILA_...` 版本。

落地配置：

- 1C1P metasim build recipe：
  `sims/firesim/deploy/config_build_recipes_local_metasim_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_hwdebug.yaml`
- 1C1P baremetal metasim runtime：
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_cfg32_slot_smoke_quick_1c1p1_hwdebug.yaml`
  和
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_rr_release_probe_1c1p1_hwdebug.yaml`
- 1C1P Linux/F2 build recipe：
  `sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
- 1C1P Linux/F2 runtime：
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug_uartprobe.yaml`
- 6P2C build/runtime 起点：
  `sims/firesim/deploy/config_build_recipes_local_metasim_gemmini_rerocc_pairmanager_dummy8x8_2c6p6_sbus64_hwdebug.yaml`
  和
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_cfg32_slot_smoke_quick_2c6p6_hwdebug.yaml`

验收标准：

- 1C1P metasim 通过时，synth print 中必须能串起：
  `rrc-client-cmd -> rrc-client-inst-beat -> rrc-manager-inst-enq -> rrc-manager-cmd-fire -> pair-wrapper-* -> coupled-dma-*`。
- 1C1P Linux/F2 通过时，必须回收 `uartlog`、`heartbeat.csv`、`TRACEFILE-C0`、synth print 文件和
  `AUTOCOUNTERFILE*.csv`；`uartprobe.status` 应记录正常结束。
- 6P2C 前不得跳过 1C1P。若 1C1P 已经暴露观测信号缺失或误报，先修硬件观测，不上大 bitstream。

### 0.1 2026-05-10 本地验证记录

已完成的本地门槛：

- 1C1P FireSim `verilog` 目标通过：
  `FireSimGemminiReRoCCPairDummy8x8C1P1Sbus64DebugConfig` +
  `WithPrintfSynthesis_WithAutoCounter_WithSynthAsserts_FRFCFS16GBQuadRank_BaseF2Config`。
  Golden Gate 报告选择了 14 个 AutoCounter 信号、31 个 synthesized printf、1814 个 synthesized assert。
- 2C6P Chisel/FIRRTL elaboration 通过：
  `FireSimGemminiReRoCCPairDummy8x8C2P6Sbus64DebugConfig`。
- 1C1P FireSim manager metasim `builddriver -> launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
  已跑通，最终观测链验证结果在：
  `sims/firesim/deploy/results-workload/2026-05-10--15-07-20-rerocc-lc-baremetal-cfg32-slot-smoke-quick-local-metasim-rerocc-baremetal-cfg32-slot-smoke-quick-1c1p1-hwdebug/`。

本轮发现并修正的静态问题：

- `rerocc_control.h` 已把 `RR_MAX_CFGS` 扩到 32，并把 `CSR_RRCFG16..31` 纳入 `RR_CSR_LIST`。
  `rerocc_lc_cfg32_slot_smoke.c` 仍无条件拼接本地 `RR32_EXTRA_CSR_LIST`，导致 `switch case`
  重复，baremetal 重新构建失败。
- 修复方式：只在 `RR_MAX_CFGS < 32` 的旧 header 上补本地 `RRCFG16..31` 列表；新 header
  直接复用 `RR_CSR_LIST`。这是兼容性修复，不改变运行路径。

1C1P metasim 判据：

- `metasim_stderr.out` 出现：
  `TracerV[0]: first nonzero host pull`、
  `rrc-client-cmd`、`rrc-client-inst-beat`、`rrc-manager-inst-enq`、
  `rrc-manager-cmd-fire`、`pair-wrapper-dma-cmd`、`pair-wrapper-gemmini-cmd`、
  `coupled-dma-copy-start`、`coupled-dma-copy-done`、`rrc-manager-release-resp`。
- `AUTOCOUNTERFILE0.csv` 已回收，包含 ReRoCC manager/client、pair wrapper、CoupledDMA
  state 与 event 列。
- `TRACEFILE-C0` 非零，final run 中为 128587 bytes。
- final run target 结果为 `*** PASSED *** after 17410 cycles`。

限制和不能支持的结论：

- `cfg32-slot-smoke` 的 DMA 子测仍使用软件 completion flag，因此这轮只能证明硬件观测链能看到
  CoupledDMA copy start/done，不能作为 DMA completion 正确性的证据。DMA completion 证据仍必须来自
  `hw_dma_fence()` / blocking wait 路径。
- 未跳过 Gemmini data check 的 1C1P metasim 曾返回 `tohost=1`。Trace 定位到 DMA copy 已完成，
  失败发生在 Gemmini `mvin -> mvout` 后的数据比较路径；它没有执行 OK marker。
  这与本文已记录的“`mvin/mvout` 不是同步 memcpy”一致，不能把该失败归因于硬件观测插桩。
- 用 `CFG32_SLOT_SKIP_GEMMINI_DATA_CHECK=1` 的 final run 只验证 custom instruction 路由、
  synthesized printf、TracerV 和 AutoCounter 链路；不验证 Gemmini 数据正确性。后续若要把
  baremetal smoke 作为功能回归，应按 `p12_pairmanager_rerocc_cfg32_hardware_fix_plan_20260419.md`
  改成最小 compute 测试，而不是只依赖 `mvin/mvout`。
- `rrc-manager-unbusy-stuck age=1024` 在 final run 中出现一次后随即 `unbusy-ack` 并通过。
  当前阈值适合暴露长等待，但短 metasim 中应按上下文判读；出现 `stuck` 字样不等于死锁。

### 1.1 快速定位“卡死在哪条指令”

本文里“硬件观测”的第一目标不是泛泛地看更多内部状态，而是在程序卡死时尽快回答：

```text
最后进入的危险指令是哪一个 wrapper / site？
它对应的静态 PC 或 asm label 是什么？
该指令是否已经发到硬件？
硬件侧是否看到该 op 完成或仍处于哪个等待状态？
```

推荐把所有可能不可返回的指令族都纳入同一套 sentry：

- `hw_dma_fence()` / `rerocc_coupleddma_wait()`
- DMA submit 的 RoCC 指令序列
- `gemmini_fence()`、Gemmini compute issue
- ReRoCC acquire / fence / release / CSR sentinel read
- SPM xlate flush / fault/status 相关 CSR

最小软件形态是在每条危险指令前后写非 TLS 全局 snapshot：

```text
enter_seq++
last_enter_site_id
last_enter_pc
last_enter_kind
last_enter_manager
last_enter_token
last_enter_segment/stage/subbatch
<dangerous custom instruction>
exit_seq++
last_exit_site_id
last_exit_pc
```

如果卡死时看到 `enter_seq > exit_seq`，最后的 `last_enter_site_id` 就是首要嫌疑指令。
`site_id` 必须能离线映射到 `file:line`、函数名、asm label 和预期 custom instruction。
精确 PC 不应依赖硬件 RoCC command 自带字段，因为常规 RoCC command 通常不携带用户态 PC；
需要精确定位时，应由软件 wrapper 在指令前用固定 asm label 或 `auipc` 记录 PC。

如果 GDB 还能读内存，这一层不需要新 bitstream。如果 GDB 不能抢回或目标线程卡在不可中断指令，
下一层才考虑“程序驱动的可综合输出”：软件在危险指令前后写一个低频 debug MMIO/CSR，
硬件 debug block latch `enter` 事件，并在超过阈值仍没看到对应 `exit` 时输出一次：

```text
PRT_STUCK seq=<n> site=<id> pc=<pc> kind=<dma_fence|gemmini_fence|rr_fence>
          manager=<m> token=<t> seg=<s> gstg=<g> lstg=<l> subbatch=<b>
          hw_state=<...> hw_last_fire=<...> hw_last_done=<...>
```

这条输出必须是 sticky / one-shot / 可清除的，不是高频 trace。它的价值是：即使 live GDB
拿不到 prompt，也能从 host log、AutoCounter、UART 或 MMIO snapshot 快速知道卡在“哪条危险指令附近”。

## 2. 当前必须保留的结论

- `doneflag` 不是 DMA completion 证据。真实 DMA completion 只能以
  `hw_dma_fence()` / blocking wait 等权威路径为准；doneflag 只能作为旁路 telemetry。
- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1` 当前不进入 artifact reader，也不应改变
  artifact 文件读取路径。若前沿停在 artifact read chunk 附近，优先怀疑 guest block/log
  自干扰、image freshness 或读取日志策略，而不是 no-DMA 开关本身。
- 2026-05-09 cfg32/NIC/noTrace F2 上 no-DMA compute 已完整通过。这排除了 no-DMA 形态下
  artifact 读取、Gemmini compute、SPM xlate 和普通 pipebuf 控制流作为首要 blocker。
  回到真实路径时，应优先查真实 DMA submit/wait/fence、direct DMA、producer publish 和 completion。
- `ours` / `gemmini` 性能对比在当前 artifact 中对应 `ours2` / `gemini2`。同一个程序、
  同一个 target key、同一个 batch、同一个 runtime binary 下，只替换
  `pipeline_mapping.<target>.<method>.yaml`。
- `worker-entry` marker 发生在 worker loop 前，不能带精确 subbatch 过滤。要证明 stage worker
  是否创建和进入，`worker-entry` 使用 subbatch `any`；要追精确 subbatch，改用 loop 内 marker。
- 当前 remote `gdbserver` 不可靠处理 TLS 查询。GDB command block 不要打印
  `g_prt_debug_tls_state` 这类 TLS 变量；需要线程状态时，把字段显式放到非 TLS 全局 snapshot。
- `gdbserver --once` 的 guest TCP 端口首连必须来自 GDB，不要用 `nc`、`curl`、`telnet`
  或端口扫描探测。

## 3. 方法一：先静态审计，不先上 F2

静态审计适合回答这些问题：

- mapper artifact 是否违反 manager / SPM / ring / tensor 地址合同。
- 某个 no-DMA 或 perf profile 是否真的只改了预期变量。
- runner、wrapper、rootfs image、runtime binary 是否 fresh。
- 某个现象是否能由已知调试脚本问题解释，例如 worker-entry subbatch filter 或 TLS 打印。

建议每次 F2 前先做：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh debug-preflight
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh image-closure
```

性能实验或自定义 profile 要额外确认：

- `METHODS` 是否只包含目标方法，例如 `ours2 gemini2`。
- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE` 是否符合实验目标。
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE` 是否符合调试或性能路线。
- `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY` 是否实际进入 guest env。
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE`、`PIPELINE_RUNTIME_STDIO_CAPTURE_MODE`
  是否和 known-good profile 一致，除非本轮问题就是验证这些变量。

静态审计无法证明真实 DMA 和 custom instruction 正确，但能显著减少无效 F2 轮次。

## 4. 方法二：phase marker 二分，而不是在自定义指令里 step

对于 RoCC / Gemmini / DMA 指令，GDB 单步通常不是高效路径。更可靠的方法是在自定义指令 wrapper
前后放低频 marker：

```text
before-custom-op
custom-op
after-custom-op
```

解释方式：

- `before` 命中且 `after` 命中：该 custom op 至少在本轮返回，继续向后收窄。
- `before` 命中但 `after` 不命中：卡点在两者之间，优先查该 custom op 对应硬件等待。
- `before` 不命中：前沿仍在更早的软件路径，不要跳到硬件结论。

marker 设计要求：

- marker 写入 `volatile` 非 TLS 全局 snapshot。
- snapshot 至少包含 `segment_idx`、`global_stage_id`、`local_stage_id`、`subbatch_id`、
  `manager_id`、`tensor_id`、`page_id`、`token_id`、`phase_id`、`last_custom_op`。
- GDB 只断在低频 marker 函数，例如 `prt_gdb_marker_stop`。
- 过滤条件优先用全局 marker state，不用优化后的局部变量。
- marker 命中后只打印短字段和短 backtrace，避免长期 `thread apply all bt full`。

当前已知例外：

- `worker-entry` 的 `subbatch_id` 是 `PRT_DEBUG_U32_NONE`，不能用
  `PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0` 过滤。
- 需要精确 subbatch 时，使用 `worker-entry-process-return`、
  `worker-entry-full-return`、`worker-before-exports-ready`、
  `worker-after-build-stage-task` 或 `worker-gemm-run` 等 loop 内 marker。

## 5. 方法三：no-DMA / no-custom-transfer 二分

no-DMA 的目的不是证明数值正确性或性能，而是把问题分成两组：

- 仍执行 scheduler、stage、pipebuf、ring、manager ownership、SPM 地址检查和 Gemmini compute。
- 不执行真实 fixed-load / export DMA 搬运，不把 DMA completion 当作必要条件。

判读：

- no-DMA 跑通：优先查真实 DMA submit/wait/fence、host buffer/direct DMA、completion、
  producer publish 或依赖 DMA 完成的后续控制流。
- no-DMA 失败：优先查 artifact、scheduler、SPM xlate、manager ownership、Gemmini issue/fence、
  ReRoCC scope 或 no-DMA 实现自身。

no-DMA 证据的边界：

- 不能替代真实 DMA correctness 证据。
- 不能作为真实路径性能证据。
- 不能证明 host PA、TileLink flag write、DMA FSM idle、direct DMA 和 cache/coherence 都正确。
- 若开启 GDB 或热日志，它也不能作为性能数据。

对于“完全不执行 custom instruction”的更强二分，应使用 host/CPU dry-run 或新增
`no-custom-transfer`/`hw-validate-only` 路线：只解析 artifact、构建 stage/task、做地址和资源合同检查，
不执行 Gemmini、ReRoCC、DMA 指令。这一层适合本地先排除 mapping 与 runtime 数据结构错误。

## 6. 方法四：显式非 TLS debug snapshot

remote GDB 在当前 RISC-V Linux/gdbserver 环境下可能无法解析 TLS 地址。后续应把 GDB 需要读的
线程状态同步到非 TLS snapshot。

建议 snapshot 字段：

```text
version
thread_role
segment_idx
global_stage_id
local_stage_id
subbatch_id
manager_id
tensor_id
page_id
token_id
phase_id
last_custom_op
last_dma_src
last_dma_dst
last_dma_bytes
last_dma_completion_pa
last_rr_cfg
last_rr_opcode
last_gemmini_funct
last_fence_kind
last_error
```

使用原则：

- snapshot 只用于调试，不作为 completion 权威信号。
- 热路径只写固定大小结构，不写长字符串。
- GDB command block 只读非 TLS 全局或函数参数、结构体指针。
- snapshot 更新点应和 phase marker 对齐，便于把“最后一次软件到达”与“硬件卡点”分开。

## 7. 方法五：窄 gdbserver frontier

GDB 的价值是回答“线程和 PC 在哪里”，不是测性能，也不是长期接管程序调度。

推荐用法：

- 先用 known-good-like profile，少改变量。
- 启动前确认 image freshness、guest env、runtime binary SHA。
- 只放少量阶段边界断点，例如 worker entry、before/after build-stage-task、GEMM run/return、
  DMA wait enter/return、ring C7 wait enter/return。
- 命中后打印函数参数、关键结构体、短 backtrace 和 `$pc` 附近反汇编。
- 一条低层路径已证明返回后，立即 `disable` 相关 helper breakpoint。

避免：

- 热函数长期条件断点。
- broad manual breakpoint 后长时间 `continue`，再靠 `Ctrl-C` 抢回。
- `thread apply all bt full` 作为默认动作。
- 在 command block 中打印 TLS。
- 用 GDB/no-DMA 轮次做性能结论。

若 `Ctrl-C` 失败或 remote disconnect：

- 这本身是有效证据，说明目标可能处在不可中断 custom instruction、fence、MMIO 或 target/gdbserver
  不稳定路径。
- 但它不是精确卡点。下一轮应把 marker 往前后移动，或改成更窄的断点，不应继续扩大等待时间。

## 8. 方法六：小型 micro-repro

full bertmini 的信息量高，但每轮成本也高。对于自定义指令族，应该准备小型可复用 repro：

- Gemmini：单 manager、单 stage、固定 SPM 地址，验证 issue、fence、manager ownership。
- CoupledDMA：单 manager、单 1 KiB 或 4 KiB copy，验证 submit、`hw_dma_fence()`、completion telemetry、
  SPM/host 地址边界。
- ReRoCC：单 acquire、single op、fence、release，验证 cfg busy、scope release 和哨兵读回。
- SPM xlate：固定 vpage/ppn 映射，验证 alias window、PTE flush、fault/status。

优先级：

1. host/CPU dry-run 验证参数和 artifact 合同。
2. FireSim metasim 或小配置验证接口与最小行为。
3. 必要时用现有 AGFI 的小 batch / early-stop workload。
4. 最后才回 full bertmini。

micro-repro 的验收不是“跑得更快”本身，而是能把一个硬件等待问题缩到单一指令族和单一 manager。

## 9. 方法七：性能比较和调试路径分离

`ours2` 与 `gemini2` 的编排性能比较应该保持：

- 同一个 `rerocc_pipeline_runtime-linux`。
- 同一个 `model.layers.yaml`。
- 同一个 `gemmini_layer_mapping.<target>.yaml`。
- 同一个 target key、batch、硬件配置、no-DMA/profile 开关。
- 只替换 `pipeline_mapping.<target>.<method>.yaml`。

性能采样不应开启 live GDB、热路径事件 trace 或高频文本日志。只需要比较模型执行窗口时，优先使用
summary-only trace：

```text
model_compute_ns
model_exec_ns
preprocess_ns
postprocess_ns
dma_submit_count
gemm_issue_count
trace_summary_only
```

如果目标是“不包括预处理”，仍应记录 `preprocess_ns`，但最终比较看 `model_compute_ns`
或明确约定的执行窗口。不要为了减少预处理时间临时改动会影响完成性的 cache/stdout/profile 变量。

## 10. 方法八：硬件可综合观测

硬件观测可以做，但必须把一天级 bitstream 周期当作稀缺资源。原则是：

- 不为单个猜测加一次性 printf。
- 不加高频宽 trace 作为第一选择。
- 优先加小、稳定、跨问题复用的 CSR/MMIO snapshot。
- 每个信号必须回答一个明确问题，并有软件读取方法和退出标准。

### 10.1 什么时候值得改硬件

同时满足以下条件，才建议进入硬件观测设计：

1. 本地静态审计和 host/CPU dry-run 未发现合同错误。
2. known-good-like F2 profile 已确认 image/env fresh。
3. gdbserver 或 marker 已把卡点压到某个硬件等待边界，例如 `hw_dma_fence()`、
   `gemmini_fence()`、ReRoCC fence/release 或 MMIO/CSR read。
4. no-DMA / no-custom-transfer 二分已经说明问题依赖真实硬件路径。
5. 现有软件 snapshot、breadcrumb、GDB 参数和 debugfs 无法区分具体硬件状态。
6. 新硬件信号对后续问题仍可复用，不只是本轮某个 token 的一次性打印。

不满足这些条件时，继续做软件侧收窄，或者构造更小 repro。

### 10.2 优先增加的硬件信号

指令 sentry / debug MMIO：

- `debug_abi_version` 和 `build_id`。
- 软件写入的 `enter_seq`、`exit_seq`、`site_id`、`pc`、`kind`、`manager_id`、
  `token_id`、`segment/global_stage/local_stage/subbatch`。
- `enter_age_cycles` 和 sticky `stuck_latch`。
- `last_enter_without_exit`，用于直接回答“最后一条没返回的危险指令”。
- `last_hw_custom_fire`：硬件实际看到的 opcode/funct/rs1/rs2/manager/cfg。
- `last_hw_custom_done` 或 per-module complete counter。
- clear-on-write 控制位，允许同一 run 多次采样。

DMA manager 每实例：

- `state` 枚举，例如 idle、issue read、wait read resp、issue write、wait write resp、issue flag、
  wait flag、fence response。
- `busy`、`queue_valid`、`queue_count`、`outstanding_reads`、`outstanding_writes`。
- 当前 `src`、`dst`、`remaining_bytes`、`completion_pa` 的低位或 hash。
- `fence_req_count`、`fence_ret_count`。
- `last_completed_count`、`last_error`、`stuck_age_cycles`。
- TileLink `a_valid/a_ready/d_valid/d_ready` stall counters 或 sticky stall reason。

ReRoCC / manager scope：

- 每个 cfg 的 owner、acquired/busy/idle state。
- command queue head/tail、valid/ready/fire counters。
- acquire、fence、release request/ack counters。
- last opcode、last manager id、last cfg id。
- blocked reason：cfg busy、queue full、response backpressure、manager busy、illegal owner。

Gemmini manager：

- RoCC command fire/complete counters。
- fence pending、execute busy、load/store busy、reservation station non-empty。
- last funct/opcode、last manager id、last ROB tag 或局部 tag。
- fence age counter 和 sticky timeout latch。

SPM xlate / alias：

- last translated VA/vpage、PPN、manager id。
- PTW request/response counters。
- xlate fault/status latch。
- PTE flush count、last flushed range。
- blocked reason：PTW wait、fault, invalid PTE, response backpressure。

全局 debug block：

- debug ABI version。
- build/config id。
- per-module progress counters。
- sticky first-stuck latch：模块、manager、state、age、last op metadata。
- clear-on-write 控制位，便于同一 run 中多次采样。

### 10.3 暴露方式

优先顺序：

1. 非 TLS 软件 snapshot。GDB 能读内存时，先用它定位 `enter_seq > exit_seq` 的 site。
2. MMIO/CSR sentry snapshot。GDB 抢不回时，由另一个 watchdog 线程或后续采样读取。
3. sticky latch + 低频 counter，避免错过瞬态。
4. FireSim AutoCounter，用于已有 metasim/FireSim 观测链。
5. one-shot synthesizable printf。只在 `enter` 超过阈值仍无 `exit` 时输出一行，默认可关。

不要把高频逐 beat trace 直接接到常开路径。它会增加资源、扰动时序，也可能让 route/timing 问题掩盖原始问题。

### 10.4 Bitstream 前的硬件改动门槛

提交大 bitstream 构建前，必须写清：

```text
本轮要回答的问题：
  例如：DMA manager 0 卡在 flag write 还是 read/write data path?

已有软件证据：
  gdbserver marker、PC、token、manager、segment/stage/subbatch、no-DMA 结果。

新增信号：
  sentry CSR/MMIO offset、字段宽度、site_id 映射表、字段含义、更新点、clear 语义。

读取方法：
  哪个软件 timeout、watchdog 线程、GDB 停点或 micro-repro 会读这些 CSR。

预期判读：
  `enter_seq/exit_seq/site_id/pc` 如何定位未返回指令；
  各硬件字段组合如何继续分流到 DMA FSM、TL backpressure、SPM xlate、ReRoCC busy 或 Gemmini fence。

退出标准：
  什么结果说明不需要继续 build 另一版硬件。
```

在 full F2 bitstream 前还要做：

- 小配置 elaboration / compile 通过。
- FireSim metasim 通过至少一个 smoke；需要调试仿真器时使用 FireSim metasim 的
  `verilator-debug` 目标，不新增 standalone Verilator 回归。
- CSR 地址和字段版本能被软件读到。
- 资源/timing 变化可接受。
- 默认关闭或低频采样时，不改变正常功能路径。

### 10.5 更值得考虑的硬件 ABI 改动

如果确认 custom instruction 的阻塞语义长期阻碍自动化调试，优先考虑增加非阻塞 status ABI，
而不是只加日志：

- 通用 sentry：新增 always-ready debug status CSR，返回最后未退出的 `site_id/pc/kind/manager/token`
  和 sticky stuck latch。
- DMA：新增 always-ready `CHECK_STATUS`，返回 busy/state/error/outstanding，而不是像
  `FUNCT_CHECK_COMPLETION` 一样等 idle 才 ready。
- Gemmini：新增 per-manager fence status CSR，软件可以 poll 状态，而不是直接进入不可恢复 fence。
- ReRoCC：新增 cfg/scope status CSR，区分 acquire busy、release pending、manager busy 和 response backpressure。

这类 ABI 需要更谨慎，因为会影响软件路径和长期维护。但一旦设计好，价值高于一次性 debug print。

## 11. 推荐执行顺序

每个新卡点按以下顺序处理：

1. 记录当前 run 的 AGFI/AFI、profile、runtime config、workload、env、binary SHA 和 artifact SHA。
2. 检查没有 stale F2、stale `firesim runworkload`、旧 watchdog 或同 tag manager。
3. 本地静态审计 artifact、runner、wrapper、profile 差异。
4. 用 host/CPU dry-run 或 `hw-validate-only` 排除明显合同错误。
5. 用 breadcrumb / marker 把前沿压到 segment/stage/subbatch/manager/token。
6. 若需要 F2，使用 known-good-like 低噪声 profile，先跑窄 gdbserver frontier。
7. 若真实 DMA 路径卡住，跑 no-DMA 二分确认问题是否依赖真实搬运。
8. 若 no-DMA 通过，回真实 DMA 的 submit/wait/fence/status；若 no-DMA 失败，查 compute/SPM/scheduler。
9. 构造 micro-repro，把 full workload 问题缩小到单 manager/单指令族。
10. 只有软件证据仍无法区分硬件内部状态时，设计可综合 debug CSR，并按 bitstream 门槛提交构建。

## 12. 每轮证据模板

建议 debug record 至少包含：

```text
日期 / run 名称：
AGFI / AFI：
F2 instance / private IP：
runtime config / HWDB / workload：
top commit / gemmini commit / rocc-tests commit：
runtime binary SHA：
guest env SHA：
artifact SHA：
profile 关键变量：
GDB 是否启用：
no-DMA 是否启用：
断点/marker：
命中结果：
最后 heartbeat：
uartlog / guest files / trace / GDB log 路径：
结论：
不能支持的结论：
下一步：
```

其中“不能支持的结论”必须明确写。例如：

- broad GDB 轮次不能作为性能数据。
- doneflag 轮次不能作为 DMA completion 证据。
- helper 因 TLS 打印失败停住，不能作为 runtime 卡点。
- no-DMA 通过不能证明真实 DMA 正确。

## 13. 当前建议

短期继续以软件侧收窄为主：

- 回到 known-good-like gdbserver 低噪声 profile。
- 用 corrected marker 和非 TLS GDB helper 追 segment2/stage2 后续边界。
- 对真实 DMA 路径，优先验证 `hw_dma_fence()` / blocking wait 的进入和返回，而不是 doneflag。
- 性能比较使用 summary-only counters，调试和性能分开跑。

硬件可综合输出可以开始设计字段，但不应立即发起一天级 bitstream，除非下一轮证据已经把问题稳定压到
某个硬件内部状态，且现有软件/GDB/no-DMA/micro-repro 都无法回答。
