# Gemmini/ReRoCC/DMA 自定义指令调试策略

更新时间：`2026-05-11 01:33 UTC`

本文记录 `pipeline-runtime` 在 Linux/F2 上调试 Gemmini、ReRoCC、CoupledDMA
自定义指令时的推荐方法。目标不是增加更多热路径日志，而是在少用 F2、少扰动程序轨迹的前提下，
把卡点稳定分到软件调度、DMA completion、ReRoCC scope、Gemmini fence 或硬件不可观测等待。

2026-05-10 更新：程序可能在软件 sentry 或 printf 来得及输出前就已经卡在 custom instruction。
因此后续卡死定位的主证据必须来自 FireSim 硬件侧观测；软件 marker、GDB 和 guest 文件日志只作为
辅助关联证据。硬件观测也不能直接从 12-pair/6-pair 大设计开始，应先按下面顺序验证观测本身。

2026-05-11 更新：后续 PC 级定位优先使用 Rocket core 中的 `SynthesizePrintf` 追踪采样退休 PC、
RoCC 发射 PC、RoCC command-ready 等待 PC 和 RoCC fence/CSR 等待 PC，并保留
ReRoCC/Gemmini/CoupledDMA 已敲定的可综合输出与 target-cycle host/target channel snapshot。
新一轮硬件调试配置不启用 TraceIO/TracerV；如果没有 `TRACEFILE-C0` 属于预期现象，不能据此判定
观测缺失。

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
- Rocket PC `SynthesizePrintf` 打印四类 breadcrumb：
  sampled retired instruction PC、RoCC command fire PC、RoCC command wait PC、RoCC fence/CSR
  wait PC。退休 PC 只打印启动首段和低频采样，不再逐条退休指令常开输出。它只在专用 hwdebug
  config 下打开，不进入普通配置。
- Target-cycle debug widget 采集 FireSim host/target channel 的 valid/ready snapshot，通过
  `+targetcycle-debug*` plusarg 打开并限制 dump 数量。
- `PerfCounter.identity` / AutoCounter 作为可选低频采样手段；本轮 PC 定位配置不把 AutoCounter
  作为必需项。
- TraceIO/TracerV 本轮不启用。只有在 PC breadcrumb、ReRoCC/Gemmini/CoupledDMA printf 和
  target-cycle snapshot 都无法定位时，才重新评估 TraceV 或 AutoILA。
- AutoILA 暂不默认启用。只有上述低成本观测仍不能定位时，才构建 `ILADepth2048_WithAutoILA_...` 版本。

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
- 1C1P Linux/F2 通过时，必须回收 `uartlog`、`heartbeat.csv`、synth print / target-cycle debug 输出；
  若当前配置未启用 TraceV/AutoCounter，不要求 `TRACEFILE-C0` 或 `AUTOCOUNTERFILE*.csv`。
  `uartprobe.status` 应记录正常结束。
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

### 0.2 2026-05-10 1C1P Linux/F2 前置镜像记录

1C1P Linux/F2 小测进入 bitstream 前，先在本地完成 FireMarshal workload 闭环：

- workload：
  `rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug.json`
- build log：
  `software/firemarshal/logs/rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug-build-2026-05-10--15-15-17-QLH67D7DGFCS25GI.log`
- install log：
  `software/firemarshal/logs/rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug-install-2026-05-10--15-16-29-H01LPKDM6FUOB1RB.log`
- install output：
  `sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug.json`

本轮静态问题：

- 该 UART probe 只执行小的 CoupledDMA export-alias 二进制，不依赖 BERT pipeline runtime。
  原 workload 复用通用 `host-init.sh`，默认 `ENABLE_PIPELINE_RUNTIME=auto`；当本地存在
  `conference/HybridMapper/output/pipeline_runtime/bertmini` 时，host-init 会误进入 pipeline artifact
  检查，并要求旧 `rerocc_globalnoc_coupleddma_c2_g2_d2...` mapping，导致 FireMarshal build 在本地立即失败。
- 修复方式：给这个 1C1P hwdebug workload 增加专用 host-init wrapper，默认
  `ENABLE_PIPELINE_RUNTIME=0`，再调用通用 `host-init.sh`。这样小 probe 的镜像构建不再依赖机器上
  恰好存在的 HybridMapper 输出。

当前 F2 状态：

- `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
  仍是占位 `agfi-00000000000000000`。
- 因此还不能执行 1C1P Linux/F2 `runworkload`；下一步必须先生成并填入真实 AGFI，或复用经核对匹配
  build recipe 的既有 AGFI。

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

## 14. 2026-05-10 1C1P 硬件观测验证记录

当前按“小规模先验证工具链，再扩到 6pair2cpu”的顺序执行：

- `1pair1cpu` baremetal + FireSim metasim 已通过。结果目录：
  `sims/firesim/deploy/results-workload/2026-05-10--15-07-20-rerocc-lc-baremetal-cfg32-slot-smoke-quick-local-metasim-rerocc-baremetal-cfg32-slot-smoke-quick-1c1p1-hwdebug/`
- UART 结束标记：`*** PASSED *** after 17410 cycles`。
- 已确认 metasim 输出中能看到 `rrc-client-cmd`、`rrc-client-inst-beat`、
  `rrc-manager-inst-enq`、`rrc-manager-cmd-fire`、`pair-wrapper-dma-cmd`、
  `pair-wrapper-gemmini-cmd`、`coupled-dma-copy-start`、`coupled-dma-copy-done`、
  `rrc-manager-release-resp`。
- 限制：该 smoke 为观测链验证，不是 DMA completion 正确性证明；DMA completion 证据仍必须来自
  `hw_dma_fence()` / blocking wait，不能用 doneflag。

`1pair1cpu` Linux/F2 前置状态：

- FireMarshal workload 已构建并 install 到 FireSim：
  `sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug.json`
- workload 使用 `host-init-1c1p-hwdebug-uartprobe.sh` 默认关闭 pipeline runtime artifact 检查，
  避免本地旧 `HybridMapper/output/pipeline_runtime/bertmini` 误触发旧 mapping 校验。
- bootbinary/rootfs 位于：
  `software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug/`

`1pair1cpu` F2 bitstream 构建状态：

- tmux session：`hwdebug-1c1p-f2-buildbitstream`
- pane log：`tmp/firesim-aws-f2/tmux/hwdebug-1c1p-f2-buildbitstream.pane.log`
- build config：
  `sims/firesim/deploy/config_build_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
- recipe：
  `sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
- 当前远端 build farm：`i-0452c39052811f732`，`z1d.3xlarge`，private IP `192.168.2.241`，
  tag `fsimbuildcluster=pairdummy8x8sbus64c1p1hwdbg`。
- Vivado log：
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C1P1Sbus64NICDebugConfig-WithPrintfSynthesis_WithAutoCounter_WithSynthAsserts_FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_10-152756.vivado.log`
- 截至 2026-05-10 15:43 UTC，远端 Vivado 已进入整体综合并启动 parallel synth worker，尚无 AGFI。

2026-05-10T19:08Z 追加状态：

- 1C1P Vivado 已完成并进入 AWS AFI creation。结果目录：
  `sims/firesim/deploy/results-build/2026-05-10--15-20-21-firesim_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug/`
- AGFI：`agfi-0795d5917b247dfb7`
- AFI：`afi-0b5d10b38e183cd8f`
- AWS AFI 状态仍为 `pending`，`UpdateTime=2026-05-10T18:57:19+00:00`；因此暂不更新
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`。
- 该 build 打印了 post-route DCP timing failure：
  `Detected a post-route DCP with timing failure for AFI creation. Design functionalities are NOT guranteed.`
  这类 AGFI 只能作为小规模 live diagnostic 候选，必须由 Linux/F2 workload 结果确认可用性。
- 当前没有运行中的 F2 runfarm 实例。

2C6P/6pair2cpu 并行构建状态：

- 初始 2C6P build 在本地 Golden Gate 阶段失败，不是远端 Vivado 失败。失败点为
  `firrtl.passes.InlineInstances.fixupRefs` 内 `java.lang.StackOverflowError`，日志：
  `sims/firesim/deploy/logs/2026-05-10--17-57-54-buildbitstream-RCNPARR4Y569QMTM.log`。
- 静态原因是 FAME 生成了很深的左折叠布尔 AND 表达式；2C6P
  `post-fame5-transform.fir` 中 `targetCycleFinishing` 单行约 461 KiB，容易让后续 FIRRTL
  引用修复递归爆栈。单纯提高 `-Xss` 不能可靠解决。
- 当前临时补丁把 `sims/firesim/sim/midas/src/main/scala/midas/passes/fame/RTLUtils.scala`
  的 `BinaryBooleanOp.reduce` 从线性左折叠改成平衡二叉 reduce，使表达式深度从 O(N) 降到 O(log N)。
- 用该补丁重启的 session 为 `hwdebug-2c6p-f2-buildbitstream-balanced-and`；截至
  2026-05-10T19:08Z 已越过原 `InlineInstances` 栈溢出点，生成
  `post-sim-mapping.fir`（约 769 MiB），仍在本地 Golden Gate/Verilog emission 阶段，尚未启动远端
  z1d Vivado build。
- 若 1C1P Linux/F2 小测失败，必须先停止该 2C6P session，再修复并完成 1C1P metasim 回归，然后同步重启
  小/大配置构建。

2026-05-10T20:02Z 追加状态：

- FireSim 文档确认 buildbitstream 的 AWS 路径最后一步是“提交 tar 到 AWS backend 转 AFI，然后本地等待 AFI
  available”；`AGFI_INFO` 在 manager 运行期间描述 AFI 状态，只有 available 后才能把 `agfi:` 条目作为
  runtime hwdb 的有效输入。
- 1C1P `afi-0b5d10b38e183cd8f` 从 `2026-05-10T18:57:19+00:00` 创建以来仍为 `pending`，
  20:01Z 查询仍无失败 `Message` 字段；继续等待，不更新 hwdb。
- 2C6P 已生成
  `sims/firesim/sim/generated-src/f2/f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C2P6Sbus64NICDebugConfig-WithPrintfSynthesis_WithAutoCounter_WithSynthAsserts_FRFCFS16GBQuadRank_BaseF2Config/FireSim-generated.sv`
  （约 212 MiB），并启动远端 build host `i-01593d53b67b4edb5` / `192.168.0.182`，build tag
  `pairdummy8x8sbus64c2p6hwdbg`。
- 当前运行中的 build hosts：1C1P `i-0452c39052811f732`（等待 AFI available）和 2C6P
  `i-01593d53b67b4edb5`（Vivado 早期 IP synthesis）。当前仍没有 F2 runfarm 实例。
- 不把 2C6P 远端 build 视作 1C1P Linux/F2 测试通过的替代证据；若 1C1P F2 workload 失败，优先停止
  2C6P build，回到本地/metasim 修复闭环。

下一步：等待 1C1P F2 bitstream/AFI creation 完成。成功后更新
`config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml` 的 AGFI，并用同一
runtime 三件套执行 `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`。只有 1C1P Linux/F2
小测确认可用后，再构建 6pair2cpu。

2026-05-10T20:35Z 追加状态：

- 1C1P AFI 已 ready，并已把
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`
  更新为 `agfi-0795d5917b247dfb7`。
- 1C1P Linux/F2 workload 已执行但失败。结果目录：
  `sims/firesim/deploy/results-workload/2026-05-10--20-30-11-rerocc-lc-linux-coupleddma-dma-export-alias-uartprobe-1c1p1-hwdebug-f2-rerocc-linux-uartprobe-1c1p1-hwdebug/`
- `uartlog` 中的关键失败为：
  `Simulator deadlock detected at target cycle 0`，随后
  `*** FAILED *** (code = 1) after 0 cycles`。
- 这轮没有 guest `pipeline-runtime-debug/*` 产物，也没有 Linux boot 证据。因此它不是
  pipeline runtime 程序卡死，卡点在 FireSim/FPGA driver 进入 target 前后的 cycle-0
  forward-progress 判断。
- FireSim hanging-simulator 文档把这种“driver 主循环仍在跑但 target cycle 不前进”的现象归到
  FPGA-side token starvation / bridge-driver 交互类问题；在本轮证据中，另一个强嫌疑是该 AGFI
  的 post-route timing 本身 violated。
- build 证据：
  `sims/firesim/deploy/results-build/2026-05-10--15-20-21-firesim_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug/`
  的 post-route timing report 最坏 setup slack 约 `-3.025ns`，最坏路径集中在
  `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2` 的 PCIS AR 通道；build log 生成
  `.post_route.VIOLATED.dcp`，并提示 timing failure 下功能不保证。
- 按“小测失败先停止大构建”的策略，已停止 2C6P session
  `hwdebug-2c6p-f2-buildbitstream-balanced-and-restart1`，并终止对应 build host
  `i-068436632dfd00c27`。1C1P run host 也已回收；当前没有运行中的 `f2.*` 或 `z1d.*` 实例。
- 限制：这轮不能作为 DMA completion 或 pipeline runtime 失败证据；它只能证明当前 1C1P
  F2 AGFI/runtime 组合无法推进 target cycle。

下一步：

1. 先做低成本 runtime 收敛：把 1C1P F2 runtime 从 cycle-0 trace trigger
   `selector: 1/start: 0` 改回和通过的 1C1P metasim 一致的 instruction trigger
   `selector: 3/start: ffffffff00008013/end: ffffffff00010013`，并把 heartbeat 放宽到
   `100000000`，减少 TracerV cycle-0 全量拉取和过早 heartbeat 的干扰。
2. runtime 修正先重新跑 1C1P metasim，确认配置仍能通过。
3. 若下一次 1C1P F2 仍 cycle-0 失败，则不再把主因押在 runtime；优先重建 1C1P AGFI：
   使用 timing 更保守的 recipe（例如 `TIMING_HOLDFIX`）或减少非必要 instrumentation，同时保持
   硬件可观测链的最小字段。1C1P metasim 通过后，再同步重启 1C1P/2C6P 构建。

2026-05-11T01:15Z 追加状态：

- 已确认新路线为：不启用 TraceIO/TracerV；保留 ReRoCC/Gemmini/CoupledDMA 已有
  `SynthesizePrintf`，新增 Rocket core PC breadcrumb，并启用 target-cycle debug widget。
- 相关配置：
  `FireSimGemminiReRoCCPairDummy8x8C1P1Sbus64NICDebugConfig` 和
  `FireSimGemminiReRoCCPairDummy8x8C2P6Sbus64NICDebugConfig` 均使用
  `WithNoTraceIO ++ WithRocketSynthPCDebug`；1C1P metasim build recipe 使用
  `WithTargetCycleDebug_WithPrintfSynthesis_WithSynthAsserts_FRFCFS16GBQuadRank_BaseF2Config`。
- 当前 1C1P metasim `infrasetup` session：
  `hwdebug-1c1p-pc-targetcycle-metasim-infrasetup-rerun`。
  本轮先清理了旧 staging/generated/output 和 `.classpath_cache/firechip.jar`，避免复用旧
  `VFireSim` 或旧 `sfc.fir`。
- 静态和 Golden Gate 证据：
  新生成的 `sfc.fir` 已包含 `[rocket-retire-pc]`、`[rocket-rocc-pc-fire]`、
  `[rocket-rocc-pc-wait]`；Golden Gate `PrintBridgeParameters` 中同时列出这些 Rocket PC
  printf 以及原有 `rrc-client-*`、`rrc-manager-*`、`pair-wrapper-*`、
  `coupled-dma-*` 输出；Simulator Memory Map 中已有 `TargetCycleDebugWidget_0`。
- 经验记录：只执行 `runworkload` 可能继续使用旧 `/home/ubuntu/sim_slot_0/VFireSim`。
  修改 target RTL、MIDAS widget、host bridge 或 build recipe 后，必须先跑 `infrasetup`
  重新生成和部署 driver，再跑 workload。
- 当前没有运行中的 `f2.*` 或 `z1d.*` 实例；本轮仍停留在本地 metasim 验证。

限制：

- 上述证据只证明观测逻辑进入 target FIR 和 Golden Gate driver 生成链路；最终可用性还要等
  1C1P metasim `runworkload` 看到 `TARGETCYCLE DEBUG`、Rocket PC printf、既有硬件 printf
  与 `*** PASSED ***`。
- Target-cycle widget 当前把部分 input channel missing-valid 也计入 problem，可能在空闲期较早触发。
  runtime 通过 `+targetcycle-debug-limit=4` 限制输出规模；若后续输出太早或过噪，再收窄触发条件。

2026-05-11T01:33Z 追加状态：

- 已把 Rocket PC 输出从逐条 retired instruction 改为首 `16` 条加每 `2^20` retired instruction
  采样，同时保留 exception 打印。这样 Linux/F2 长跑仍能看到软件自旋附近 PC，但不会把每条退休指令
  都送进 synthesized printf。
- 额外加入 `rocket-rocc-fence-wait-pc`。它在 decode 阶段遇到 `fence` 或 RoCC CSR write 且
  `id_rocc_busy` 时低频打印 PC、指令、`rocc_busy` 以及 ex/mem/wb 是否仍有 RoCC 指令，覆盖
  “命令已经 fire，但后续 fence 等 busy 清零”的卡点。
- 1C1P NIC hwdebug metasim `infrasetup` 已通过：
  session `hwdebug-1c1p-pc-sampled-metasim-infrasetup`，exit code `0`；
  Golden Gate 报告 `35` 个 synthesized printf，`PrintBridgeParameters` 中列出
  `rocket-retire-pc-sample`、`rocket-rocc-pc-fire`、`rocket-rocc-pc-wait`、
  `rocket-rocc-fence-wait-pc`，同时仍列出既有 `rrc-*`、`pair-wrapper-*`、`coupled-dma-*`。
  新部署的 `/home/ubuntu/sim_slot_0/VFireSim` 时间戳为 `2026-05-11 01:30 UTC`。
- 1C1P NIC hwdebug metasim `runworkload` 已通过：
  session `hwdebug-1c1p-pc-sampled-metasim-runworkload`，exit code `0`；
  结果目录为
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-05-11--01-32-34-rerocc-lc-baremetal-cfg32-slot-smoke-quick-local-metasim-rerocc-baremetal-cfg32-slot-smoke-quick-1c1p1-nic-hwdebug/`。
  `metasim_stderr.out` 结尾为 `Simulation complete.` / `*** PASSED *** after 17410 cycles`。
- 运行期计数：`rocket-retire-pc-sample=16`、旧 `rocket-retire-pc=0`、`rocket-rocc-pc-fire=7`、
  `rocket-rocc-pc-wait=0`、`rocket-rocc-fence-wait-pc=1`、`rrc-client-cmd=7`、
  `rrc-manager-cmd-fire=7`、`pair-wrapper-gemmini-cmd=5`、`pair-wrapper-dma-cmd=2`、
  `coupled-dma-copy-start=1`。这证明 PC breadcrumb 已经可用，且没有重新引入逐条退休 PC 噪声。
- 仍需注意：`targetcycle-debug-limit=4` 虽限制 dump 次数，但 `+targetcycle-debug-labels=1`
  会在每次 dump 打印最多 256 个 wire output label；本轮 1C1P `uartlog` 约 `7713` 行。
  F2/Linux 复现若日志压力过大，优先把 runtime plusarg 改为 `+targetcycle-debug-labels=0`，必要时
  再针对问题 channel 重新开 label，而不是扩大 synthesized printf。

2026-05-11T01:41Z 追加状态：

- 用户确认后，硬件观测路线固定为：不用 TraceV/TraceIO；使用 Rocket core
  `SynthesizePrintf` 采样退休 PC，并保留 `rocket-rocc-pc-fire`、
  `rocket-rocc-pc-wait`、`rocket-rocc-fence-wait-pc`、既有 ReRoCC/Gemmini/CoupledDMA
  synthesized printf，以及 target-cycle debug snapshot。
- 已启动 1C1P F2 小配置构建：
  session `hwdebug-1c1p-f2-pc-sampled-buildbitstream`，
  build config `config_build_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`，
  recipe `config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug.yaml`。
- 曾尝试同步启动 2C6P F2 构建：
  session `hwdebug-2c6p-f2-pc-sampled-buildbitstream`，但在本地 elaboration 阶段主动停止，
  exit code `120`。原因是两个 `buildbitstream` 在同一个 workspace 内会共享
  `sims/firesim-staging`、`sim/midas/src/main/scala/target-symlinks`、
  `.classpath_cache/firechip.jar` 和部分 `sim/generated-src`/`sim/output` 生成路径；不同硬件
  配置并行跑 `replace-rtl`/Golden Gate 有污染小配置结果的风险。
- 后续同步构建规则：不要在同一 workspace 并行启动两个不同 target config 的
  `buildbitstream` 本地生成阶段。若需要同步推进大配置，等 1C1P 完成本地
  `replace-rtl`/driver 打包并进入远端 build farm 后再启动，或使用隔离 worktree/独立 checkout。

2026-05-11T01:50Z 追加状态：

- 1C1P F2 小配置构建的本地 `replace-rtl`、Golden Gate 和 F2 driver 编译通过；
  `PrintBridgeParameters` 中确认包含 `rocket-retire-pc-sample`、`rocket-rocc-pc-fire`、
  `rocket-rocc-pc-wait`、`rocket-rocc-fence-wait-pc` 以及既有 `rrc-*`、
  `pair-wrapper-*`、`coupled-dma-*`，Simulator Memory Map 中确认有
  `TargetCycleDebugWidget_0`。
- 远端 build farm 启动前失败：FireSim 默认查找
  `FPGA Developer AMI (Ubuntu) - 1.17.0   -prod-rhng4b6alkhdq`，并自动尝试
  `1.17.1` 到 `1.17.9`，但当前 AWS `us-west-2` 均不可见，`get_f2_ami_id()`
  触发 `AssertionError`。本轮未启动任何 z1d/f2 实例，失败日志为
  `sims/firesim/deploy/logs/2026-05-11--01-39-34-buildbitstream-Y9UZWAYZDKPX8F67.log`。
- 本地 F2 SDK 文档列出的可用 F2 Developer AMI 已更新到 `1.18.0`/`1.16.1`；
  实时 AWS 查询当前可见 Ubuntu AMI 包括：
  `ami-082c5db2375456e1a`（`FPGA Developer AMI (Ubuntu) - 1.19.1-prod-rhng4b6alkhdq`）
  和 `ami-0c4a5ae51e92d81b3`（`FPGA Developer AMI (Ubuntu) - 1.16.1 -prod-byisb4uqt2pwc`）。
- 修复策略：给 FireSim `AWSEC2` build farm 增加可选 `ami_id` 参数并传给
  `launch_instances`；只在 1C1P/2C6P hwdebug build yaml 中显式指定
  `ami-082c5db2375456e1a`。这样避免全局修改默认 AMI 查找逻辑，也避免影响其它历史配置。

2026-05-11T01:59Z 追加状态：

- 上述 `ami-082c5db2375456e1a` 选择已验证为不可用：它是
  `FPGA Developer AMI (Ubuntu) - 1.19.1-prod-rhng4b6alkhdq`，远端
  `hdk_setup.sh` 检测到 `Vivado v2025.2` 后直接退出。当前 F2 HDK 只接受
  `Vivado v2024.1`、`v2024.2`、`v2025.1`；失败日志为
  `sims/firesim/deploy/logs/2026-05-11--01-51-14-buildbitstream-GIT9CQRJ1N2FNVED.log`。
- FireSim 已自动终止本轮 z1d build host `i-04d7333abf83e313f`，AWS 状态确认为
  `terminated`，未留下运行中的 build 实例。
- 后续 hwdebug F2 build 改用 `ami-0c4a5ae51e92d81b3`
  (`FPGA Developer AMI (Ubuntu) - 1.16.1 -prod-byisb4uqt2pwc`)，其描述为
  `Vivado 2024.1 tools`，与当前 F2 HDK 支持列表匹配。若 2024.1 后续遇到
  IP/脚本兼容问题，再优先寻找 us-west-2 可见的 1.18.0/2025.1 AMI 或切到隔离的
  supported developer AMI，而不是放宽 `hdk_setup.sh` 对 2025.2 的版本检查。

2026-05-11T02:05Z 追加状态：

- 第二次 1C1P `buildbitstream` 尝试改用 `ami-0c4a5ae51e92d81b3` 后没有启动实例：
  AWS `RunInstances` 返回 `OptInRequired`，要求先订阅 Marketplace SKU
  `6s157wr19zh05fzcemdz6t1kl`。失败日志为
  `sims/firesim/deploy/logs/2026-05-11--01-58-39-buildbitstream-DIO8JFJ51I76STSY.log`。
- 更合适的 replacement 是当前 FireSim manager 自身使用的
  `ami-0d7cdfb6b3ce5b5e0`，AWS 描述为
  `FPGA Developer AMI (Ubuntu) - 1.17.0   -prod-rhng4b6alkhdq`，带
  `Vivado 2024.2 tools`，产品码仍是当前账户已能使用的 `e4txuxx6uz6371b7tgmotozac`。
  本机 `/opt/Xilinx/Vivado/2024.2/bin/vivado -version` 也确认 manager 环境为
  `vivado v2024.2`。
- 因此 1C1P/2C6P hwdebug build yaml 改为显式 pin `ami-0d7cdfb6b3ce5b5e0`。
  这既避开 1.19.1 的 `Vivado 2025.2` 不兼容，也避开 1.16.1 的 Marketplace
  订阅阻塞。

2026-05-11T02:12Z 追加状态：

- 第三次 1C1P `buildbitstream` 使用 `ami-0d7cdfb6b3ce5b5e0` 成功启动
  z1d.3xlarge build host `i-0f8e0a817ba503f21`，private IP `192.168.1.226`。
  manager 日志为
  `sims/firesim/deploy/logs/2026-05-11--02-01-51-buildbitstream-4WZZDD2PNOS9HWRK.log`。
- 远端 `hdk_setup.sh` 已通过：日志显示 `Using vivado v2024.2`、
  `VIVADO_TOOL_VERSION is 2024.2`、`AWS HDK setup PASSED`。随后已进入
  `aws_build_dcp_from_cl.py` 和 Vivado batch `build_all.tcl`。
- 远端 Vivado 日志路径：
  `/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimGemminiReRoCCPairDummy8x8C1P1Sbus64NICDebugConfig-WithTargetCycleDebug_WithPrintfSynthesis_WithSynthAsserts_FRFCFS16GBQuadRank_BaseF2Config/build/scripts/2026_05_11-020329.vivado.log`。
  host 资源约为 12 logical cores、100 GiB memory，满足 F2 构建需求。

2026-05-11T02:18Z 追加状态：

- 远端 Vivado 已通过 early synthesis/optimization 阶段：
  `Synthesis finished with 0 errors, 0 critical warnings and 1 warnings`。
  这说明本轮 PC synthesized printf、既有 ReRoCC/Gemmini/CoupledDMA synthesized printf、
  `TargetCycleDebugWidget_0` 以及 F2/2024.2 工具链组合没有在前端综合阶段失败。
- 当前仍在同一 Vivado batch 中继续执行后续 netlist optimization / implementation / routing。
  build host `i-0f8e0a817ba503f21` 仍为 `running`，此 checkpoint 不是 AGFI/AFI
  完成证据。

2026-05-11T02:19Z 追加经验：

- 历史上一次完成的 1C1P hwdebug F2 build 为
  `sims/firesim/deploy/logs/2026-05-10--15-20-21-buildbitstream-76BCL5ON1VOYVMB6.log`。
  从 `2026-05-10 15:20:21` 启动，到 `2026-05-10 20:06:34` AGFI 变为
  `available`，约 `4h46m13s`；到 manager 打印 `Build complete!` 为
  `2026-05-10 20:06:54`，约 `4h46m33s`。
- 该历史构建生成 `agfi-0795d5917b247dfb7` / `afi-0b5d10b38e183cd8f`，
  deploy triplet 是 `WithPrintfSynthesis_WithAutoCounter_WithSynthAsserts`，
  不等同于当前 `WithTargetCycleDebug_WithPrintfSynthesis_WithSynthAsserts` 配置。
- 后续如只验证 PC/synthesized printf 硬件观测链路，优先复用历史上更小、更快的
  1C1P 小配置；当前这轮 1C1P/2C6P 构建不为切换小配置而中断。

2026-05-11T07:33Z 追加状态：

- 当前 1C1P `WithTargetCycleDebug_WithPrintfSynthesis_WithSynthAsserts` F2 build
  没有生成 AGFI。tmux session `hwdebug-1c1p-f2-pc-sampled-buildbitstream-ami117`
  exit code 为 `1`。
- 时间线：manager 于 `2026-05-11T02:01:51Z` 启动，FireSim tmux 于
  `2026-05-11T07:23:13Z` 结束，耗时约 `5h21m22s`。build host 为
  `i-0f8e0a817ba503f21` / `192.168.1.226`，FireSim 已请求 terminate；当前 AWS
  运行中只剩 2C6P build host。
- 失败点是 Vivado `route_design`，不是普通 shell timing violation：
  `ERROR: [Constraints 18-4430] ... boundary net WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE/... does not contain PartPin LOC`，
  随后 `INFO: [Route 35-17] Router encountered errors`、`route_design failed`、
  `ERROR: Did not find the post-route DCP file`。
- 这与既有
  `route35_514_static_assessment_20260506_temp.md` 和
  `cfg32_nic_f2_fix_options_20260507_temp.md` 中记录的 F2 small-shell DFX
  boundary 问题一致，重点仍是 `RL_SHIM/DMA_PCIS_AXI_REG_SLC` 这类 PCIS 边界 net
  的 PartPin 合法性。单纯“规模小”不能保证通过；历史 8p 低资源构建也曾在同类
  `Constraints 18-4430` 上失败。
- 同步启动的 2C6P 构建仍在运行：
  session `hwdebug-2c6p-f2-pc-sampled-buildbitstream-ami117`，build host
  `i-018460888f9704c7e` / `192.168.0.241`。截至 `2026-05-11T07:32Z`，
  2C6P 处于 Vivado placement / post-placement optimization 阶段，尚未到
  route/DFX failure 或 AGFI 创建。

2026-05-11T07:44Z 追加结论：

- 本轮 1C1P route failure 的直接原因确认是 F2 small-shell DFX PartPin 合法性错误，
  不是普通 AWS shell timing violation，也不是 PC synthesized printf 或
  `TargetCycleDebugWidget` 的前端综合错误。错误集中在
  `WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE` 的 PCIS 边界网。
- AWS F2 文档要求 PCIS 相关 CL-side first flop/register slice 尽量放在 PCIS 所在
  SLR1；跨 SLR 时两侧应有 flop/register slice。本轮报错说明 router 最终选择的
  static/CL boundary branch 没有合法 PartPin LOC。
- 与历史成功 1C1P 构建相比，本轮最明显、最可控的差异是 build recipe 使用了
  `TIMING_HOLDFIX`。该策略只比普通 `TIMING` 多
  `set_param route.enableHoldExpnBailout 0`，日志中 Vivado 也反复提示可以打开
  hold-expansion bailout 来减少 runtime。该参数会让 router 在大量 hold violator
  下继续展开 hold 修复，可能把 PCIS/RL_SHIM 边界 net 推入 DFX 非法 routing branch。
- 已将 1C1P/2C6P hwdebug F2 build recipes 的 `build_strategy` 从
  `TIMING_HOLDFIX` 改回 `TIMING`，作为下一轮最小验证修复。若后续仍报
  `Constraints 18-4430`，再进入 PCIS floorplan/first-register-slice 修复，不建议
  继续盲目重复 `TIMING_HOLDFIX` 构建或直接手写 PartPin LOC。

2026-05-11T11:55Z 追加状态：

- `TIMING` 重建仍在 1C1P route 阶段报 `Constraints 18-4430` 后，已转入
  PCIS floorplan 修复。根因判断不是 AWS shell timing violation，而是
  F2 small-shell DFX PartPin 合法性错误；可见边界 net 从
  `WRAPPER/RL_SHIM/DMA_PCIS_AXI_REG_SLC/AXI_REGISTER_SLICE` 指向
  `WRAPPER/CL/CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2`。
- 本轮采用 XDC-only 修复，不改 RTL/软件、不手写 PartPin：将
  `CL_DMA_PCIS_SLV/AXI4_REG_SLC_PCIS_SLR2` 从 `pblock_CL_SLR2` 移到
  `pblock_CL_SLR1`，并把 `wide_pcis_clock_convert`、`pcis_width_bridge`
  同放入 SLR1。依据是 AWS F2 文档中 small-shell PCIS 物理位于 SLR1，
  shell interface 的 first flop/register slice 应尽量放在同一 SLR。
- 修复 checkpoint：
  - `aws-fpga-firesim-f2` commit `26a1172b516f0ea70501d23f38dd3a2b23a0af9f`
    (`Place small-shell PCIS handoff in SLR1`)
  - `sims/firesim` commit `91888035e601638b356f98aa70793b4005cbf653`
    (`Add m8i build config for PCIS floorplan test`)
  - top-level commit `650e5f55` (`Checkpoint PCIS floorplan build setup`)
- 已按要求用更小的 m 系实例启动 1C1P 验证构建：
  `m8i.2xlarge` build host `i-0f5d56a0fa8ccdac2` / `192.168.2.155`，
  FireSim log
  `sims/firesim/deploy/logs/2026-05-11--11-53-05-buildbitstream-SQ1Q3F3QADK0GM8I.log`，
  tmux session `hwdebug-1c1p-f2-pcis-slr1-floorplan-m8i-buildbitstream`。
  实际远端命令为 `build-bitstream.sh --frequency 20 --strategy TIMING`；
  日志已确认 `AWS HDK setup PASSED`、`Using BUILD_STRATEGY=TIMING`。
- 同时保留正在运行的 2C6P 构建，不打断：build host
  `i-018460888f9704c7e` / `192.168.0.241`，build tag
  `pairdummy8x8sbus64c2p6hwdbg`。

2026-05-11T12:08Z 追加状态：

- 已主动终止旧 2C6P 构建，避免继续消耗 `z1d.3xlarge`。该构建是
  `hwdebug-2c6p-f2-pc-sampled-buildbitstream-ami117`，build host
  `i-018460888f9704c7e`，build tag `pairdummy8x8sbus64c2p6hwdbg`。
- 终止前状态：仍在 Vivado `route_design` Phase 5 rip-up/reroute，日志反复出现
  `Route 35-469` large hold violators，最后可见 overlap 数回升到 `78481`；
  未看到 `Constraints 18-4430`，也未看到 `route_design completed`、post-route
  DCP、bitstream 或 AGFI。
- 终止理由：该 2C6P 是 PCIS SLR1 XDC 修复前启动的旧构建，而且远端命令仍使用
  `--strategy TIMING_HOLDFIX`。在 1C1P floorplan 修复尚未通过前，它不能作为
  新修复的有效验证；若 1C1P 仍不通过，2C6P 大概率也不会给出更有价值的结果。
- 操作结果：`i-018460888f9704c7e` 已进入并确认到 `terminated`，对应 tmux session
  已清理。保留 1C1P m8i 构建继续运行：
  `hwdebug-1c1p-f2-pcis-slr1-floorplan-m8i-buildbitstream` /
  `i-0f5d56a0fa8ccdac2`。

2026-05-11T12:12Z 追加状态：

- 基于当前 PCIS SLR1 XDC 修复重新启动 2C6P/6-pair F2 构建：
  `hwdebug-2c6p-f2-pcis-slr1-floorplan-buildbitstream`。
- 使用配置：
  `config_build_f2_gemmini_rerocc_pairmanager_dummy8x8_2c6p6_sbus64_nic_hwdebug.yaml`
  和
  `config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_2c6p6_sbus64_nic_hwdebug.yaml`。
  recipe 已确认是 `build_strategy: TIMING`，远端实际命令也打印
  `build-bitstream.sh ... --frequency 20 --strategy TIMING`，不是旧的
  `TIMING_HOLDFIX`。
- 新 build host：`i-04458088bcf314a5f` / `z1d.3xlarge` / `192.168.1.212`，
  build tag `pairdummy8x8sbus64c2p6hwdbg`。manager log：
  `sims/firesim/deploy/logs/2026-05-11--12-11-05-buildbitstream-Q6IKPNZOGM6P7GL9.log`。
- 当前并发构建只有两条：1C1P m8i floorplan validation
  `i-0f5d56a0fa8ccdac2` 和这条新的 2C6P z1d。旧 2C6P host
  `i-018460888f9704c7e` 已 terminated。

2026-05-11T16:20Z 追加状态：

- 1C1P PCIS SLR1 floorplan 构建已越过 Vivado route/DFX PartPin 卡点并提交
  AWS AFI creation。结果：
  `agfi-098bce7d5e0c3d937` / `afi-0d63b7450829af6c6`。
  截至 `2026-05-11T16:20Z`，AWS 状态仍为 `pending`，`State.Message=None`；
  这不是本地 route 失败，也不是 workload 执行卡死。
- 已把 1C1P F2 hwdb 和 built-hwdb entry 更新到
  `agfi-098bce7d5e0c3d937`，等待 AFI `available` 后执行 baremetal 和 Linux
  runworkload 测试。runtime/workload YAML 已用 PyYAML 校验可解析。
- FireSim `F2BitBuilder.aws_create_afi()` 的流程是：manager 本地上传 tar 到 S3、
  调 `aws ec2 create-fpga-image`、然后本地循环 `describe-fpga-images` 等待
  `available`，最后再 `release_build_host()`。因此一旦 AFI/AGFI ID 已生成，
  build host 不再参与后续 AFI 状态推进。
- 为减少空转成本，已手动终止 1C1P m8i build host
  `i-0f5d56a0fa8ccdac2`。保留 manager tmux
  `hwdebug-1c1p-f2-pcis-slr1-floorplan-m8i-buildbitstream` 继续等待 AWS AFI
  状态。若 manager 后续重复 terminate 该 instance，应视为可接受的清理重试。
- 新 2C6P 构建 `hwdebug-2c6p-f2-pcis-slr1-floorplan-buildbitstream` 仍运行在
  `i-04458088bcf314a5f` / `192.168.1.212`，截至本记录处于 post-place
  `phys_opt_design`，尚未看到 `Constraints 18-4430`。

2026-05-11T16:50Z 追加状态：

- 1C1P F2 hwdebug bitstream 已创建完成并进入 baremetal smoke 首轮测试：
  `agfi-098bce7d5e0c3d937` / `afi-0d63b7450829af6c6`，runfarm
  `i-078214ea606e6044d` / `192.168.1.137`，runtime
  `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug_baremetal_cfg32_slot_smoke_quick.yaml`，
  workload result
  `sims/firesim/deploy/results-workload/2026-05-11--16-35-23-rerocc-lc-baremetal-cfg32-slot-smoke-quick-f2-rerocc-baremetal-cfg32-slot-smoke-quick-1c1p1-nic-hwdebug/`。
- 本轮不能用来下“硬件 AGFI 不可用”结论。关键原因是调试动作重叠：
  full-debug `runworkload` 在 16:35:32 启动 slot 0 仿真后仍在运行；
  随后 nodebug A/B 的 `infrasetup` 在 16:43:16 对同一台 F2、同一 slot 执行
  `sudo fpga-clear-local-image -S 0 -A`。旧 `runworkload` manager 之后在 16:43:45
  看到 slot 0 screen/仿真消失，按 `terminate_on_completion: yes` copy-back 并终止实例。
  因此 nodebug A/B 被旧 manager 干扰，full-debug 的最终退出也被新 `infrasetup`
  干扰；两者都不能作为软件或硬件正确性的有效判据。
- full-debug 首轮仍提供一个有用现场：仿真启动后 `uartlog` 在约 16:35:33 达到
  `940716` 字节，此后到 16:38 live copy 不再增长；`heartbeat.csv` 只有表头，
  `memory_stats0.csv` 也只有表头；没有 baremetal `PASS`/`FAIL` 或 guest 日志。
  `uartlog` 内容几乎全部是 `TARGETCYCLE DEBUG` 的 channel/blocker dump，其中
  `wire_out=256`，大量 endpoint 是 `synthesizedPrintf_*`。这更像
  target-cycle/printf 观测链在启动早期制造了大量诊断输出或握手压力，但尚不能排除
  AGFI、runtime plusargs 或 FireSim bridge 交互问题。
- 经验约束：同一 runfarm/slot 上，必须确认旧 `runworkload` manager、remote
  `FireSim-f2`、`screen -S fsim0` 都已经退出，或显式保留现场不再对该 slot 运行
  `infrasetup`，才能启动新的 A/B。`infrasetup` 会 clear FPGA slot，不能和正在运行的
  workload 并发。
- 下一轮低成本验证应复用同一 AGFI，但重新 launch 一个干净 runfarm，按顺序执行：
  `launchrunfarm -> infrasetup -> runworkload -> inspect -> terminaterunfarm`。
  先跑 nodebug runtime（`+targetcycle-debug=0`，`synth_print.start/end` 延后），
  再跑中间 runtime（`+targetcycle-debug=1` 但 `+targetcycle-debug-labels=0`，
  `synth_print` 仍延后），最后才恢复 full-debug。这样区分 AGFI 基础可运行性、
  TargetCycleDebugWidget 读寄存器/输出压力、以及 synthesized printf token 压力。

2026-05-11T17:05Z 追加状态：

- 已按上述顺序复用同一 AGFI 启动干净 1C1P F2 nodebug baremetal 测试，未与其它
  `infrasetup`/`runworkload` 重叠：
  - AGFI/AFI：`agfi-098bce7d5e0c3d937` / `afi-0d63b7450829af6c6`
  - runfarm：`i-08d7a61110e529d28` / `192.168.1.160`
  - runtime：
    `config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_1c1p1_sbus64_nic_hwdebug_baremetal_cfg32_slot_smoke_quick_nodebug.yaml`
  - run log：
    `sims/firesim/deploy/logs/2026-05-11--16-59-49-runworkload-MZA3XZ563KFTRUAH.log`
  - result：
    `sims/firesim/deploy/results-workload/2026-05-11--16-59-49-rerocc-lc-baremetal-cfg32-slot-smoke-quick-f2-rerocc-baremetal-cfg32-slot-smoke-quick-1c1p1-nic-hwdebug-nodebug/`
- 有效结果：`uartlog` 显示 `Simulation complete.` 和
  `*** PASSED *** after 44104832 cycles`，driver `COMMAND_EXIT_CODE="0"`；
  wallclock 2.2s，effective target frequency 约 19.982MHz。`TRACEFILE*`、
  `AUTOCOUNTERFILE*`、`metasim_stderr.out` copy-back 缺失只是对应功能未启用的
  rsync warning，不影响 PASS 结论。
- 因此当前不能把首轮 full-debug 的“没有 guest 输出/没有 PASS”归因到 payload
  进不去程序、AGFI 基础不可用或 AWS shell timing violation。至少在
  `+targetcycle-debug=0` 且 synth print 延后时，同一 AGFI 和同一 baremetal payload
  可在 F2 上跑通。
- 额外发现：TargetCycleDebugWidget 的 F2 OCL 读数不可信，但本地 metasim 正常。
  首轮 full-debug F2 `uartlog` 中 `TARGETCYCLE DEBUG [tick]` 出现
  `hcycle=0x37daa56537daa565` 这类高低 32 位近似相同的异常值，且
  `counts hport=9 wire_in=6 wire_out=6 rv_in=6 rv_out=0` 与构造时打印的
  `labels hport=0 wire_in=9 wire_out=256 rv_in=4 rv_out=6` 不一致。同一
  targetcycle debug 在 local metasim 结果
  `2026-05-11--01-32-34-...-1c1p1-nic-hwdebug` 中读数正常：
  `hcycle=7560`、`counts hport=0 wire_in=9 wire_out=256 rv_in=4 rv_out=6`，
  且 `COMMAND_EXIT_CODE="0"`。
- 当前解释优先级：
  1. 首轮 full-debug 无效退出的直接原因仍是调试流程重叠，后续 `infrasetup`
     clear 了同一 slot。
  2. full-debug 期间没有进入 guest PASS 的可见原因，是 targetcycle/printf 观测链
     在启动早期打印大规模 channel/blocker dump；该 dump 本身可能持续数分钟，
     并且 F2 上读数不可信，不能用其中 blocker 统计当成真实 target 卡点。
  3. 需要进一步隔离的是 F2 上 TargetCycleDebugWidget 的 OCL 读路径/实现时序和
     synthesized printf 输出压力；不是先重建 bitstream，也不是先判定 payload 错。
- 下一步建议仍是少量 F2 A/B：先本地审计 TargetCycleDebugWidget 的 CR 绑定和
  F2 OCL 连续读行为；若无确定静态修复，再跑中间 runtime
  `+targetcycle-debug=1 +targetcycle-debug-labels=0` 且 synth print 延后，只保留摘要，
  验证 targetcycle 打开但不大量打印时是否仍 PASS。只有这一步通过后，才考虑恢复
  full-debug 或重建不带 TargetCycleDebugWidget、只保留 Rocket PC synth printf 的
  hwdebug AGFI。
