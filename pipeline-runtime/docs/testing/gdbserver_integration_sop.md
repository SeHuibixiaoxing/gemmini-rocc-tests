# Pipeline Runtime gdbserver Integration SOP

更新时间：`2026-05-07 15:10 UTC`

## 1. 目标

- 给 `pipeline-runtime` 主线提供一条比 `TraceV` 更直接的 user-space 调试链路。
- 目标不是“再加一层日志”，而是直接拿到：
  - 当前停住线程的 `PC`
  - 指令窗口
  - 调用栈
  - 多线程状态
- 若 `gdb` 已能直接回答“卡在哪条指令 / 哪个函数 / 哪个线程”，本主线可不再依赖 `TraceV`。

## 2. 适用范围

- 适用于当前 `pairdummy / sbus128 / bertmini / Linux / FireSim FPGA` 主线。
- 不改共享 `br-base`，只对 workload-local rootfs 变体启用 `gdbserver`。
- 不适用于 baremetal `TraceV` bring-up；那条线仍看
  [`tracerv_integration_sop.md`](tracerv_integration_sop.md)。

## 3. 方案概述

这一条链路是：

`Buildroot gdbserver -> guest runner 用 gdbserver 拉起 rerocc_pipeline_runtime-linux -> workflow 在 run 前自动准备 example_1config + SSHPort switch + tap0 -> guest 通过 UART 宣告 endpoint -> host 通过 run host private IP 建隧道 -> cross-gdb 直接 target remote`

当前接入点：

- workflow：
  [`pairdummy_sbus128_gdbserver_workflow.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh)
- first-triage helper：
  [`run_pairdummy_cfg32_gdbserver_first_triage.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh)
- batch/expect triage helper：
  [`run_pairdummy_cfg32_gdbserver_expect_triage.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh)
- runtime config：
  [`config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver.yaml`](/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver.yaml)
- workload：
  [`rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json)
- guest runner：
  [`run_rerocc_pipeline_runtime_bertmini.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh)
- wrapper：
  [`run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh)

当前必须按目标分清三条路线：

1. `single-core Rocket + NIC + no TraceIO + 1BP` smoke 路线。它用于证明
   FireSim NIC、guest TCP、`gdbserver --once`、host cross-gdb 和基本 GDB
   操作是健康的。
2. `pairdummy / 12p4c128sbus32cfg / current NIC / optimized DMA` 路线。它是
   pipeline-runtime 当前要调试的目标路线，必须使用 `cfg32_nic` 构建和运行配置，
   不能把旧非 NIC 或陈旧 HWDB 当成已验证 bitstream。
3. local-gdb 路线。它用于 guest 内本地调试，不等价于 remote gdbserver；remote
   gdbserver 的关键验证是跨 NIC TCP attach、interrupt 和 detach。

已恢复的 single-core 1BP 基线：

- AGFI：`agfi-03d9518415ec82449`
- AFI：`afi-0bf1f9a2bdacaab09`
- top-level commit：`e884f5b8`
- `generators/gemmini` commit：`7b02d19`
- `generators/gemmini/software/gemmini-rocc-tests` commit：`797326d`
- 证据：
  - [`20260505T111031Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260505T111031Z.md)
  - [`20260505T121059Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260505T121059Z.md)
  - [`nic_gdbserver_current_1bp_recovered_20260505.md`](nic_gdbserver_current_1bp_recovered_20260505.md)

该基线已经覆盖：`target remote`、多个 software breakpoint、`continue`、`next`、
`info threads`、`thread apply all bt`、寄存器读取、反汇编、变量/内存读写、线程切换、
`Ctrl-C` 抢回控制和 `detach`。如果这条路线继续通过，而 `pairdummy cfg32_nic`
失败，优先把问题分到目标硬件、目标 rootfs/workload、pipeline-runtime 或
cfg32_nic 专属 FireSim 路径，不要回头怀疑通用 GDB 软件栈。

当前 `dummy8x8 / sbus64 / cfg32 / NIC / no TraceIO` 路线也已通过同等的
remote-gdbserver 操作矩阵：

- AGFI：`agfi-077451484fe3b63c3`
- AFI：`afi-07989ce9ce725a690`
- build result：
  `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`
- workflow：
  [`pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh)
- expect helper：
  [`run_pairdummy_cfg32_gdbserver_expect_triage.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh)
- debug records：
  [`20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass.md)
  和
  [`20260507T061559Z_dummy8x8_sbus64_gdbserver_extended_matrix_pass.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260507T061559Z_dummy8x8_sbus64_gdbserver_extended_matrix_pass.md)

该路线的关键前提是 runtime config 中保留已验证的 SimpleNIC plusargs：

```text
+simplenic-relaxed-required-bytes=1
+simplenic-empty-switch-poll-interval=1024
+simplenic-token-debug=0
+cpu-managed-stream-debug=0
+heartbeat-polling-interval=100000000
```

`run_pairdummy_cfg32_gdbserver_expect_triage.sh` 当前会验证：
`target remote`、`info threads`、`thread apply all bt`、寄存器读取、反汇编、
变量读写、内存读写、`break prt_main_entry`、`continue`、`next`、多个
software breakpoint、Ctrl-C 抢回控制和 `detach`。运行前仍然不要用
`nc`/telnet 探测端口；`gdbserver --once` 的第一条 TCP 连接必须来自 GDB。

### 3.1 无断点调用栈采样

`pipeline-runtime` 卡死定位不要求只能靠 breakpoint。对于未知卡点，优先使用
无业务断点的 stack sampling：

1. 等 UART 出现 `[gdbserver] phase=listening`。
2. 不用 `nc`、telnet、端口扫描或 TCP 探测；第一条连接仍必须是 GDB。
3. host 侧建立 SSH tunnel 后，让 cross-gdb `target remote`。
4. 先采集初始 `info threads`、`thread apply all bt`、寄存器和 `$pc` 反汇编。
5. `continue` 运行固定时间，再用 Ctrl-C interrupt。
6. 每次停住后采集 `info threads`、`thread apply all bt`、寄存器和 `$pc` 反汇编。
7. 如果 Ctrl-C 超时无法重新拿回 GDB prompt，把它记录为有效证据：目标可能已经在
   custom instruction、fence、MMIO 或其它不可中断硬件等待路径中。

当前 helper：

- [`run_pairdummy_cfg32_gdbserver_stack_sample.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_stack_sample.sh)

典型命令：

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_SAMPLE_COUNT=4 \
PRT_GDB_SAMPLE_SECONDS=75 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_stack_sample.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

解释采样结果时要分清三类观测源：

- GDB 栈：最后一次成功 interrupt 时的用户态线程现场。
- guest 磁盘日志：只说明已经落盘/同步到 rootfs image 的最后日志，可能落后于实际执行点。
- breadcrumb：mmap 文件中的低扰动 frontier，通常比文本日志更接近最后执行边界。

若三者不完全一致，不要直接合并成一个精确 PC；先以 breadcrumb frontier 和最后一次
GDB 栈共同缩小代码窗口，再决定是否需要下一轮更窄的 page/token 级探针。

运行后也可以动态加断点，但在当前 remote gdbserver 的常规 all-stop 用法里，实际流程是
先让目标停住，再修改断点集合。也就是说，`continue` 运行期间 GDB 没有普通命令 prompt；
需要用 `Ctrl-C` / `interrupt` 抢停，或者等已有断点命中，然后执行
`break file:line`、`break function`、`break *addr`、`condition`、`delete` 等命令，再
继续运行。不要把“必须在程序启动前把所有断点都想好”和“运行中可随时在不停止目标的情况
下改断点”混为一谈；本项目默认按“停住后动态增删断点”处理。

如果无断点采样停在 YAML 解析、artifact 校验、synthetic model prefault 等初始化阶段，
下一轮不要继续扩大采样秒数。更稳妥的做法是预设少量软件断点跨过初始化边界，例如
`prt_runtime.c` 的 `runtime init-step=ready`、segment begin、worker `pthread_create`
和 `stage_worker_main`。到达这些边界后，再动态补 `prt_gemm_conv_run`、
`sync_stage_export_aliases`、`dma_blocking_wait` 等更窄断点。

### 3.2 DMA frontier 定点采样

如果无断点采样只停在启动阶段、YAML 解析或其它非卡点路径，不要 detach 后继续等待。
`gdbserver --once` 的首连已经被消耗，后续无法可靠地再次拿用户态调用栈。下一轮应改用
DMA frontier helper，让 GDB 从程序入口一直运行到目标 `dma_blocking_wait` 附近：

- helper：
  [`run_pairdummy_cfg32_gdbserver_dma_frontier.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh)
- 默认断点：
  `dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 546`
- 断点命中后采集：
  `bt`、`thread apply all bt`、寄存器、`tok->id/stage_idx/tensor_id/rr_manager_id`、
  `rr_scope_valid/external`、`hw_done_flag`、`debug_src_addr/debug_dst_addr/debug_done_flag_pa/debug_bytes`
- 然后继续短时间并 Ctrl-C：
  - 若能抢回，栈就是该 DMA frontier 后的用户态现场。
  - 若 Ctrl-C 超时，说明已经进入 custom instruction/fence/MMIO 等难抢占硬件等待路径。
  - 若 inferior 退出，结合 breadcrumb 和 guest log 判断 fail-fast 是否生效。

典型命令：

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_POST_HIT_SECONDS=8 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

如果 token 有偏移，可临时放宽条件，例如：

```bash
PRT_GDB_FRONTIER_CONDITION='tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 520'
```

`dummy8x8/sbus64` profile 默认继承 `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`，用于保持与历史
低扰动 profile 一致。若本轮目标只是缩短 GDB 到达 DMA frontier 的启动时间，可在 workflow
前设置：

```bash
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
```

这只改变 artifact mapping 加载路径，不改变 DMA/RoCC 执行语义；启用后仍必须重做
`marshal-build`、`marshal-install`、freshness、`launch/infrasetup/run`，并在 debug record
里把 cache 状态写清楚。

### 3.3 源码条件 marker 定位

当已知卡点大概落在某个 segment、stage、tensor、page 或 token 附近时，优先使用
源码条件 marker，而不是继续扩大无断点采样时间。做法是在源码里保留一个
`noinline` 的 `prt_gdb_marker_stop()`，运行时先把当前上下文写入
`g_prt_gdb_marker_state`，再调用该函数。GDB 只需要打：

```gdb
break prt_gdb_marker_stop
continue
print g_prt_gdb_marker_state
thread apply all bt
```

到达 marker 后目标已经停住，此时可以安全地动态修改断点集合，例如删除
`prt_gdb_marker_stop`，再添加 `sync_stage_export_aliases`、`dma_blocking_wait`、
`prt_gemm_conv_run` 或具体 `file:line` 断点，然后继续运行。这就是“先用条件 marker
跳到局部窗口，再现场换更窄断点”的标准流程。

不要把源码 marker 写成普通的 `if (...) { int i; ++i; }`。在当前优化等级下，普通局部变量
和空操作很容易被优化、合并或重排，GDB 行号也可能漂移。应使用现有的
`prt_gdb_marker_note()` / `prt_gdb_marker_stop()` 路径：先把 segment、stage、tensor、
page、token 等语义状态写入 `volatile` 全局结构，再调用 `noinline` 停点函数。这样既能
保留“源码条件断点”的灵活性，也能保证 host 侧符号、全局状态和 backtrace 可解释。

当前 marker 默认关闭。单次 workflow 可通过以下变量打开和过滤：

```bash
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=runtime-ready
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=2
PIPELINE_RUNTIME_GDB_MARKER_PAGE=546
PIPELINE_RUNTIME_GDB_MARKER_TOKEN=546
```

空值、未设置、`any` 或 `*` 都表示不过滤该字段。常用 `SITE`：

- `artifact-mapping-parse-done` / `mapping-parse-done`：确认 layer mapping YAML 已解析完。
- `artifact-validate-done` / `validate-artifacts-done`：确认 artifact validation 已完成。
- `synthetic-model-prefault-begin` / `synthetic-prefault-begin`：定位 synthetic model blob
  预触页开始。
- `synthetic-model-prefault-end` / `synthetic-prefault-end`：确认 synthetic model blob
  预触页循环已经返回。
- `synthetic-model-ready` / `synthetic-ready`：确认 synthetic model blob 已 commit 到 runtime。
- `runtime-ready`：跳过 YAML、artifact 校验和 synthetic model prefault 的早期噪声。
- `segment-begin`：按 segment 边界收窄。
- `worker-entry`：确认 worker 线程是否已经进入目标 stage。
- `worker-gemm-run`：定位 compute 前后。
- `worker-export-sync` / `export-sync-tensor`：定位 export 同步路径。
- `dma-export-page-submit-begin` / `dma-export-page-submit-end`：按 export page 定位。
- `dma-wait-enter` / `dma-wait-return`：定位 DMA wait 是否返回。

约束：2026-05-08 之前的 DMA marker 只携带 local stage / manager / tensor / page /
token，不携带 segment、global stage、subbatch。用旧 guest binary 时，不要给
`dma-export-page-submit-*` 或 `dma-wait-*` 同时设置 `PIPELINE_RUNTIME_GDB_MARKER_SEGMENT`
/ `GLOBAL_STAGE` / `SUBBATCH`，否则 marker 会因为字段不匹配而永远不命中。当前
pipeline-runtime binary 已把 DMA marker 改为携带线程 TLS debug context；使用新 binary
并完成 image freshness 后，才可以用这些字段精确过滤到某个 segment/subbatch。

注意：这不是 hardware breakpoint；它仍是普通 software breakpoint 停在一个稳定函数符号上，
所以适合当前 1BP 硬件限制。由于 `gdbserver --once` 只能服务一次 TCP GDB 会话，
每轮 marker 条件要在启动前写入 guest env；命中后在同一 GDB session 内动态增删断点。

host 侧 ELF 必须使用 FireMarshal staged binary：
`generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`。
不要手工改成源码目录下的
`rerocc-linux-tests/rerocc_pipeline_runtime-linux`；二者可能 sha256 不同，GDB 仍可能撞到
同名函数地址，但全局变量、行号和调用栈会失真。当前 marker helper 默认使用 staged
binary：

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

若要在 marker 命中后立刻切换断点集合，不要让 helper 只做一次 smoke 后结束。可以把后续
GDB 命令通过环境变量注入；这些命令会在 marker state、bt、寄存器和 PC window 采集之后、
`detach` 之前执行：

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_DELETE_AFTER_HIT=1 \
PRT_GDB_POST_MARKER_GDB_CMDS='break sync_stage_export_aliases\ncontinue\nbt\nthread apply all bt' \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_marker_stop.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

更复杂的片段使用 `PRT_GDB_POST_MARKER_GDB_FILE=/abs/path/to/post.gdb`，避免多行 shell
转义出错。对于 `gdbserver --once`，这个“同一 GDB session 内换断点”的约束很关键：
一旦 `detach`，本轮 gdbserver 通常不能再被第二个 GDB 连接复用。

## 4. 关键约束

- 仍然必须走固定 FireSim workflow：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- 仍然必须通过：
  [`scripts/firesim-tmux-run.sh`](/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh)
- fresh run 前除了查 AWS F2 实例，也要查本地 manager 侧是否还有同 runtime config 的
  stale `firesim runworkload` 进程或旧 tmux session。旧 manager 即使对应的 F2 已经终止，
  仍可能继续轮询同一个 cluster tag，污染下一轮 runworkload 状态。检查命令：

```bash
pgrep -af 'firesim runworkload -c .*cfg32_nic_notrace'
tmux ls | grep 'pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload'
```

  若确认这些 session 对应的 F2 已经不存在，先终止本地 stale manager，再启动新的
  `launchrunfarm`。不要通过清理结果目录来解决这个问题。
- 不要改全局 watchdog；`pairdummy` 本地 profile 已把 live-idle 上限收敛到 `3600s`
- `gdbserver` 只是 workload-local rootfs 变体，不要把它塞回共享 `br-base`
- `gdbserver` 监听的是 guest 内网地址；host 侧要先通过 run host private IP 做 SSH 隧道
- 当前 gdbserver 专用 runtime config 已切到 `topology: example_1config`
- FireSim manager 当前实现仍会无条件读取 `target_config.no_net_num_nodes`；
  即使实际 topology 是 `example_1config`，YAML 里也要保留该字段作为 parser 兼容项
- `pairdummy_sbus128_gdbserver_workflow.sh run` 现在会自动执行：
  - 本地重写/重编 `switch0` 为 `SSHPort(1)` 版本
  - 下发到 run host `/home/ubuntu/switch_slot_0/switch0`
  - 在 run host 上建立 `tap0=172.16.0.1/16`
- `prepare_firesim_networked_gdbserver.sh` 默认还会在 run host 上执行
  `sudo pkill -x hw_server || true`，避免 Xilinx UDP discovery broadcast
  被 `tap0` 注入 guest NIC RX 路径；`virtual_jtag` 保留
- 但 “host 链路起来” 只解决了 FireSim topology 侧的 blocker；
  后文所有 live attach 仍然要求 guest 自己真的枚举出了可用 netdev，并配置出 IPv4
- `example_1config` 只是在 manager 侧生成
  `switch -> server` 的拓扑，并不会替 target 自动补一个 NIC；
  target 必须来自 `WithNIC_...` 硬件配置，DTS/地址图里必须能看到 `ice-nic@10016000`
- 历史上不带 `WithNIC` 的 `pairdummy 4c12p12 sbus128` 生成物已被静态证实不含 IceNIC；
  对这类旧 AGFI 不要继续尝试 guest TCP `gdbserver`
- 当前 `cfg32_nic` HWDB 指向 `agfi-02e18c6f7a7a95096`。该 AGFI 不能直接写成
  “当前源码 + optimized DMA + current NIC + cfg32_nic 已验证”，因为 AWS image
  名称和历史记录显示它可能是较早的 `WithNIC` 构建产物。
- 2026-05-01 `cfg32_nic` 主线构建目录：
  `sims/firesim/deploy/results-build/2026-05-01--02-36-56-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic/`
  已失败于 Vivado placement：
  `ERROR: [Place 30-99] Detail Placement failed` 和
  `ERROR: [Common 17-69] Command failed: Placer could not place all instances`。
  该轮没有产生可用新 AGFI。
- 因此，pipeline-runtime remote gdbserver 调试需要重新构建
  `12p4c128sbus32cfg + optimized DMA + current NIC`，并在 build 完成后更新/确认 HWDB。
- `firesim infrasetup` 会按本地源码重新生成并编译 host driver；
  如果本地 `SimpleNICBridge.scala` 等 target-side bridge 源码已经改过，
  但实际运行的仍是旧 AGFI，
  则这轮 run 只能用于验证 host-only 改动或观察旧 AGFI 的现象，
  不能直接被写成“RTL 修复已经在 FPGA 上验证”
- 当前围绕 `SimpleNIC` bulk 语义、`HostPort` 单步语义与 token 漂移风险的详细静态推理，
  见：
  [`firesim_simplenic_static_audit_20260424.md`](firesim_simplenic_static_audit_20260424.md)

## 5. 当前实现细节

### 5.1 Rootfs

workload-local Buildroot kfrag：

- [`linux_gdbserver.kfrag`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/linux_gdbserver.kfrag)

当前使用：

```text
BR2_PACKAGE_GDB=y
BR2_PACKAGE_GDB_SERVER=y
```

原因：

- 现有 external toolchain 里没有可直接复制的 target-side `gdbserver`
- 因此不能走 `BR2_TOOLCHAIN_EXTERNAL_GDB_SERVER_COPY=y`
- host 侧 cross-gdb 仍使用现成工具链：
  `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb`

### 5.2 Guest 运行方式

默认环境变量：

- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR=0.0.0.0`
- `PIPELINE_RUNTIME_GDBSERVER_PORT=2345`
- `PIPELINE_RUNTIME_GDBSERVER_INFO_PATH=/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info`
- `PIPELINE_RUNTIME_GDBSERVER_LOG_PATH=/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.log`

runner 在 `gdbserver` 模式下执行：

```text
gdbserver --once 0.0.0.0:2345 /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux ...
```

效果：

- 进程会先在 guest 内等待 gdb attach
- attach 之后再真正开始执行 runtime 用户程序
- 因此可以在程序最早阶段就抓住线程、PC 和调用栈

### 5.3 Live announcement

runner 会额外做两件事：

- 把 endpoint / guest IPv4 / 当前 pid 写到
  `/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info`
- 通过 `/dev/console` 往 UART 打一条极短 announcement：

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.x.y endpoint=172.16.x.y:2345 pid=...
```

目的：

- 不依赖 guest 文件 copy-back 才知道 guest IP
- run 还活着时，就能直接从 run host 上看到应该连哪个 guest IP
- BusyBox / Buildroot 场景下，`S99run` 的 `PATH` 不一定覆盖 `/sbin`
- 因此当前 runner 的 `guest_ipv4` 探测已经做了两层增强：
  - 裸 `ip` / `/sbin/ip` / `/bin/ip`
  - 裸 `ifconfig` / `/sbin/ifconfig` / `/bin/ifconfig`
  - 若未显式指定 `PIPELINE_RUNTIME_GDBSERVER_NET_DEV`，
    则优先选 `eth0`；
    否则在 `/sys/class/net/*` 中选择满足以下条件的真实以太网设备：
    `device` 存在、`type=1`、MAC 非全零
  - 最后再按 FireSim 默认规则，从 `/sys/class/net/<dev>/address` 推导 `172.16.<machigh>.<maclow>`

### 5.4 Inferior 跟踪

因为 `bin_pid` 在 `gdbserver` 模式下首先是 `gdbserver` wrapper 自己，
runner 还会轮询：

`/proc/<gdbserver_pid>/task/<gdbserver_pid>/children`

一旦 inferor 真实拉起，会补记：

- `gdbserver-inferior ...`
- `phase=inferior`

这样后续 `/proc` 快照不至于永远只盯着 `gdbserver` 自己。

### 5.5 静态 NIC 前置审计

新增 helper：

- [`audit_pairdummy_networked_gdbserver_target.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pairdummy_networked_gdbserver_target.sh)

用途：

- 在真正 `launchrunfarm` / `runworkload` 之前，先检查当前 `pairdummy` 生成目标是否真的把 IceNIC 接进了 target
- 如果生成 DTS 里完全没有 `ice-nic` / `nic@10016000` / `ethernet` 节点，则直接 fail-fast

当前静态结论要按目标区分：

- 旧的非 NIC 目标目录：
  `sims/firesim-staging/generated-src/firechip.chip.FireSim.WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  只含 `blkdev-controller@10015000`，没有 `ice-nic` / `nic@10016000` / `ethernet`。
  这类 AGFI 即使 host `tap0` / `switch0` 正确、guest `gdbserver` 已经 listen，也不会在 Linux 中出现可配置 IPv4 的真实 NIC。
- 新的 NIC 目标目录：
  `sims/firesim-staging/generated-src/firechip.chip.FireSim.WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  已确认 DTS/JSON/memmap 中包含 `ice-nic@10016000`。

因此，`network_target_audit_status=fail` 时应停止本轮 FPGA 测试；
`network_target_audit_status=pass` 只说明硬件前提具备，仍要继续看 Linux driver、`S40network`、guest IPv4 和 `gdbserver` 是否真正起来。

`PAIRDUMMY_NETWORK_AUDIT_TARGET_GLOB` 支持用 `:` 分隔多个候选 glob。当前
`cfg32_nic` gdbserver workflow 默认同时接受主线
`WithNIC_WithDefaultFireSimBridges_...Dummy16x16Sbus128` 和 no-TraceIO
`FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig` 生成目录。这样
主线或 noTrace 任一新 AGFI 先完成时，运行前审计都检查实际 DTS 是否包含 IceNIC，
而不会因为 target 名字不同被误拦。

### 5.6 cfg32 + NIC 构建状态

标准 `12p4c128sbus32cfg` 路线使用以下 `WithNIC` F2 build 配置：

- build config：
  [`config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml`](/home/ubuntu/chipyard/sims/firesim/deploy/config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml)
- recipe：
  [`config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml`](/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml)
- build EC2：`z1d.3xlarge`
- build strategy：`TIMING`
- target frequency：`20MHz`

2026-05-01 的一次构建已确认吃到了 NIC 和 DMA 优化相关 RTL，但最终 placement
失败，没有可用 AGFI。失败证据：

- manager log：
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-05-01--02-36-56-buildbitstream-M0F3Z2HY8GA258HG.log`

2026-05-05 当前并行跑两条构建：

- 主线 cfg32 NIC：
  `pairdummy-cfg32-nic-mainline-20260505T132956Z`，使用
  `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
  + `FRFCFS16GBQuadRank_BaseF2Config`，`TIMING`，`20MHz`。该路线保留 TraceIO/TracerV
  相关桥接输入，目标是验证最接近当前主线的 bitstream。
- no-TraceIO 资源削减 cfg32 NIC：
  `pairdummy-cfg32-nic-notrace-20260505T171453Z`，使用
  `FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig`
  + `FRFCFS16GBQuadRank_BaseF2Config`，`TIMING`，`20MHz`。该路线保留 NIC、blockdev、
  FASED 和默认 FireSim bridge 组合，但通过 `chipyard.config.WithNoTraceIO`
  去掉 target TraceIO，目的是降低 F2 placement 压力。

两个构建都不是最终验证本身；只有新 AGFI 写入 cfg32 NIC HWDB、重新
`infrasetup` 并跑过 remote `gdbserver` attach 后，才能把对应硬件记为通过。
如果主线失败而 no-TraceIO 成功，优先用 no-TraceIO AGFI 打通 gdbserver，因为当前调试目标是
user-space pipeline-runtime hang，不依赖 TraceIO。
- build result：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-build/2026-05-01--02-36-56-firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic/`
- failure：
  `ERROR: [Place 30-99] Placer failed with error: 'Detail Placement failed please check previous errors for details.'`
  followed by `ERROR: [Common 17-69] Command failed: Placer could not place all instances`

后续重新构建时仍先用 `TIMING`，因为这条路线历史上更接近能稳定产出可用 F2
bitstream 的策略。只有当新失败日志明确指向 placement/timing 之外的可解释原因时，
才考虑策略变体；策略变化必须单独记录，不能和 RTL 或 workload 改动混在一个结论里。

推荐启动命令：

```bash
cd /home/ubuntu/chipyard
SESSION=pairdummy-cfg32-nic-mainline-$(date -u +%Y%m%dT%H%M%SZ)
scripts/firesim-tmux-run.sh --session-name "${SESSION}" \
  buildbitstream \
  -b config_build_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_cfg32_nic.yaml \
  -r config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_nic.yaml
```

启动后必须记录：

- `SESSION`
- pane log：`tmp/firesim-aws-f2/tmux/${SESSION}.pane.log`
- build host instance id/private IP
- config-specific CL 目录
- RTL freshness gate 结果

### 5.7 当前 P0 gdbserver 使用流程

当前已经验证可用的 remote-gdbserver 路线是
`dummy8x8 / 4c12p12 / sbus64 / cfg32 / NIC / no TraceIO`：

- AGFI：`agfi-077451484fe3b63c3`
- AFI：`afi-07989ce9ce725a690`
- workflow：
  [`pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh)
- expect triage：
  [`run_pairdummy_cfg32_gdbserver_expect_triage.sh`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh)

标准步骤：

```bash
cd /home/ubuntu/chipyard

generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh run <run-host-private-ip>
```

等 guest UART 或镜像里的 info 文件出现：

```text
[gdbserver] phase=listening ... guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345
Listening on port 2345
```

如果 run host 上没有稳定 ARP，先让 helper 安装静态 neighbor；这不会连接
`gdbserver --once`，不会消耗第一次 TCP 连接：

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

当前 helper 的通过判据是出现以下 marker：

```text
GDB_MARK_CONNECTED
GDB_MARK_MEMORY_RW_DONE
GDB_MARK_HIT_MAIN_ENTRY
GDB_MARK_NEXT_DONE
GDB_MARK_HIT_FIRST_BREAK
GDB_MARK_INTERRUPT_BEGIN
GDB_MARK_INTERRUPT_DONE
GDB_MARK_DETACH_OK
```

P0 卡点分类时优先看这些断点和栈：

| 分类 | 主要断点 / 现象 | 下一步 |
| --- | --- | --- |
| pipe/ring wait | `stage_prepare_exec_views` 之后卡在 pipe/ring wait 栈 | 看 `prt_debug_state`、pipebuf/ring 指针、producer/consumer stage |
| DMA submit/wait/fence | `prt_dma_submit`、`prt_dma_wait`、`dma_blocking_wait` | 同时读 token、`hw_done_flag`、completion flag、DMA manager id |
| ReRoCC acquire/fence/release | `prt_gemmini_spm_xlate_program`、`prt_gemmini_spm_xlate_flush`、`prt_rr_release_scope` | 看 cfg id、manager id、scope 是否 external、fault CSR |
| Gemmini compute/fence | `prt_gemm_conv_run`、`prt_gemm_fence` | 看 task manager set、conv/resadd descriptor 和 alias address range |
| thread join/stop/fatal | Ctrl-C 后 `thread apply all bt` 停在 join/wait 或 fatal path | 读 `rt->fatal_error`、`rt->stop_requested`、worker thread 栈 |

测试结束后必须先收集：

- `uartlog`
- `heartbeat.csv`
- `sim-run.sh`
- `switchlog`
- gdb transcript / expect stdout
- 可选 pcap 和 tcpdump log

收集完立刻执行：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

然后用 AWS 查询确认 F2 instance 不再是 `running`。任何 gdbserver 关键测试通过或失败后，
都要在 `gemmini-rocc-tests`、`generators/gemmini` 和 top-level 仓库逐级做 checkpoint
commit，commit message 写清 AGFI/AFI、runtime/HWDB、命令、结果、证据目录和限制。

## 6. 标准执行步骤

### 6.0 buildbitstream 启动前冻结

NIC / gdbserver 相关 bitstream 构建时间长，启动前必须先把本轮硬件改动想清楚并冻结。
不要在构建已经开始后再继续思考“还应该加哪些硬件观测点”，更不要把构建中的 AGFI
当成包含后补源码改动的版本。

启动任何新的 NIC/gdbserver `buildbitstream` 前，必须完成：

1. 复核历史记录：
   `debug_records/CATEGORY_INDEX.md`、`change_records/CATEGORY_INDEX.md` 以及最近 NIC/gdbserver 记录，
   确认历史已修 bug 没有被回退，当前源码包含上一轮成功测试所需的修复。
2. 静态排查当前卡点：
   同时读 target bridge、FireSim stream engine、host driver、switch/topology、runtime config 和 workload，
   先判断是否能用软件、rootfs、gdbserver、metasim 或旧 AGFI 复现，不能直接跳到新 bitstream。
3. 冻结可综合观测面：
   一次性加入本轮需要的低扇出 CSR/计数器/sticky snapshot，覆盖从 host switch、
   CPU-managed stream、BigToken adapter、SimpleNIC queues、ChannelizedHostPort leaf、
   IceNIC target 端 ready/valid/fire 的完整链路。
4. 冻结源码 marker：
   每轮硬件观测面变化必须更新可读 marker，例如 `minobs_build_marker`，
   并在 debug/change record 里写明该 marker 对应哪些信号。
5. 完成静态校验：
   Scala `attach(...)` 顺序与 C++ `SIMPLENICBRIDGEMODULE_struct` 顺序必须完全一致；
   host bridge C++ 至少跑 `g++ -std=c++17 -fsyntax-only`；
   再跑 `git diff --check`。
6. 写清楚本轮 bitstream 预期回答的问题：
   例如“target cycle 是否推进”“host->target payload 是否进入 adapter”
   “哪个 leaf ready/valid 长期卡住”“是否出现 PCIS/stream backpressure”。

可以一次性规划多个并行 `buildbitstream`，例如 A/B 配置对比或不同规模配置回归；
但每个构建都必须在启动前独立冻结配置、源码 marker、观测点、预期问题和 freshness gate。
禁止先启动一个构建，等待过程中又发现同一卡点没想清楚，再临时修改硬件并启动“补丁版”
构建。构建开始后如果又发现新的硬件观测需求，只能记录到下一轮候选清单；是否启动下一轮，
必须先说明当前已启动构建能回答什么、不能回答什么，以及为什么无法等测试结果。

### 6.0.1 buildbitstream 启动后的 RTL freshness gate

每次启动 `buildbitstream` 后，必须立刻再做一次 RTL freshness 检查。原因是 FireSim F2 构建会先把通用 CL 目录：

```text
sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim
```

复制成当前 build 对应的 config-specific CL 目录，例如：

```text
sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config
```

真正进入该轮 bitstream 的是 config-specific 目录里的 Verilog。只检查源码目录不够；如果 build 已经在修改前启动，或者复制出来的目录仍是旧内容，就会出现“以为构建了新修复，实际 AGFI 还是旧 RTL”的错误。

强制步骤：

1. 用 `scripts/firesim-tmux-run.sh buildbitstream ...` 启动构建后，先看 tmux pane log，确认已经出现 `cp -rf ... cl_firesim -T ... cl_<deploytriplet>`，并记录本轮 `cl_<deploytriplet>` 目录。
2. 在本地 config-specific CL 目录中检查最新 RTL 改动。以 F2 NIC read-lane 修复为例：

```bash
CL_DIR=/home/ubuntu/chipyard/sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_f2-firesim-FireSim-FireSimRocketNICNoTraceConfig-BaseF2Config

rg -n "fpga_pci_peek64|read_lane_mask = 8'b0000_0001 << lane" \
  "$CL_DIR/design/cl_firesim.sv"

cmp -s \
  /home/ubuntu/chipyard/sims/firesim/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/cl_firesim/design/cl_firesim.sv \
  "$CL_DIR/design/cl_firesim.sv"
```

3. 如果本轮修改涉及 generated RTL，也必须检查实际 config-specific generated 文件，例如：

```bash
rg -n "<本轮新增 signal/module/assertion/常量名>" \
  "$CL_DIR/design/FireSim-generated.sv" \
  "$CL_DIR/design/FireSim-generated.defines.vh"
```

4. build host 启动后，尽量再 SSH 到 build host private IP 检查远端副本。路径通常是：

```text
/home/ubuntu/firesim-build/platforms/f2/aws-fpga-firesim-f2/hdk/cl/developer_designs/<cl_deploytriplet>/design/
```

如果远端 `cl_firesim.sv` / generated Verilog 不含本轮改动，立即停止该 buildbitstream tmux，并终止对应 build EC2；不要等它完成，也不要把该 AGFI 当成新修复结果测试。

NIC cycle-0 deadlock 修复的 freshness gate 还必须额外检查以下 generated RTL：

```bash
rg -n "configChannelReady|fromHostChannelReady|fromHostChannelValid|targetCycleReady|targetBlockedFromHostChannels" \
  "$CL_DIR/design/FireSim-generated.sv"
```

期望至少看到：

```text
configChannelReady = hPort_macAddr_ready & hPort_rlimit_ready & hPort_pauser_ready
fromHostChannelReady = hPort_nicIn_ready & configChannelReady
targetCycleReady = toHostReadyDrive & fromHostChannelReady
targetBlockedFromHostChannels = ~fromHostChannelReady
```

如果只看到 `fromHostChannelReady = hPort_nicIn_ready` 或
`targetCycleReady = ... hPort_nicIn_ready`，说明构建仍在使用会导致 cycle 0
deadlock 的旧 generated RTL，必须停止该轮构建。

对 `12p4c128sbus32cfg + optimized DMA + current NIC` 构建，freshness gate
还必须检查：

```bash
rg -n "SimpleNICBridgeModule|ice-nic@10016000|bytes_written_per_beat|write_shift|MAX_CFGS|cfg32" \
  "$CL_DIR/design/FireSim-generated.sv" \
  "$CL_DIR/design/FireSim-generated.defines.vh" \
  "$CL_DIR" \
  2>/dev/null
```

最小期望：

- generated RTL 中含 `SimpleNICBridgeModule`
- DTS/metadata 中含 `ice-nic@10016000`
- DMA RTL 中含 optimized DMA marker，例如 `bytes_written_per_beat`、`write_shift`
- ReRoCC/cfg 相关生成物能证明使用的是 cfg32 路线，而不是旧 cfg 槽数量

如果 freshness gate 找不到这些 marker，不要等 Vivado 完成；先停止构建并记录为
“构建输入不新鲜/目标不对”。

2026-05-05 当前 cfg32 NIC 构建 `pairdummy-cfg32-nic-mainline-20260505T132956Z`
已通过本节 freshness gate：本地与远端 build host `192.168.0.60` 的
`FireSim-generated.sv` 均为 `2026-05-05 16:46:07 UTC`、`166512822` bytes；
生成 RTL 同时包含 `SimpleNICBridgeModule`、`bytes_written_per_beat` 和
`write_shift`；`network-audit` 找到 `ice-nic@10016000`。该轮仍未产生
AGFI/AFI，尚不能更新 HWDB 或启动 cfg32 NIC gdbserver run。

2026-05-05 17:02 UTC 复查：该构建已完成 GoldenGate 和远端源码同步，当前在 build host
`i-0479b74dd4de8e428` / `192.168.0.60` 上执行 Vivado customer CL 综合。远端主进程是
`vivado -mode batch -source build_all.tcl ... SSI_SpreadLogic_high AggressiveExplore AggressiveExplore ... H2`，
并伴随多个 parallel synth worker；日志已有多条 `synth_design completed successfully`，但还没有
可用于判断资源余量的 utilization/place/timing report，也没有 AGFI/AFI。此时不应停止构建或更新
HWDB；继续按 1200s 轮询等待 post-synth/place 结果。

2026-05-05 19:14 UTC 复查：主线 `cfg32_nic` 构建已完成 `link_design` 和
`opt_design`，并进入 `place_design -directive ExtraNetDelay_high -no_bufg_opt`。
post-synth/post-opt 没有报 error；post-synth `cl_firesim` LUT 约 `96.96%`，
`firesim_top` 约 `94.18%`，说明资源压力很高但尚未失败。no-TraceIO fallback 已经进入
GoldenGate 后段，已确认实例化 `SimpleNICBridgeModule`，但此时尚未生成
`FireSim-generated.sv`，也没有 AGFI/AFI。两条构建仍只能记为“进行中”，不能更新 HWDB。

2026-05-05 19:22 UTC 复查：no-TraceIO fallback 已完成本地 generated RTL 并启动远端
Vivado build host `i-0ecf73dad2a8ae8d0` / `192.168.0.213`。该轮远端 log 是
`2026_05_05-191909.vivado.log`。本地 freshness 已确认：
`FireSim-generated.sv` 约 `158 MiB`，含 `SimpleNICBridgeModule`、`IceNIC`、
`bytes_written_per_beat` 和 `write_shift`；DTS 含 `ice-nic@10016000` 和
`rerocc-mgr@2b000`；4 个 CPU 的 `hardware-exec-breakpoint-count` 均为 `1`；
generated RTL 中 `TracerVBridge`、`TraceIO`、`TracePort` 计数为 0。该轮仍未产生
AGFI/AFI，仍不能更新 HWDB。

2026-05-05 21:46 UTC 复查：主线 cfg32 NIC 构建
`pairdummy-cfg32-nic-mainline-20260505T132956Z` 已失败，没有产生 AGFI/AFI。
失败不是 runworkload、rootfs 或 gdbserver 问题，而是 Vivado placement 问题：
`Phase 3 Detail Placement` 报 `Place 30-487`，CL pblock 中可用 `26405`
个 CLBs，而未放置实例需要 `29960` 个 CLBs，随后 `Place 30-99` /
`Common 17-69` 结束构建。该轮 post-synth `cl_firesim` 总 LUT 约 `96.96%`。
详细记录见
[`20260505T214600Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260505T214600Z.md)。

2026-05-05 21:49 UTC 复查：no-TraceIO fallback 仍在远端 Vivado placement，
最新阶段是 `Phase 1.3 Build Placer Netlist Model`，仍没有 AGFI/AFI。它的
post-synth `cl_firesim` 总 LUT 约 `89.67%`，`firesim_top` 约 `86.89%`，
比主线释放了约 7 个百分点的总 LUT 压力；因此当前继续等待 noTrace TIMING
构建是合理路线。该轮 post-opt timing 仍有 `SH_DDR/SYNC_RST` 到
`mmcm_clkout0` 的 async recovery slack，例如 `-1.284 ns`，但当前是否可用的
第一判断仍是 placement/route 能否完成并生成 AGFI。

2026-05-06 00:08 UTC 复查：no-TraceIO fallback 已完成
`Phase 3 Detail Placement`，进入
`Phase 4 Post Placement Optimization and Clean-Up` /
`Phase 4.1.1.2 Post Placement Timing Optimization`。这跨过了主线 cfg32
NIC 构建失败的同一 detail placement 边界；截至该 poll 仍没有 AGFI/AFI，也还没有
post-place checkpoint/report。继续等待 post-place、route、bitstream 和 AFI creation。

2026-05-06 00:19 UTC 复查：no-TraceIO fallback 的远端 Vivado 日志已经出现
`place_design completed successfully`，随后开始写 post-place checkpoint/report。
该轮 placement 阶段 `0 Critical Warnings and 0 Errors`，但 Vivado 同时提示设计
`highly congested and may have difficulty routing`，post-placement timing summary
为 `WNS=-3.250`。因此当前判断是：placement capacity blocker 已解除，下一风险是
route/timing closure；仍未产生 AGFI/AFI，仍不能更新 HWDB 或启动 cfg32 NIC
gdbserver run。

2026-05-06 00:49 UTC 复查：no-TraceIO fallback 已执行 pre-route
`phys_opt_design -directive AggressiveExplore`，完成多个
`Single Cell Placement Optimization` / `Multi Cell Placement Optimization`
阶段，并进入 `Writing post-phy_opt design checkpoint and report`。这说明构建已从
placement 推进到 pre-route physical optimization 之后；仍未进入可见 route
完成、bitstream 或 AFI creation，仍不能更新 HWDB 或启动 runfarm。

2026-05-06 01:09 UTC 复查：no-TraceIO fallback 已进入
`route_design -tns_cleanup -directive Explore -timing_summary`，并完成
`Phase 2.2 Pre Route Cleanup` 与 `Phase 2.3 Global Clock Net Routing`。此时
`post_phys_opt.dcp` 和 `post_phy_opt_timing.rpt` 已出现；Vivado 仍在跑，仍未产生
AGFI/AFI。继续等待 route 完成、bitstream 和 AFI creation。

2026-05-06 01:29 UTC 复查：no-TraceIO fallback route 继续推进，已完成
`Phase 2 Router Initialization`、`Phase 3 Global Routing` 和
`Phase 4 Initial Routing` / `Phase 4.1 Initial Net Routing Pass`。没有新的 route
report/checkpoint、bitstream、AGFI 或 AFI；Vivado 仍在跑。继续按 1200s 等待。

2026-05-06 09:34 UTC 复查：no-TraceIO fallback 最终失败于
`route_design -tns_cleanup -directive Explore -timing_summary`。失败不是 timing-only；
Vivado 报 `ERROR: [Route 35-2] Design is not legally routed. There are 5975 node
overlaps`，随后 `ERROR: [Constraints 18-1000] Routing results verification
failed due to partially-conflicted nets`。没有 post-route DCP、bitstream、AFI 或
AGFI。该构建不能用于 gdbserver；cfg32 NIC HWDB 不应更新。

每次汇报 buildbitstream 已启动时，都要同时汇报：

- tmux session 名称；
- build EC2 instance id / private IP；
- 本轮 config-specific CL 目录；
- freshness gate 检查的关键 `rg/cmp` 结果。

### 6.0.2 长构建无人值守监控

当目标是“构建完成后继续调试，必要时再自动启动下一轮构建”时，不要在
`buildbitstream` 尚未完成时给出最终结论并停止本轮工作。正确做法是进入低频
轮询循环：

```bash
SESSION=<buildbitstream-tmux-session>
while true; do
  sleep 1200
  EXITFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.exitcode
  LOGFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.pane.log

  if [ -f "${EXITFILE}" ]; then
    echo "exitcode=$(cat "${EXITFILE}")"
    tail -n 160 "${LOGFILE}"
    break
  fi

  if tmux has-session -t "${SESSION}" 2>/dev/null; then
    echo "[monitor] ${SESSION} still running at $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    tail -n 80 "${LOGFILE}"
  else
    echo "[monitor] ${SESSION} missing and no exitcode file"
    tail -n 160 "${LOGFILE}"
    break
  fi
done
```

轮询原则：

- 默认间隔是 `1200s`，除非用户明确要求更短；
- 每轮只看 `exitcode`、tmux 是否还在、pane log 尾部，不做高频日志刷新；
- 在 Codex/统一 exec 环境里，不要长期保留很多 `sleep 1200`、
  `tail -f`、`tmux attach` 或 SSH tunnel 会话。长时间等待应由 tmux wrapper
  或 detached monitor 写日志承载，交互侧只用短命令读取 `exitcode` 和 log tail；
- 如果发现失败，先读完整错误链路，静态排查并修改；需要新硬件时再启动下一轮
  `buildbitstream`；
- 如果发现成功，先识别最新 AGFI / AFI，并校验 HWDB 指向该 AGFI，再进入
  `launchrunfarm -> infrasetup -> runworkload`；
- 下一轮 `runworkload` 失败或卡住时，先保留现场、手动搬回日志和抓包，再决定是否
  `terminaterunfarm`；
- 如果又需要硬件修改，启动下一轮 `buildbitstream` 后继续使用本节的 `1200s`
  轮询策略。

监控期间如果本地出现大量遗留进程，优先清理无用的 `tmux attach`、旧
`runworkload`、旧 SSH tunnel 或旧 `sleep` 监控进程；不要杀正在运行的
`buildbitstream` tmux session、Vivado build host 或仍需保留的失败现场。

如果确实需要无人值守地记录构建状态，优先启动一个单独的 detached tmux monitor，
而不是让交互式 `exec` 一直占着进程槽。例如：

```bash
MONITOR=monitor-${SESSION}
tmux new-session -d -s "${MONITOR}" "bash -lc '
  set -euo pipefail
  while true; do
    sleep 1200
    date -u +%Y-%m-%dT%H:%M:%SZ
    EXITFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.exitcode
    LOGFILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/${SESSION}.pane.log
    if [ -f \"${EXITFILE}\" ]; then
      echo exitcode=\$(cat \"${EXITFILE}\")
      tail -n 160 \"${LOGFILE}\" || true
      break
    fi
    tail -n 80 \"${LOGFILE}\" || true
  done
'"
```

这个 monitor 只负责落日志和停止在构建完成点；发现失败后的静态排查、修改和是否
启动下一轮 bitstream，仍然需要人工/AI 读取日志后执行，不能用脚本盲目自动重构。

### 6.0.3 完整自动调试循环

本项目当前推荐的闭环是：

1. 启动 `buildbitstream`，立刻执行 6.0 的 RTL freshness gate。
2. 构建期按 6.0.1 每 `1200s` 低频轮询。
3. 构建成功后，确认 HWDB 使用最新 AGFI，重新执行 `infrasetup`。
4. 启动 `runworkload` 后，等待 guest boot、网络起来和 `[gdbserver] phase=listening`。
5. 只用真正的 `gdb target remote` 消费 `gdbserver --once` 连接，不用
   `nc`/`telnet` 预探测端口。
6. 能 attach 时，记录 `bt`、`info threads`、寄存器和关键断点结果。
7. 不能 attach、通信损坏或 workload 卡死时，先把 run host 的
   `uartlog`、`heartbeat.csv`、switch 目录、pcap、guest debug 文件和本地 tmux
   log 搬回本地。
8. 现场保存后，静态排查错误边界；如果是 host-only 或软件问题，直接修复并重跑；
   如果必须改 RTL 且 metasim/baremetal 无法覆盖，再启动下一轮 bitstream。

该循环的停止条件是：remote `gdbserver` 能稳定 attach 到目标程序，能在运行中用
`Ctrl-C`/`interrupt` 抢回 prompt，并能读取实时调用栈、线程、寄存器和指令窗口。
如果目标程序继续暴露 pipeline-runtime 自身问题，应把问题转入软件/runtime 调试，
而不是继续把它归为 NIC/gdbserver 链路问题。

### 6.1 构镜

```bash
cd /home/ubuntu/chipyard
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh image-closure
```

`12p4c128sbus32cfg + NIC` 专用路径必须使用 cfg32 wrapper：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh image-closure
```

构镜完成后，可做本地 freshness：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh local-freshness
```

至少应确认：

- guest image 含 `/usr/bin/gdbserver`
- `/firemarshal.env` 里已经带上 `PIPELINE_RUNTIME_GDBSERVER_*`
- runner / wrapper / runtime binary 都是最新版本

### 6.2 FireSim 启动

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh run
```

`cfg32_nic` bitstream 验证对应命令：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh infrasetup
RUN_HOST_PRIVATE_IP="$(generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh current-private-ip)"
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh run "${RUN_HOST_PRIVATE_IP}"
```

说明：

- `launch` / `infrasetup` / `run` 现在都会先做静态 NIC 审计。
- 对 `cfg32_nic`，构建成功后必须先把
  `config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml`
  更新到新 AGFI 和匹配的 `driver_tar`，并提交该状态；当前旧 HWDB 指向
  `agfi-02e18c6f7a7a95096`，只能作为历史记录，不能作为新 bitstream 验收输入。
- 如果它们在本地直接报
  `network_target_audit_status=fail`，
  不要继续耗费 FPGA 机时；这代表当前 bitstream 不满足 guest TCP `gdbserver` 的基本前提。
- `run` 会自动做 network prepare。
- 若只想单独验证 switch/tap0 准备，也可执行：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh network-audit
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh network-prepare
```

`cfg32_nic` 单独验证：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh network-audit
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh network-prepare
```

### 6.2.1 cfg32 NIC 构建成功后的强制检查点

新 `cfg32_nic` AGFI 生成后，进入 runfarm 之前必须做一个 git 检查点。该 commit 至少要写清：

- AGFI / AFI / build result 目录；
- build config、recipe、deploy triplet、strategy、frequency；
- buildbitstream session、manager log、pane log、远端 Vivado log；
- freshness gate 结果和关键 marker；
- HWDB 更新的 AGFI 与 `driver_tar`；
- 当前未验证项：还没有跑 gdbserver workload、还没有证明 pipeline-runtime 能继续执行。

不要把 HWDB 更新和后续 runtime 代码修复混在同一个 commit。先提交“新 bitstream 可引用”的状态，
再启动 `launchrunfarm -> infrasetup -> runworkload`。

### 6.2.2 cfg32 NIC 首轮 attach 判据

首轮目标是证明“pipeline-runtime 可以被 remote gdbserver 稳定接管”，不是证明模型结果正确。
最小通过条件：

- guest UART 出现 IceNet probe / network OK / `[gdbserver] phase=listening`，且 `guest_ipv4` 非空；
- manager 到 run host 的 SSH tunnel 建立成功；
- 第一次 TCP 客户端是真实 cross-gdb；
- `target remote :32345` 成功停住 inferior；
- `info threads` 和 `thread apply all bt` 能返回 runtime 线程；
- 能读 `rt->cfg.sync_mode`、`rt->cfg.dma_backend`、`rt->cfg.gemmini_mode`；
- 能 `Ctrl-C` 抢回 prompt，并能 `detach` 或 `continue` 到预期状态。

若这些通过而 runtime 后续卡死，结论应写成“gdbserver 链路通过，pipeline-runtime 工作负载暴露
软件/硬件执行卡点”，再用 backtrace 继续定位，不要把它归类为 gdbserver/NIC attach 失败。

### 6.3 取 run host private IP

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_workflow.sh current-private-ip
```

### 6.4 观察 live gdbserver announcement

从 manager 机器 SSH 到 run host private IP，
直接看 live `uartlog`：

```bash
ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@<run-host-private-ip> \
  'grep -n "\[gdbserver\]" /home/ubuntu/sim_slot_0/uartlog | tail -n 20'
```

如果只想持续观察：

```bash
ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@<run-host-private-ip> \
  'tail -f /home/ubuntu/sim_slot_0/uartlog'
```

拿到 `guest_ipv4=172.16.x.y` 之后，再继续。

如果 announcement 仍是 `guest_ipv4=unknown`，不要直接推断 “host 拓扑没起来”。
先区分三种情况：

- `network-audit` 已经 fail
  这说明 blocker 在 bitstream 根本没带 NIC；
  此时不应再继续做 guest netdev 枚举或 host 侧连 `172.16.x.y`
- `tap0` 仍是 `NO-CARRIER`
  这说明 topology / switch / tap0 链路本身还没拉起
- `tap0` 已是 `LOWER_UP`，但 `gdbserver.info` 里 `net_dev=` 与 `guest_ipv4=` 都为空
  这说明当前 blocker 已经转成 guest 侧没有可用 netdev
- `tap0` 已是 `LOWER_UP`，`net_dev` 有值，但 `guest_ipv4` 为空
  这说明 guest 枚举到了接口，但 IPv4 没配上

## 7. 建隧道与 attach

### 7.1 本地 SSH 隧道

在 manager 机器上执行：

```bash
ssh -N \
  -L 32345:172.16.x.y:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip>
```

说明：

- 左边 `32345` 是 manager 本地端口；
  这里故意不用 `2345`，避免和本机已有服务或上一轮残留隧道冲突
- 右边 `172.16.x.y:2345` 是 guest announcement 给出的 endpoint
- 这个 SSH 会保持前台，调试期间不要退出
- 只有当 host 能实际到达 guest 时，这条隧道才会成功
- 不要用 `nc` / `telnet` 去“试一下端口”：
  本项目 runner 使用 `gdbserver --once`，第一个 TCP 连接会被当成唯一的 gdb 会话；
  `nc` / `telnet` 会消耗这次连接，导致后续真正的 `gdb target remote` 失败
- run host 上的 Xilinx `hw_server` 会周期性发 UDP discovery broadcast。
  在 NIC smoke 实测里，这些包会穿过 `tap0` 进入 SimpleNIC host-to-target 路径，
  造成大量早期网络噪声。当前 network prepare helper 已默认执行
  `sudo pkill -x hw_server || true`；如果绕过 helper 手工跑 FireSim，
  必须自己在 `infrasetup` 后、`runworkload` 前执行同一条命令。
  保留 `virtual_jtag` 不影响这条 gdbserver smoke 路线。
- 如果本地已有旧 tunnel，先杀掉旧进程再开新 tunnel：

```bash
pgrep -af 'ssh .*32345' || true
kill <old-ssh-pid>
```

- tunnel 只能证明 manager 能把 TCP 转发到 run host；
  真正的 guest 端口是否可用，以 `gdb target remote` 为准。
  不要改用 `nc`/`telnet` 预探测。

最新实测边界：

- single-core Rocket + NIC + no TraceIO + 1BP smoke 已经在当前恢复后的 AGFI
  `agfi-03d9518415ec82449` 上重复验证过。guest 能 boot、网络能起来，remote `gdb`
  能连接 `gdbserver`，并能完成软件断点、线程/栈/寄存器/内存读写、`Ctrl-C` 和
  `detach`。
- 旧 single-core NIC AGFI `agfi-019e0b22099a5214b` 还暴露出一个空 RX 路径问题：
  如果 run host 完全没有 host-to-target 包，guest 可能长时间停在早期 Linux boot，
  UART 同时反复出现
  `CPU_STREAM DEBUG pull_blocked ... SIMPLENICBRIDGEMODULE_0_to_cpu_stream ... count=0 ... required_bytes=0`。
  注入少量 `ping 172.16.0.2`/ARP 包可以把旧 AGFI 推进到 IceNet probe 和 gdbserver prelaunch。
  这只是旧 bitstream 的诊断/推进手段，不应写成新 RTL 的正确行为。
- 旧的 `pairdummy` 非 NIC AGFI 不能用于 guest TCP `gdbserver`，因为 target 本身没有 IceNIC
- 2026-05-01 `cfg32 + WithNIC` pairdummy build 已失败于 placement，没有可用新 AGFI。
  后续 pipeline-runtime remote gdbserver 调试必须先重新构建并更新 HWDB。

### 7.2 启动 host cross-gdb

新开一个 manager shell：

```bash
/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

或者先用可复现的 batch/expect triage helper 留证据：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  <run-host-private-ip> <guest-ip-or-endpoint> [local-port]
```

它会自动：

- 检查 host-side `rerocc_pipeline_runtime-linux` 是否带 debug info
- 建立 `ssh -L` tunnel
- 用真实 GDB 执行第一条 `target remote` TCP 连接
- 记录初始 `info threads` / `thread apply all bt` / registers / disassembly
- 设置当前已核实的 pipeline-runtime 软件断点集
- `continue` 到第一个断点并记录栈、线程、寄存器和反汇编
- 禁用断点后用 `Ctrl-C` 抢回控制并记录现场
- `detach`

输出目录形如：

```text
tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-expect-*
```

如果明确需要交互式 GDB，再用一键 interactive triage helper：

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh \
  <run-host-private-ip> <guest-ip-or-endpoint> [local-port]
```

它会自动：

- 检查当前 host-side `rerocc_pipeline_runtime-linux` 是否带 debug info
- 建立 `ssh -L` tunnel
- 用已核实存在的断点集启动 cross-gdb
- 默认在断点集后 `continue`

如果只想停在初始现场而不继续，让：

```bash
PRT_GDB_CONTINUE=0 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh \
  <run-host-private-ip> <guest-ip-or-endpoint> [local-port]
```

进入 gdb 后：

```gdb
set pagination off
set confirm off
target remote :32345
info threads
thread apply all bt
info registers
x/16i $pc
disassemble /m $pc-32, $pc+32
```

如果要继续跑到下一个可疑点：

```gdb
continue
```

如果要直接停在某个函数：

```gdb
break prt_runtime_run
break prt_action_bind_topology
break stage_prepare_exec_views
break prt_dma_submit
break prt_dma_wait
break dma_blocking_wait
break prt_gemmini_spm_xlate_program
break prt_gemmini_spm_xlate_flush
break prt_rr_release_scope
break prt_gemm_conv_run
break prt_gemm_fence
continue
```

说明：上面函数名对应当前 `2026-05-05` 源码。不要沿用旧文档里的
`prt_schedule_action_submit`、`prt_dma_submit_tokenized`、
`prt_rr_release_and_fence` 作为首轮断点；这些名字不再是当前主线可直接
break 的符号。

首轮 `cfg32_nic` 推荐最小断点集：

```gdb
break prt_runtime_run
break prt_action_bind_topology
break stage_prepare_exec_views
break dma_blocking_wait
break prt_gemmini_spm_xlate_program
break prt_gemmini_spm_xlate_flush
continue
```

2026-05-05 符号复核结果：当前
`build/rerocc-linux-tests/rerocc_pipeline_runtime-linux` ELF 中可按名字 break 的首轮符号包括
`prt_runtime_run`、`prt_action_bind_topology`、`stage_prepare_exec_views`、
`prt_dma_submit`、`prt_dma_wait`、`dma_blocking_wait`、
`prt_gemmini_spm_xlate_program`、`prt_gemmini_spm_xlate_flush`、
`prt_rr_release_scope`、`prt_gemm_conv_run` 和 `prt_gemm_fence`。
`hw_dma_fence` 当前是 `prt_dma.c` 中的 `static inline`，ELF 没有稳定函数符号；
首轮不要用 `break hw_dma_fence` 消耗 `gdbserver --once` 会话。若需要贴近
`hw_dma_fence()` 调用点，先停在 `dma_blocking_wait`，再在 GDB 中用源码行或
`next`/`stepi` 进入调用附近；当前源码中的调用点在 `pipeline-runtime/src/prt_dma.c`
约 `3481` 行。

2026-05-05 18:55 UTC 后的 Makefile 默认会给 host 和 RISC-V
`pipeline-runtime` binary 加 `-g3 -fno-omit-frame-pointer`，同时保留 `-O2`。
重建后的 `rerocc_pipeline_runtime-linux` 已确认包含 `.debug_info` 和 `.debug_line`。
因此新镜像重新 `image-closure`/`infrasetup` 后，remote GDB 应能使用源码行、类型信息和更多变量；
但由于仍是 `-O2`，部分局部变量可能显示为 optimized out。

如果停在 DMA wait，优先打印：

```gdb
p *tok
p tok->debug_src_addr
p tok->debug_dst_addr
p tok->debug_bytes
p tok->debug_done_flag_pa
p tok->hw_done_flag
p tok->rr_manager_id
p tok->rr_cfg_id
```

如果停在 SPM xlate，优先看 `manager_id`、`ptbr_pa`、`pte_count`、
`range_base`、`range_size`，并确认是否在 `prt_rr_release_scope` 或
`rr_fence(scope->cfg_id)` 附近等待。

控制语义：

- `continue` 会让 target 程序继续跑；如果程序开始长时间等待，host gdb 看起来会像“卡住”，这是正常的
- 在 gdb 里按 `Ctrl-C` 会向 `gdbserver` 发 interrupt，通常可以重新拿回 prompt，然后执行 `info threads` / `thread apply all bt`
- `Ctrl-C` 抢回 prompt 后，如果只是想观察现场再让程序继续跑，优先用
  `signal 0` 或确认不传递 `SIGINT` 后再 `continue`；不要在信号停住状态下直接
  `detach` 并假设 workload 一定自然 PASS
- `detach` 会让 inferior 脱离 gdb 继续跑；如果 guest 脚本还在等程序退出，runworkload 可能继续占着 runfarm
- `kill` 会终止 inferior；这适合验证调试链路，不适合让 workload 自然 PASS
- 如果希望 runworkload 自然收尾，应在 gdb 中 `continue` 到程序正常退出，并观察 guest wrapper 是否继续执行到 `poweroff`

### 7.3 single-core NIC smoke 实测命令

旧 single-core NIC AGFI：

```text
agfi-019e0b22099a5214b
```

已实测通过的最小链路：

```bash
export ROCKET_SINGLECORE_NIC_BUILD_NAME=firesim_rocket_singlecore_nic_notrace_30mhz
export ROCKET_SINGLECORE_NIC_RUNTIME_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_relaxed_noprint_preserve_30mhz.yaml
export ROCKET_SINGLECORE_NIC_BUILD_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_30mhz.yaml
export ROCKET_SINGLECORE_NIC_BUILD_RECIPES_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml
export ROCKET_SINGLECORE_NIC_HWDB_CFG=/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml

generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh infrasetup
RUN_HOST_PRIVATE_IP="$(generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh current-private-ip)"
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/rocket_singlecore_nic_gdbserver_smoke_workflow.sh run "${RUN_HOST_PRIVATE_IP}"

ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@"${RUN_HOST_PRIVATE_IP}" \
  'grep -n "\[gdbserver\]" /home/ubuntu/sim_slot_0/uartlog | tail -n 20'

ssh -f -N \
  -L 32345:172.16.0.2:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  ubuntu@<run-host-private-ip>

/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke
```

进入 gdb 后：

```gdb
set pagination off
set confirm off
target remote :32345
break main
continue
bt
info registers pc sp ra a0 a1
x/16i $pc
```

注意：上面 SSH tunnel 只能在 UART 已经出现 `[gdbserver] phase=listening`
之后建立并交给 gdb 使用。若只看到 `phase=prelaunch`，继续等；如果旧
AGFI 没有入包而停在 early boot，可临时从 run host 侧低频 ping
`172.16.0.2` 推进旧 RX 路径，但仍要等 `phase=listening` 后再 attach。
旧 AGFI 上 console 输出可能严重滞后；如果已经看到 `Starting network: OK`、
run host 能稳定 ping 通 guest，且 `prelaunch` 后长时间没有新 announcement，
唯一允许的端口探测方式仍然是“一次真实 gdb attach”。不要改用 `nc`/`telnet`，
因为它们会消费 `gdbserver --once` 的唯一连接，而且拿不到调试状态。

2026-05-01 在 `agfi-0079cbbca617eca4e` + `+simplenic-empty-switch-poll-interval=1024`
上重新实测，结论更进一步：

- `target remote :32345` 成功
- 初始停在 `_start`
- `break main` 成功
- `continue` 后命中 `main()`
- `bt` 返回 `#0 main ()`
- `info registers` 能读出 `pc/sp/ra/a0/a1`
- `x/12i $pc` 能读出当前指令窗口
- `print smoke_counter` 能读出 guest 全局变量
- run host 到 guest `172.16.0.2` 的 `ping` 成功，ARP 解析为
  `00:12:6d:00:00:02`

这说明 remote gdbserver 链路已经能执行常规源码级调试动作：attach、断点、
运行到断点、调用栈、寄存器、指令窗口和变量读取。它还没有证明“任意断开后可
重连”，因为当前 guest runner 使用 `gdbserver --once`。

2026-05-02 复测 old AGFI 时补充了一条必须固定的约束：

```text
+cpu-managed-stream-debug=0
```

原因是同一颗 `agfi-0079cbbca617eca4e` 如果配到会默认打印
`CPU_STREAM DEBUG` 的 driver，guest 仍能进入 Linux / IceNet / `gdbserver`，
但 `target remote` 初始 RSP 握手可能在 240s 内拿不到 gdb prompt。`gdbserver.log`
会显示远端已经收到来自 `172.16.0.1` 的调试连接，随后本地 expect 超时断开，
`gdbserver --once` 杀掉 inferior。这不是“NIC 完全不通”，而是回归路径相对
05-01 成功状态多了高扰动 console 输出。

复测通过的最小判据：

- UART 没有 `CPU_STREAM DEBUG` 刷屏；
- UART 到达 `[gdbserver] phase=listening`；
- 第一次 TCP 连接必须是真实 `gdb target remote`；
- GDB 能停在 `_start`，并用软件断点命中 `main`、worker heartbeat、
  `smoke_iteration_hook`；
- `info threads` 和 `thread apply all bt` 能返回 5 个线程；
- guest 最后出现 `[gdbserver] phase=exit`、`Simulation complete`、`PASSED`。

自动回归脚本：

```bash
GDBSERVER_SMOKE_EXPECT_TIMEOUT=240 \
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_remote_gdbserver_software_expect_smoke.sh \
  <run-host-private-ip> 32345
```

默认不注入静态 ARP / host ARP；只有遇到明确的 host-to-target 邻居问题时，再显式设置：

```bash
export GDBSERVER_SMOKE_STATIC_NEIGH_MAC=00:12:6d:00:00:02
export GDBSERVER_SMOKE_ADVERTISE_HOST_ARP=1
```

### 7.4 不要把 prelaunch 当成 listening

`[gdbserver] phase=prelaunch` 只说明 guest runner 已经算出了 endpoint，
还没有证明 `gdbserver` 已经完整进入 RSP 监听状态。

2026-04-30 的旧 AGFI 实测中，在只有 `phase=prelaunch`、还没有
`phase=listening` 时强行执行 `target remote :32345`，TCP/RSP 已能到达 target，
但 gdb 报：

```text
warning: unrecognized item "timeout" in "qSupported" response
Remote replied unexpectedly to 'vMustReplyEmpty': timeout
```

所以 SOP 里应按这个顺序操作：

- 优先等 UART 出现 `[gdbserver] phase=listening`
- 如果旧 AGFI 一直停在早期 boot 或 `Starting network:`，可以临时从 run host 低频执行
  `ping 172.16.0.2` 注入 host-to-target 包，推进旧 NIC RX 路径
- 如果当前 AGFI 已经出现 `phase=prelaunch`，并且 run host 能 `ping 172.16.0.2`，
  但 UART 长时间没有 `phase=listening`，允许用**一次真实 gdb attach**验证；
  2026-05-01 实测中这种状态下 `target remote` 仍然可以成功断到 `_start/main`。
- 不要用 `nc` / `telnet` / 端口扫描在 `phase=prelaunch` 时“试一下端口”；
  它们可能消费 `gdbserver --once` 的唯一连接，却不给出任何调试状态。

### 7.5 `gdbserver --once` 的一次连接限制

当前 runner 用的是：

```bash
gdbserver --once 0.0.0.0:2345 <target-program>
```

含义是：第一个 TCP 连接就是唯一会话。实测中，如果第一次 gdb 会话被
`timeout` 从外部杀掉，或者本地 gdb 在 target running 状态下异常退出，
后续再执行：

```gdb
target remote :32345
```

会得到类似结果：

```text
Remote communication error. Target disconnected: Connection reset by peer.
```

这不是 NIC 链路重新坏了，而是那次 `--once` 监听已经被消费。处理方式：

- 想继续调试同一个 guest 程序：不要让本地 gdb 被 `timeout` 或外部信号杀掉；
  在 gdb 内用 `Ctrl-C` / `interrupt` 拿回 prompt，然后 `bt` / `info registers`
- 想获得一条全新的 gdbserver 连接：重新跑 workload，让 guest 重新启动 `gdbserver --once`
- 若后续需要“断开后还能反复连”，应把 workload-local runner 改成可配置模式，
  例如对 smoke 负载去掉 `--once`，或另做 `gdbserver --multi` / `extended-remote` 流程

### 7.6 Batch gdb 的安全写法

不要在 batch 命令里机械写：

```bash
-ex 'continue' -ex 'quit'
```

原因：如果 target 仍在 running，`quit` 可能报：

```text
Cannot execute this command while the target is running.
Use the "interrupt" command to stop the target and then try again.
```

更稳的 smoke 方式是只验证 attach、断点、栈和寄存器，然后显式 detach：

```bash
timeout 180 /home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex 'target remote :32345' \
  -ex 'break main' \
  -ex 'continue' \
  -ex 'bt' \
  -ex 'info registers pc sp ra a0 a1' \
  -ex 'detach' \
  -ex 'quit'
```

如果目标是让 workload 自然 PASS，不要在断点处 `detach` 后马上结束本地操作；
应交互式 `continue` 到 inferior 正常退出，并从 UART 看到 guest runner 进入
`[gdbserver] phase=exit` 和 `poweroff`。

## 8. 推荐 first-response 命令

当 run 看起来“卡住”时，第一批命令建议固定成：

```gdb
interrupt
info threads
thread apply all bt
thread 1
info registers
x/16i $pc
```

如果某个线程停在 syscall / futex / poll，
说明不是用户程序 hot loop 卡住；
如果停在 runtime 某个自旋或等待点，
就能直接看到对应函数和指令地址。

失败现场处理原则：

- `runworkload` 失败或长时间不返回时，先不要立刻 `terminaterunfarm`
- 先从 manager log、run host live 文件和 guest debug 文件抓现场：

```bash
ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@<run-host-private-ip> \
  'ls -lah /home/ubuntu/sim_slot_0 &&
   tail -200 /home/ubuntu/sim_slot_0/uartlog &&
   tail -50 /home/ubuntu/sim_slot_0/heartbeat.csv 2>/dev/null || true'
```

- 如果 manager 没有自动 copy-back，就手动从 run host 拷回
  `/home/ubuntu/sim_slot_0/uartlog`、`heartbeat.csv`、driver/switch logs 和 workload 输出
- 只有在确认现场已经搬回本地后，再执行 `firesim terminaterunfarm --forceterminate`
- `terminaterunfarm` 之后必须立刻用 `aws ec2 describe-instances` 查所有相关
  `f2.*` 实例；若仍有 `running`，直接 `aws ec2 terminate-instances` 手工回收，
  并再次确认不再处于 `running`

### 8.1 single-core smoke 现场打包

旧 single-core AGFI 调 gdbserver 时，建议每轮结束前固定搬回一份现场，
避免 manager 没有自动 copy-back 时丢失失败证据：

```bash
SCENE_DIR=/home/ubuntu/chipyard/tmp/firesim-aws-f2/nic-scenes/remote-gdb-$(date -u +%Y%m%d-%H%M%S)
mkdir -p "${SCENE_DIR}"

ssh -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no ubuntu@"${RUN_HOST_PRIVATE_IP}" \
  'tar czf /tmp/sim-slot-0-gdbserver-scene.tgz \
     -C /home/ubuntu/sim_slot_0 \
     uartlog heartbeat.csv 2>/dev/null || true;
   tar czf /tmp/switch-slot-0-gdbserver-scene.tgz \
     -C /home/ubuntu/switch_slot_0 . 2>/dev/null || true'

scp -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no \
  ubuntu@"${RUN_HOST_PRIVATE_IP}":/tmp/sim-slot-0-gdbserver-scene.tgz "${SCENE_DIR}/" || true
scp -i /home/ubuntu/firesim.pem -o StrictHostKeyChecking=no \
  ubuntu@"${RUN_HOST_PRIVATE_IP}":/tmp/switch-slot-0-gdbserver-scene.tgz "${SCENE_DIR}/" || true

cp /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/rocket-singlecore-nic-gdbserver-*.pane.log \
  "${SCENE_DIR}/" 2>/dev/null || true
```

若这轮 gdb 会话已经成功 attach，另外把本地 gdb transcript 放进同一个
`SCENE_DIR`。之后再 terminate runfarm，并确认 EC2 进入
`shutting-down` 或 `terminated`。

## 9. 何时可以替代 TraceV

满足以下任一条件，就可以优先用 `gdb`，不必再开 `TraceV`：

- 已经拿到明确的 `PC`，能定位到具体 `rerocc_pipeline_runtime-linux` 指令窗口
- 已经通过 `thread apply all bt` 看清线程间谁在跑、谁在等
- 已经确认 stall 发生在 Linux 用户态，而不是 RTL / bridge / boot 路径

只有当怀疑点回到：

- `custom` 指令与硬件握手
- RoCC/bridge/RTL 时序
- guest user-space 已经看不到更深的因果

才回退到 `TraceV`

## 10. 失败分流

### 10.1 image-closure 失败

优先检查：

- `linux_gdbserver.kfrag` 是否被 workload JSON 正确引用
- Buildroot 是否因为 `gdb` 依赖不满足而拒绝配置

### 10.2 guest 无 gdbserver announcement

优先检查：

- `local-freshness` 是否确认 `/usr/bin/gdbserver` 在 image 里
- `/firemarshal.env` 是否带上 `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`
- wrapper / runner 是否已经是最新镜像内容

### 10.3 隧道已通但 `target remote` 连不上

优先检查：

- UART announcement 里的 `guest_ipv4`
- SSH 隧道右侧是否写成了 guest IP，而不是 run host private IP
- `gdbserver.log` 是否显示正在监听对应端口
- 是否曾经用 `nc` / `telnet` 连过 `gdbserver --once` 的端口；
  如果连过，重启 guest 侧 `gdbserver` 或重跑 workload，不要继续复用那次监听

### 10.4 `tap0` 已 `LOWER_UP` 但 `guest_ipv4=unknown`

优先检查：

- `/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info`
  是否已经写出 `net_dev=`
- `S40network` 是否停在 `Starting network:`，
  或者已回到 `Starting network: OK`
- guest 侧接口筛选是否误选了虚拟接口；
  当前应只接受：
  - `/sys/class/net/<if>/device` 存在
  - `type=1`
  - MAC 非 `00:00:00:00:00:00`
- 若 `net_dev` 仍为空，
  下一轮应优先把 `/sys/class/net` 枚举结果直接导出到 guest 文件，
  而不是继续只试图 host 侧连端口

### 10.5 已连上但没符号

优先检查：

- host gdb 打开的 binary 是否是
  `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- 是否误用了 image 内 stripped binary

## 11. 当前结论

- `gdbserver` 路径仍然是当前 `pipeline-runtime` 主线最直接的 user-space 调试办法：
  它能把 inferior 冻结在 attach 前，并能直接读 PC、寄存器、线程和调用栈
- single-core NIC smoke 已经证明 remote `gdbserver` 链路本身可用；
  之前的 target-to-host payload corruption 已由 IceNet driver 的 Linux DMA API 修复解释并实测消失
- `pairdummy` 主线过去的关键误区是复用了不含 IceNIC 的旧 AGFI；
  这不是 `gdbserver` 方法本身失败，而是 target 没有 guest 可见 NIC
- 2026-05-05 已启动的 `12p4c128sbus32cfg + optimized DMA + current NIC`
  主线构建 `pairdummy-cfg32-nic-mainline-20260505T132956Z` 已失败于 Vivado
  detail placement。该轮 freshness 通过、确实含 `ice-nic@10016000`、
  `SimpleNICBridgeModule` 和 optimized DMA marker，但 post-synth 总 LUT 约
  `96.96%`，最终 CL pblock 可用 CLB 不足；没有 AGFI/AFI，不能用于 HWDB 更新或
  gdbserver 验收。
- 2026-05-05 并行启动的 no-TraceIO 资源削减构建
  `pairdummy-cfg32-nic-notrace-20260505T171453Z` 是当前唯一活动 cfg32 NIC
  AGFI 候选。它保留 NIC、1BP、cfg32 和 optimized DMA marker，去掉 TraceIO，
  post-synth 总 LUT 约 `89.67%`；截至 `2026-05-06 00:08 UTC`，它已通过
  Vivado detail placement 并进入 Phase 4 post-placement optimization，但还没有
  产生 AGFI/AFI。
- 下一步等新 AGFI 完成并更新 cfg32 NIC HWDB 后，优先验证：
  1. guest `dmesg` 中 IceNet driver 是否加载，并打印 `IceNet DMA API mappings enabled`
  2. `S40network` 是否 `OK`
  3. `[gdbserver]` announcement 是否给出非空 `guest_ipv4`
  4. SSH tunnel + `target remote :32345` 是否能断到 `rerocc_pipeline_runtime-linux`
  5. 若 workload 不返回，先保留 runfarm 并手动搬回完整现场，再终止远端实例

## 12. 2026-05-07 cfg32 NIC no-TraceIO 复跑结论

`agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690` 的
`dummy8x8 4c12p12 sbus64 cfg32 NIC noTrace` bitstream 已经在 remote
`gdbserver` cfg32 triage 中通过。关键条件是 runtime 必须保留旧 1BP 成功链路的
SimpleNIC plusargs：

```text
+simplenic-relaxed-required-bytes=1
+simplenic-empty-switch-poll-interval=1024
+simplenic-token-debug=0
+cpu-managed-stream-debug=0
+heartbeat-polling-interval=100000000
```

对应 runtime：

`sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`

建议 cfg32 triage 命令固定带静态邻居，避免 ARP 分支掩盖 RSP 结果：

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

这个静态邻居动作不会连接 `2345`，不会消耗 `gdbserver --once` 的唯一客户端。
仍然禁止使用 `nc`、telnet 或其它端口探测作为第一 TCP 客户端。

本次通过的记录：

`pipeline-runtime/debug_records/20260507T055420Z_dummy8x8_sbus64_gdbserver_plusargs_pass.md`

本次 cfg32 triage 覆盖了 `target remote`、多软件断点、`continue`、线程列表、
全线程 backtrace、寄存器、反汇编、Ctrl-C 抢回控制和 `detach`。它还没有覆盖旧
single-core smoke 中的 `next` 以及变量 / 内存写读；下一步要扩展 cfg32 expect
脚本后用同一 AGFI 再跑一次 gdbserver-only 验收。
