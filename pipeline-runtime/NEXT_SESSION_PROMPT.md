# Next Session Prompt

## 2026-05-09 no-DMA PASS and current performance experiment

当前接手 `pipeline-runtime` 时，顶部结论优先于下面旧线程：

- `cfg32/NIC/noTrace + no-DMA compute` 已完整 PASS。
- AGFI：`agfi-077451484fe3b63c3`
- 运行结果：`Simulation complete` / `*** PASSED *** after 22734035102 cycles`
- checkpoint commit：`9742924 Record cfg32 no-DMA compute pass`
- 这已经排除 no-DMA 路线下的 artifact 读取、Gemmini compute、SPM xlate 和普通
  pipebuf 控制流作为当前 blocker。
- 真实正确性主线后续应回到 DMA submit/completion、`hw_dma_fence()` / blocking wait、
  host buffer/direct DMA，以及依赖真实 DMA 完成的 producer publish。
- `doneflag` 不能作为 DMA completion 证据。

当前临时任务：

1. 暂时保持 no-DMA，比较 HybridMapper 两类编排方法的执行模型时间。
2. 用户口径 `ours` 对应当前 artifact 方法名 `ours2`；用户口径 `gemmini` 对应当前
   HybridMapper/runtime artifact 方法名 `gemini2`。
3. 本地已生成/审计 `ours2` 和 `gemini2` 的 dummy8x8/sbus64/cfg32 runtime artifacts；
   两套 pipeline YAML 与当前 overlay 哈希一致，不需要覆盖 overlay。
4. 计时口径必须排除预处理：不包含 HybridMapper 生成、YAML/artifact 读取校验、
   model/input/golden load；优先使用 runtime 内模型执行窗口的 trace 字段。
5. runtime trace 已有 `model_exec_ns`、`model_compute_ns`、`preprocess_ns`、
   `postprocess_ns`；本地 CPU/no-DMA dry-run 已确认字段可用。
6. 新增 perf workflow：
   `scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh`
   ，profile 展开为 `METHODS="ours2 gemini2"`、`TRACE_ENABLE=1`、
   `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`、`gdbserver=0`。
7. 减少 F2 使用：下一步先 `image-closure`，然后同一 F2 run farm 连续跑两种
   `METHODS`，完成后解析 trace、copy-back 并 terminate。

详细记录：
[`debug_records/20260509T193535Z_no_dma_compute_full_pass_cfg32_gdbserver.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260509T193535Z_no_dma_compute_full_pass_cfg32_gdbserver.md)
[`debug_records/20260510T053309Z_no_dma_perf_ours2_gemini2_prep.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260510T053309Z_no_dma_perf_ours2_gemini2_prep.md)

## 2026-04-27 Direct-Only cfg32_nic Local-GDB Thread

当前接手 `pipeline-runtime` 时，优先执行这条最新线程：

- 不构建新 bitstream。
- 使用现有带 NIC、带 DMA 不对齐搬运优化的 cfg32_nic AGFI：
  `agfi-02e18c6f7a7a95096`。
- 不检查、不打断、不清理并行运行中的另一个 `buildbitstream` 任务。
- `pipeline-runtime` 必须走 direct 软件路线：
  - `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1`
  - `PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=0`
- 使用 wrapper：
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_local_gdb_cfg32_nic_workflow.sh`

当前已完成：

- README / local-GDB SOP / workflow / hwdb 已读。
- `pairdummy-sbus128-fixed-v25` profile 已建立。
- cfg32_nic local-GDB workload、runtime config、wrapper 已建立。
- `show` / `debug-preflight` 已通过，env render 已确认 direct-only 和 local-GDB 变量。
- `image-closure`、`launch`、`infrasetup`、`remote-freshness` 已通过。
- runworkload 已执行并 terminate 本轮 run farm。

本轮结果：

- runworkload session:
  `pairdummy-sbus128-local-gdb-cfg32-nic-runworkload-20260427-054907`
- results:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-27--05-49-08-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-local-gdb-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync-local-gdb-cfg32-nic/`
- `uartlog`:
  `ERR MISMATCH! on writing tokens in. actually wrote in 0 bytes, wanted 58560 bytes.`
- guest 没进入 Linux，local-GDB 文件不存在。
- instance `i-0881a2d8250b28d6f` 已 terminate，最后确认到 `shutting-down`。

当前结论：

- 这是 boot 前 SimpleNIC / host bridge blocker，不是 `pipeline-runtime` 用户态卡点。
- 当前源码已有 `push() == 0` retry/debug 逻辑，但 hwdb 部署的 cfg32_nic
  `driver_tar` 里的 `FireSim-f2` 不包含这些新字符串，说明 driver bundle 仍旧。

下一步：

1. 不重复跑同一个旧 `driver_tar`。
2. 不构建新 bitstream，不干扰并行 buildbitstream。
3. 为同一 `agfi-02e18c6f7a7a95096` 找到或生成包含当前 SimpleNIC host-side 修改的
   cfg32_nic driver bundle。
4. 用临时 hwdb 指向该 driver bundle 后，再跑
   `infrasetup -> remote-freshness -> run`。
5. 只有 Linux/local-GDB 起得来后，才继续定位 `pipeline-runtime` direct DMA 卡点。

详细记录：
[`debug_records/20260427T053456Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260427T053456Z.md)

## 2026-04-20 Mainline Override

这条会话提示顶部内容优先于下面的旧摘要；当前活跃调试线程仍是：

- `pipeline-runtime` 主线 `g6 bertmini`
- FireSim FPGA artifact / capture / 源码静态复盘
- 当前已结束最新一轮 run，下一轮 rerun 需用户再次同意

当前硬约束：

- 新的 `infrasetup` / `runworkload` 前，必须先向用户汇报：
  - 当前结论
  - 计划修改
  - 预期判据
  并等待用户同意。
- 不清理环境。
- 继续坚持：
  - 先静态读代码
  - 先读现有 capture / manager / watchdog artifact
  - 最后才决定是否需要新的单变量 rerun
- 不要再把单一 `guest_sparse` 尾部当成 authoritative frontier。

### 2026-04-20 15:42 UTC Static-Audit Correction

- 之前顶部把这轮 fresh rerun 直接写成：
  **`page28 after_accounting` 之后、`page29 before_v2p` 之前**
  ，这句需要降级。
- 当前安全结论应改成：
  - 最后一个**稳定偶数** breadcrumb 仍是
    `page28 dma_page_after_accounting`
  - 但 `page29` 证据本身是模糊的，因为：
    - `slot14` 在两份 live capture 里都是同一个 odd torn state
    - 该 odd raw slot 是
      `page=19 tok=1176 phase=dma_submitwait_after_cleanup`
    - `page29/token0` 的 page-level breadcrumb 也恰好哈希到 `slot14`
    - breadcrumb slot key 不含 `phase`，也不含 alias `target_seq`
- 所以不要再把
  “当前 populated ring 中没有稳定的 `before_v2p`”
  直接写成
  “控制流一定还没到 `page29`”
- 同时，静态代码审计表明：
  `page29 before_v2p` 之前的 direct-path 只剩：
  - 地址/长度重算
  - `dma_trigger_export_host("pset", ...)`
  - `dma_chunk_needs_bounce(...)`
  而当前 run config 下：
  - `debug_trigger_enable=0`
  - `page29` 不是 sparse page probe
  - `page29` 不是默认 chunk marker
  - `src=0x40006000`
    `dst=0x103b31800`
    `chunk=0x400`
    静态上应 direct，不应 bounce
- 这意味着：
  如果执行真的停在 `before_v2p(page29)` 之前，
  它会停在一段几乎没有正常阻塞 helper 的极窄走廊里；
  当前静态上并不好解释。

当前 authoritative 结论：

- 最新一轮 `g6 bertmini` FPGA run
  已完成到人工 terminate 收尾。
- runworkload session：
  `pairdummy-sbus128-runworkload-20260420-141608`
- 远端实例：
  `i-01d398695bdc22fd6`
  / `192.168.1.67`
- live snapshots：
  - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T143126Z-g6-live-capture`
  - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T144843Z-g6-live-capture`
- freshness 已通过；
  这是带
  `dma_page_before_v2p / dma_page_after_v2p`
  probes 的第一轮有效 fresh rerun
- `host-watchdog` 误杀已再次真实排除：
  - `2026-04-20 14:22:57 UTC`
    armed 后，
    `guest_sparse`
    从
    `1011`
    推进到
    `323227`
  - heartbeat 同时从
    `425`
    推进到
    `1912`
  - plateau 后 watchdog 仍记录：
    - `idle=625s hb_idle=0s`
    - `idle=844s hb_idle=0s`
    - `idle=1063s hb_idle=0s`
  - host 没有提前 terminate；
    本轮停机是人工触发
    `terminaterunfarm`
- 因而当前主线 blocker
  仍然是 guest 内部真实 stall，
  不是 watchdog
- 这轮 guest 已明确跑进
  `rerocc_pipeline_runtime-linux`
  - `status` 里仍是
    `state=running`
  - runtime binary pid：
    `196`
- 当前最新 run 的局部 authoritative frontier
  是：
  **`segment=0 stage=0 subbatch=3 tensor=2` export page loop，
  最后稳定 breadcrumb 在 `page=28 dma_page_after_accounting`。**
- 直接证据：
  - breadcrumb decode：
    - `kind=dma`
    - `phase=dma_page_after_accounting`
    - `seg=0 gstage=0 lstage=0 sb=3 tensor=2 page=28`
    - `src=0x40005c00 dst=0x103b31400 aux0=0x400 line=1000`
  - decode 文件：
    - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T143126Z-g6-live-capture/breadcrumb.decode.txt`
    - `/home/ubuntu/chipyard/tmp/pipeline-runtime-live/20260420T144843Z-g6-live-capture/breadcrumb.decode.txt`
  - 这次 fresh binary 已经带上
    [`change_records/20260420T121216Z.md`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260420T121216Z.md)
    中加入的 export `v2p` probes
  - 但这里不要再直接下结论说
    “一定早于 `page29 before_v2p`”
  - 因为 `slot14` 是 torn/collision slot，
    当前 breadcrumb 对 `page29` 本身不够可信
  - 所以当前更安全的写法是：
    **最后稳定 even breadcrumb 在 `page28 after_accounting`；
    `page29` 的 breadcrumb 证据是模糊的**
- `trigger.log` 仍为 `0`，
  但这轮
  `debug_trigger_enable=0`
  ，所以它是非诊断性结果
- 当前 g6 mapping 已对齐到：
  - `segment0`
  - `globalStageId=0`
  - `layerIdList=[0]`
  - `exportTensorIdList=[2]`
  - `vAccIdxList=[[0,1,2,3]]`
  - RR cfg：
    - DMA / opcode2 -> `cfg0`
    - Gemmini / opcode3 -> `cfg1`
- 重要：
  不要把这轮
  `page28 after_accounting`
  误写成“全局上覆盖旧 run”的 frontier。
  `pipeline-runtime` 的 segment/action 在
  [`src/prt_runtime.c`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c#L4864)
  到
  [`src/prt_runtime.c`](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c#L5111)
  是串行推进的，
  所以 older run 的
  `segment=3 stage=2 compute-done`
  明确比这轮
  `segment=0 ... page28`
  更靠后。
  正确表述是：
  新 breadcrumb 只锚定了**这轮 run 的局部冻结点**，
  而“日志/探针改变了行为或时序”仍然是活跃假设。
- 对这轮局部 frontier，
  静态嫌疑顺序已改成：
  1. 先承认当前 `page29` breadcrumb 证据有 torn/collision 歧义
  2. `page29` 迭代开头的地址/长度重算
  3. `dma_trigger_export_host("pset", ...)`
  4. `dma_chunk_needs_bounce(...)`
  5. 只有在真正证明控制流已到
     `before_v2p`
     之后，
     才重新追
     `prt_host_virt_to_phys(dst_ptr, &dst_pa)`
     / `pread(/proc/self/pagemap)`
- 当前可先降级的旧嫌疑：
  - page28 前一页的
    `hw_dma_fence()`
    /
    `rr_fence(cfg0)`
    /
    `release`
    没退干净
  - 直接把 stall 主因归到
    `v2p`
    内部
  - bounce path
    `src_mod64 == dst_mod64 == 0`
    不应触发
- `g6` artifact 仍已确认是：
  - `num_gemmini=6`
  - `num_dma=6`
- `cfg15`
  预算风险结论不变：
  当前 g6 不是它的直接受害者；
  它仍只是 future artifact risk
- 当前最合理的下一步顺序：
  1. 先静态审计
     `segment0 stage0 tensor2`
     的
     `page28 after_accounting -> page29 before_v2p`
     更窄走廊
  2. 若做定向 probe，
     下一轮优先设计一个
     **不复用 `slot14` 等价类**
     的低扰动判别点，
     用来区分：
     - 真没到 `page29 direct-path`
     - 还是到了，但 breadcrumb 不可信
  3. 若需要 rerun，
     继续保持
     `seg0/stage0`
     focus，
     不要再切回旧的 stage2 叙事
  4. 只有在新证据表明控制流已进入
     `before_v2p`
     之后，
     再继续追
     `iv2p-b / iv2p-pb / iv2p-pe / iv2p-ok`
  5. 在用户同意前，
     不启动新的
     `launchrunfarm / infrasetup / runworkload`

接手 `pipeline-runtime` 时，先按这个顺序阅读：

1. `/home/ubuntu/chipyard/AGENTS.md`
2. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/README.md`
3. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/project_guide.md`
4. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/CURRENT_STATUS.md`
5. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/constraints/hard_constraints.md`
6. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/DECISIONS.md`
7. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/architecture/runtime_mechanisms.md`
8. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/architecture/alignment_constraints.md`
9. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/plans/runtime_alignment_plan.md`
10. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/plans/hybridmapper_alignment_plan.md`
11. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/plans/blocker_debug_sop_plan.md`
12. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/architecture/g6_stage2_post_compute_dma_rr_static_audit_20260420.md`
13. `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/architecture/g6_segment0_tensor2_export_page28_page29_static_audit_20260420.md`

固定入口：

- profile：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_g6_fixed_env.sh`
- workflow：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_g6_workflow.sh`
- runbook：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/workflows/pairdummy_sbus128.md`

硬约束：

- SSH 和 live 检查一律使用私网 IP。
- FireMarshal 只走 `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`。
- FireSim manager 只走 `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`。
- 任何编译、FireMarshal、FireSim 之前，先：
  `cd /home/ubuntu/chipyard/sims/firesim`
  然后：
  `set +u; source sourceme-manager.sh --skip-ssh-setup; source /home/ubuntu/chipyard/env.sh; set -u`
- `infrasetup` 和 `runworkload` 前必须完成 local / remote freshness。
- 主观测面是 guest 文件系统日志，不是 `uartlog`。
- Linux boot 早期只要没有明确 boot error / panic / crash，
  且 `heartbeat.csv` 还在前进，
  就继续等；
  不要只凭 `uartlog` 静默把它记成新的异常或 blocker。
- 遇到卡点/报错，先：
  1. 跑 artifact 静态审计
  2. 静态读对应代码路径
  3. 跑 capture 分诊
  4. 必要时用
     `triage_prt_capture.py --emit-trigger-env`
     生成 trigger overlay
  5. 最后才做单变量 rerun
- 对本条 `TraceV local metasim` 线程，
  以上旧限制已失效；
  当前只走本机 metasim，
  不开新 FPGA/CPU 机器。
- 每一轮调试都写 `debug_records/<timestamp>.md`。
- 每一轮实际修改都写 `change_records/<timestamp>.md`。

静态 / 分诊常用命令：

- `python3 /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pipeline_runtime_artifact.py --pipeline-yaml <pipeline.yaml> --hardware-yaml <hardware_target.yaml> --model-yaml <model.layers.yaml> --expect-target-key <target_key>`
- `python3 /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/triage_prt_capture.py <capture-dir-or-file>`
- `python3 /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/triage_prt_capture.py <capture-dir-or-file> --emit-trigger-env`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh debug-preflight`

当前主线摘要：

- 当前 fixed profile 已升级到：
  `pairdummy-sbus128-fixed-v20`
  - trigger-gated 短日志默认关闭
  - 仅允许对
    `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
    做受控 overlay
- `firesim-tmux-run.sh`
  现已显式透传
  `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
  到 detached `tmux` session；
  以后若按 SOP 做 trigger overlay，
  不会在 wrapper 边界被静默丢失
- 当前 trigger 语义已做关键修正：
  命中前若事件不在目标维度内，
  `prt_trigger_log_note()`
  直接返回，
  不再继续做
  `format_line + ring_push`
  这一步是为了落实新的 SOP：
  靠近卡点再开日志，
  不再让 page0..page23 这类前置路径为 page24 trigger 制造热路径扰动
- 最新一轮 `v20` control rerun
  已完成并落盘官方 capture：
  - host：
    `192.168.1.147`
  - capture prefix：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-runworkload-20260414-162449-192.168.1.147-host-watchdog-20260414T164512Z`
  - manager 最终退出原因是基础设施侧
    `Error reading SSH protocol banner`
    不是 guest panic；
    对应 EC2
    `i-0cfb64404d409fed9`
    已确认
    `terminated`
- 这轮最重要的有效结论不是
  `page28`
  新 frontier，
  而是：
  旧的
  `sb3/page24`
  frontier
  已被穿过。
- 直接证据：
  - trigger log 只有
    `33`
    行，
    正好等于
    `match line + post_budget(32)`；
    因而最后一行
    `sb=3 page=28 ph=pset`
    只是 trigger window 截断点，
    不是 authoritative frontier
  - sparse log 已出现：
    - `c2-export ... subbatch=3 ... copy-end`
    - `worker stage=0 subbatch=3 done`
    - `worker stage=0 subbatch=4 begin`
    - `c2-export ... subbatch=4 ... copy-begin`
  - breadcrumb `--all`
    已有
    `sb=4 tensor=2 page=63 dma_submitwait_after_cleanup`
    /
    `dma_page_end`
    以及
    `sb=4 rr_release_end`
    记录
- 因而目前更可信的结论是：
  strict-trigger 修复后，
  旧的
  `sb3/page24`
  卡点已被穿过；
  但新的 blocker 尚未重新精确定义，
  当前只能确定它已经后移到
  `subbatch=4`
  的 export 后段 / cleanup
  之后或附近
- 对旧的
  `page24..28`
  窗口做静态复核后确认：
  - `pset -> v2p-b`
    之间没有新的硬件动作
  - 本轮真实地址上
    `src mod64 == dst mod64 == 0`
    所以不会走 bounce path
  - 旧窗口没有新的
    `HybridMapper / pair-manager / DMA`
    语义错位证据
- `triage_prt_capture.py`
  已再次补强：
  现在会解析
  `guest-trigger-log.txt`
  并在
  `trigger_line_count == post_budget + 1`
  时明确提示
  `trigger window exhausted`
  避免再把最后一条 trigger 误记成 frontier
- 下一轮 rerun
  不要再用
  `sb3/page24`
  trigger。
  优先改成：
  - `dma-export`
  - `subbatch=4`
  - `tensor=2`
  - `page=63`
  - 并把
    `POST_BUDGET`
    提高到
    `64`
    或
    `96`
  若这条仍不够，再退到 runtime family 只钉
  `subbatch=4`
  worker 边界。

- 不要再回到旧叙事：
  - export page0
  - RR acquire return
  - first submit/wait
  - fixed-load `tensor0 page0`
  这些都已经被更晚 live run 清掉了。
- `v15`
  把 export 路径缩到
  `page63 dma_page_end`
  之后的 release 窗口；
  `v16`
  通过新增
  `RR_RELEASE_BEGIN/END`
  breadcrumb
  明确证明了 export 侧 release
  不是 blocker。
- `v16`
  sparse log 已经推进到：
  - `worker stage=0 subbatch=4 begin`
  - `pointwise-matmul-fallback ... mgr=0..3 begin/end`
  - `conv-sync-strided ... mgr=0..3 fence-end rc=0`
- 对 pointwise split-OC 路径的静态审计结论：
  - `split_1d_range()`
    与 host 参考实现一致
  - 子 conv 只按
    `oc_beg`
    偏移
    `weights/bias/output`
  - full
    `in/weight/out stride=256`
    保持不变
  - 这与当前
    `HybridMapper + runtime`
    对齐约束一致，
    没有静态 tile 指针 bug 证据
- 如果 `mgr3 fence-end`
  是真实 frontier，
  那个窄窗口之后只剩：
  - `prt_rr_release_scope(&scope)`
  - 返回到
    `oc-split-pointwise ... end`
  - 以及下一轮
    `mgr4`
    入口

本轮最重要的新发现：

- `v17`
  新 capture 继续把前沿往前缩：
  - sparse log 文件尾稳定停在
    `conv-sync-strided stage=0 mgr=7 flushed use_pointwise=1`
  - 同时
    `heartbeat.csv`
    继续推进，
    `bertmini-batch8.log`
    文件大小固定不变
  - sharded breadcrumb 的最后有效 pointwise 事件仍在
    `mgr=6`
- 对这段新窗口做静态阅读后确认：
  在当前 fixed profile 下，
  `mgr7 flushed`
  之后到下一条 pointwise breadcrumb 之间
  没有新的硬件动作；
  第一嫌疑已转成
  pointwise 热路径里剩余 coarse guest log 自干扰

本轮已做的软件修正：

- [`src/prt_breadcrumb.c`](src/prt_breadcrumb.c)
  - breadcrumb slot 现在 hash：
    `stage/subbatch/kind/manager/tensor/page/token`
- [`scripts/decode_prt_breadcrumb.py`](scripts/decode_prt_breadcrumb.py)
  - `--all`
    输出按
    `seq`
    排序
- [`scripts/pairdummy_sbus128_fixed_env.sh`](scripts/pairdummy_sbus128_fixed_env.sh)
  - profile 升到
    `pairdummy-sbus128-fixed-v17`
  - 当前关闭
    fixed-load / export
    高频 probe，
    只保留 breadcrumb

本轮已做的软件修正：

- [`src/prt_gemmini_adapter.c`](src/prt_gemmini_adapter.c)
  - 当 breadcrumb 已启用时，
    关闭 pointwise 热路径内已被 breadcrumb 覆盖的 coarse guest log
- [`scripts/pairdummy_sbus128_fixed_env.sh`](scripts/pairdummy_sbus128_fixed_env.sh)
  - profile 升到
    `pairdummy-sbus128-fixed-v18`
  - 关闭
    `PIPELINE_RUNTIME_CRITICAL_UART_PROBE`

当前部署状态：

- local `image-closure`：
  通过
- remote freshness：
  `192.168.1.14`
  在本轮 control rerun 前通过
- runtime binary sha256：
  `36e85f2a3e357f6d2b0fc79a741120f6566effebc3c140b36e56b8be325d0a97`
- firemarshal env sha256：
  `04aa1fe0bb9e00d992924adb8c751f4fe16ed7138cd2d816c600e218edbd285e`
- remote image sha256：
  `eb8ada339a11d77f2c61268e95f3cb26c91c61fd1d976dc768e3de1efe1dc9a3`

最近一次 run：

- host：
  `192.168.1.14`
- runworkload session：
  `pairdummy-sbus128-runworkload-20260414-150549`
- pane：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260414-150549.pane.log`
- manager log：
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-14--15-05-50-runworkload-UIKVAB93PDHR9S5I.log`
- results dir：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-14--15-05-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
- official capture prefix：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-runworkload-20260414-150549-192.168.1.14-host-watchdog-20260414T152611Z`
- tmux exitcode：
  `1`
- 这次结束是 manager 侧 SSH banner 中断，
  但官方 capture 已成功落盘；
  当前没有 active run，
  下一轮要按固定流程重新做
  `launchrunfarm -> infrasetup -> runworkload`

接手后的直接动作：

1. 先 terminate 这次中断 run 对应的 run farm，
   然后重新：
   `launchrunfarm -> infrasetup`
2. 在单独 subshell 中叠加下面这组单变量 trigger：
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=1`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND=dma-export`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH=3`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER=0`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID=2`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE=24`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING=8`
   - `PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET=32`
3. 先跑：
   `pairdummy_sbus128_workflow.sh debug-preflight`
4. 通过后再做单变量 rerun，
   不要同时打开 deep log / export probe / fixed-load probe。
   这一步现在依赖新的 strict-trigger 语义；
   若 rerun 仍提前停在
   `sb1/page42`
   一带，
   再考虑继续静态缩小目标维度，
   不要回退到大面积日志。
5. rerun 完成后，
   在 `debug_records/` 里明确写：
   - `frontier`
   - `claim_class`
   - `disturbance_risk`
   - `requires_control_rerun`
6. 若 trigger log 仍只停在 page24 尾部，
   优先回到静态阅读
   `prt_host_virt_to_phys()` /
   export page loop，
   不要直接扩大 probe 面。
7. 若仍逼到必须改硬件，
   先停下来写清楚为什么，
   不要直接改 bitstream 路线。

最新记录：

- debug：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260414T103114Z.md`
- change：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260414T103114Z.md`
- debug：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260414T125526Z.md`
- change：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260414T125526Z.md`
- debug：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260414T132728Z.md`
- change：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260414T132728Z.md`
- debug：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260414T144739Z.md`
- change：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260414T144739Z.md`
- debug：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260414T153308Z.md`
- change：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/change_records/20260414T153308Z.md`
