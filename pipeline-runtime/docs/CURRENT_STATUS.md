# Current Status

更新时间：`2026-04-14 16:52 UTC`

## 当前目标

- 主线：
  `12-pair sbus128` dummy-model 上继续推进
  `bertmini batch8 file-only`
- 方法：
  固定 workflow + freshness 闭环 + guest-file-first + breadcrumb
- 调试策略：
  先静态读代码与 artifact，
  再读 capture，
  最后才加新的窄 probe；
  新一轮窄 probe 默认走
  `trigger-gated short log`
  而不是直接回到 deep log；
  当前继续走 FPGA/FireSim 软件调试，
  不做 bitstream 重建

## 当前 authoritative 结论

- 当前仍不改硬件；
  本轮只做 FPGA/FireSim 软件调试、
  静态审计和低扰动观测收敛。
- fixed profile 已升级到
  `pairdummy-sbus128-fixed-v20`，
  且
  [`src/prt_trigger_log.c`](../src/prt_trigger_log.c)
  的 strict-trigger 语义已经生效：
  命中前若事件不在目标维度内，
  直接返回，
  不再做
  `seq/format_line/ring_push`。
- `v20` control rerun
  已完成 manager 侧收尾；
  这轮的 manager 失败是基础设施侧
  `Error reading SSH protocol banner`，
  不是 guest panic / kernel crash。
  对应 EC2
  `i-0cfb64404d409fed9`
  已确认
  `terminated`。
- 本轮最重要的有效结论不是
  `page28`
  新前沿，
  而是：
  旧的
  `sb3/page24`
  frontier
  已经被穿过，
  先前把它当成当前 blocker 的结论已失效。
- 直接证据有三条：
  1. trigger log
     恰好只有
     `33`
     行，
     等于
     `match line + post_budget(32)`；
     因而最后一行
     `sb=3 page=28 ph=pset`
     是 capture cutoff，
     不是 authoritative frontier。
  2. sparse log 明确出现：
     - `c2-export stage=0 tensor=2 phase=copy-end idx=0 subbatch=3 rc=0`
     - `worker stage=0 subbatch=3 done`
     - `worker stage=0 subbatch=4 begin`
     - `c2-export stage=0 tensor=2 phase=copy-begin idx=0 subbatch=4`
  3. breadcrumb `--all`
     中已有
     `sb=4 tensor=2 page=63 dma_submitwait_after_cleanup`
     /
     `dma_page_end`
     以及
     `sb=4 rr_release_end`
     记录，
     说明 capture 时 guest 至少已经推进到
     `subbatch=4`
     的 export 后段 / cleanup 窗口。
- 因此：
  - 之前“新前沿在
    `page28 pset -> v2p-b`
    之间”的说法无效
  - 当前更可信的表述是：
    这轮 strict-trigger control rerun
    证明旧的
    `sb3/page24`
    卡点已被穿过，
    但新的 blocker 还未被重新精确定义
- 对旧窗口
  `sb3/page24..28`
  的静态审计结论：
  - 在
    [`src/prt_dma.c`](../src/prt_dma.c)
    的 export host 直通路径里，
    `pset -> v2p-b`
    之间没有新的硬件指令，
    只剩：
    `dma_chunk_needs_bounce()`
    和紧邻其后的
    `prt_host_virt_to_phys()`
  - 结合本轮实际地址：
    `src mod64 == dst mod64 == 0`
    （page24..28 及 page63 均如此），
    `dma_chunk_needs_bounce()`
    为
    `false`；
    旧窗口不会走 bounce path，
    也没有暴露新的硬件语义错位
  - `req.src_acc == req.dst_acc == manager_id`
    与外层
    RR scope
    的 acquire/release 语义保持一致，
    当前看不出与
    `HybridMapper`
    / pair-manager
    设计约束冲突的静态证据
- 本轮新增一个纯软件工具修正：
  [`scripts/triage_prt_capture.py`](../scripts/triage_prt_capture.py)
  现在会解析
  `guest-trigger-log.txt`，
  并在
  `trigger_line_count == post_budget + 1`
  时明确提示：
  当前 trigger log 已打满预算，
  最后一行只能算 capture 截断点，
  不能直接当 frontier。
- 当前 blocker 排查 SOP 继续固定为：
  1. artifact audit
  2. 静态读代码
  3. `triage_prt_capture.py`
  4. 只有静态窗口仍不够细时，才用
     `triage_prt_capture.py --emit-trigger-env`
     生成 trigger overlay
  5. 单变量 rerun
- 下一轮不应再把 trigger 钉在
  `sb3/page24`；
  优先重定向到更靠后的窗口：
  - 首选：
    `dma-export sb=4 tensor=2 page=63`
    并适当增大
    `POST_BUDGET`
    （如
    `64`
    或
    `96`）
  - 备选：
    runtime family 仅钉
    `subbatch=4`
    的 worker 边界
  - 仍然先静态读代码，
    再做单变量 rerun

- `v15`
  live run
  `192.168.1.206`
  已把 export 路径缩到：
  `tensor=2 page=63 dma_page_end`
  之后的极窄窗口：
  - `dma_batch_scope_release(&scope)`
  - `prt_rr_release_scope(scope)`
  - `rr_release(cfg)`
  - 以及上层
    `c2-export copy-end/retire`
- 这已经把旧的三类 export 候选清掉：
  1. `dma_batch_scope_acquire()` /
     `prt_rr_acquire_scope()` 返回边界
  2. export page0 的
     `prt_host_virt_to_phys((const void *)dst_ptr, &dst_pa)`
  3. page0 首个
     `dma_submit_wait_annotated_scoped()` /
     `prt_dma_submit()` /
     `prt_dma_wait()`

- `v16`
  通过新增
  `RR_RELEASE_BEGIN/END`
  breadcrumb
  继续推进了同一条 live 主线：
  - profile：
    `pairdummy-sbus128-fixed-v16`
  - host：
    `192.168.1.68`
  - sparse log 已明确出现：
    - `c2-export stage=0 tensor=2 phase=copy-end idx=0 subbatch=3`
    - `c2-export stage=0 tensor=2 phase=retire idx=0 next_sbatch=4`
    - `worker stage=0 subbatch=3 done`
    - `worker stage=0 subbatch=4 begin`
    - `pointwise-matmul-fallback ... mgr=0..3 ... begin/end`
    - `conv-sync-strided ... mgr=0..3 ... fence-end rc=0`
- 因而新的 authoritative 结论是：
  export 侧的
  `rr_release`
  不是 blocker；
  当前前沿已经后移到
  `stage0/subbatch4`
  pointwise fallback
  更晚的路径

- 对 pointwise split-OC 路径做了静态审计：
  - `split_1d_range()`
    与 host 参考实现一致
  - 子 conv 只对
    `out_channels / weights / bias / output`
    做
    `oc_beg`
    偏移，
    仍保留 full
    `in_stride=256`
    `weight_stride=256`
    `out_stride=256`
  - 这与
    `HybridMapper`
    的按
    `OC`
    切分语义、
    以及
    [`runtime_mechanisms.md`](runtime_mechanisms.md)
    /
    [`alignment_constraints.md`](alignment_constraints.md)
    中当前 runtime 兑现方式对齐；
    静态上没看到
    `tile4`
    指针计算本身的错误

- 若把 `v16` sparse frontier 当真，
  当前静态窗口已经非常窄：
  从
  `conv-sync-strided stage=0 mgr=3 fence-end rc=0`
  到下一条可见的
  `oc-split-pointwise ... end`
  /
  `mgr4 begin`
  之间，
  实际有语义动作的只剩：
  - `prt_rr_release_scope(&scope)`
  - 返回到
    `conv_call_for_manager_sync_strided()`
  - 回到外层
    `oc-split-pointwise ... end`
    和下一轮
    `mgr4`
    入口

- 但这轮又发现了新的 observability 问题：
  `v16`
  同一份 capture 内，
  sparse log 与 breadcrumb 前沿不一致
  - sparse log 文件尾部只到：
    `subbatch=4 mgr=3 fence-end`
  - breadcrumb 却显示：
    `subbatch=4 mgr=6 gemmini_pointwise_call_begin`
- 静态阅读
  [`src/prt_breadcrumb.c`](../src/prt_breadcrumb.c)
  后确认根因：
  旧实现的 breadcrumb slot 只按
  `stage`
  选槽，
  所有
  `stage 0`
  的 Gemmini / RR / DMA 事件都在争抢同一个 slot；
  在当前 live 并发下，
  `last_phase`
  不再可靠

- 本轮已做纯软件、低扰动修正：
  - [`src/prt_breadcrumb.c`](../src/prt_breadcrumb.c)
    - breadcrumb slot 现在按
      `stage/subbatch/kind/manager/tensor/page/token`
      做稳定 hash，
      不再把整个
      `stage 0`
      压到单槽上
  - [`scripts/decode_prt_breadcrumb.py`](../scripts/decode_prt_breadcrumb.py)
    - `--all`
      输出改为按
      `seq`
      排序，
      便于直接看最新局部前沿
  - [`scripts/pairdummy_sbus128_fixed_env.sh`](../scripts/pairdummy_sbus128_fixed_env.sh)
    - profile 升级到
      `pairdummy-sbus128-fixed-v17`
    - 当前固定 profile 关闭了
      fixed-load / export
      高频 probe，
      回到
      breadcrumb-only
      的低扰动观察面

- 继续观察 `v17`
  live run 后，
  又获得了比上一轮更窄的新前沿：
  - `heartbeat.csv`
    在
    `724s -> 941s`
    期间继续推进
  - `bertmini-batch8.log`
    大小固定在
    `368377`
    bytes
  - sparse 文件尾始终停在：
    `conv-sync-strided stage=0 mgr=7 flushed use_pointwise=1`
  - 新的 static-first 结论因此变成：
    当前可疑窗口已经前移到
    `mgr7 flushed`
    之后、
    `dispatch=pointwise`
    之前

- 对这段新窗口做静态阅读后确认：
  在当前 fixed profile 下，
  这段路径里已经没有新的硬件语义动作；
  主要只剩：
  - 若干不会真正落盘的 marker / crit 调用
  - coarse guest log append
    `dispatch=pointwise`
  - 以及随后的
    pointwise call-begin breadcrumb
- 这意味着当前第一嫌疑从“硬件 release 语义”
  转成了
  “pointwise 热路径里剩余 coarse guest log 自干扰”

- 本轮又补跑了静态 artifact 审计：
  - `segments=13`
  - `split_kind_coverage={'oc': 32, 'resadd_spatial': 6, 'single': 2}`
  - `op_type_coverage={'conv': 32, 'resadd': 8}`
  - 结果：
    PASS

- 因而本轮做了新的纯软件、低扰动修正：
  - [`src/prt_gemmini_adapter.c`](../src/prt_gemmini_adapter.c)
    - 当 breadcrumb 已启用时，
      关闭 pointwise 热路径内已被 breadcrumb 覆盖的 coarse guest log
  - [`scripts/pairdummy_sbus128_fixed_env.sh`](../scripts/pairdummy_sbus128_fixed_env.sh)
    - profile 升到
      `pairdummy-sbus128-fixed-v18`
    - 关闭
      `PIPELINE_RUNTIME_CRITICAL_UART_PROBE`
      以去掉当前 profile 下本来就不会输出的
      `CRIT`
      探针格式化开销

- `v18`
  已完成部署闭环：
  - `image-closure`：
    通过
  - local freshness：
    通过
  - remote freshness：
    `192.168.1.14`
    通过
  - runtime binary sha256：
    `36e85f2a3e357f6d2b0fc79a741120f6566effebc3c140b36e56b8be325d0a97`
  - firemarshal env sha256：
    `04aa1fe0bb9e00d992924adb8c751f4fe16ed7138cd2d816c600e218edbd285e`
  - remote image sha256：
    `eb8ada339a11d77f2c61268e95f3cb26c91c61fd1d976dc768e3de1efe1dc9a3`

- `v18`
  重新拉起 run farm 时，
  `launchrunfarm`
  曾因 AWS `f2.6xlarge`
  容量不足在多个 subnet 间重试；
  这不是新的 runtime blocker

- 当前 run 状态：
  - 最近一次 control rerun：
    - host：
      `192.168.1.14`
    - session：
      `pairdummy-sbus128-runworkload-20260414-150549`
    - results dir：
      `sims/firesim/deploy/results-workload/2026-04-14--15-05-50-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
    - manager log：
      `sims/firesim/deploy/logs/2026-04-14--15-05-50-runworkload-UIKVAB93PDHR9S5I.log`
    - pane：
      `tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260414-150549.pane.log`
    - official capture prefix：
      `tmp/firesim-aws-f2/captures/pairdummy-sbus128-runworkload-20260414-150549-192.168.1.14-host-watchdog-20260414T152611Z`
    - tmux exitcode：
      `1`
  - 这次退出是 manager 侧 SSH banner 中断，
    发生在官方 capture 已落盘之后；
    当前不要把它记成新的 guest root cause。
  - 下一轮 rerun 前，
    按固定 FireSim 约束重新做：
    `launchrunfarm -> infrasetup -> runworkload`
    不直接复用这次中断 run 的 manager 状态。

## 当前固定配置

- profile：
  `pairdummy-sbus128-fixed-v19`
- target：
  `rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128`
- batch：
  `8`
- `NUM_CORES=4`
- `NUM_GEMMINI=12`
- `NUM_DMA=12`
- `PAIR_MANAGER_MODE=1`
- `DUMMY_GEMMINI_MODE=1`
- `PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD=1`
- `PIPELINE_RUNTIME_SKIP_INPUT_LOAD=1`
- `PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK=1`
- `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
- `PIPELINE_RUNTIME_UART_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
- `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_CRITICAL_UART_PROBE=0`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=1`
- `PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`
- 当前 profile 的目的：
  在不回到高频文本 probe 的前提下，
  让 pointwise 热路径回到
  breadcrumb-first
  观察面，
  避免 coarse guest log
  在同一窗口里继续自扰；
  trigger-gated 短日志默认保持关闭，
  只在 triage 明确建议后做单变量 overlay

## 当前关键文档

- 总纲：
  [`project_guide.md`](project_guide.md)
- 硬约束：
  [`constraints/hard_constraints.md`](constraints/hard_constraints.md)
- 固定流程：
  [`workflows/pairdummy_sbus128.md`](workflows/pairdummy_sbus128.md)
- 观测机制：
  [`testing/observability.md`](testing/observability.md)

## 当前最新记录

- debug：
  [`../debug_records/20260414T103114Z.md`](../debug_records/20260414T103114Z.md)
- change：
  [`../change_records/20260414T103114Z.md`](../change_records/20260414T103114Z.md)
- debug：
  [`../debug_records/20260414T125526Z.md`](../debug_records/20260414T125526Z.md)
- change：
  [`../change_records/20260414T125526Z.md`](../change_records/20260414T125526Z.md)
- debug：
  [`../debug_records/20260414T132728Z.md`](../debug_records/20260414T132728Z.md)
- change：
  [`../change_records/20260414T132728Z.md`](../change_records/20260414T132728Z.md)
- debug：
  [`../debug_records/20260414T144739Z.md`](../debug_records/20260414T144739Z.md)
- change：
  [`../change_records/20260414T144739Z.md`](../change_records/20260414T144739Z.md)
- debug：
  [`../debug_records/20260414T153308Z.md`](../debug_records/20260414T153308Z.md)
- change：
  [`../change_records/20260414T153308Z.md`](../change_records/20260414T153308Z.md)

## 下一步

1. terminate 这次中断 run 对应的 run farm，
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
3. 先跑：
   `pairdummy_sbus128_workflow.sh debug-preflight`
4. 通过后再做单变量 rerun，
   只看：
   - `heartbeat.csv`
   - `bertmini-batch8.status`
   - `bertmini-batch8.breadcrumb.bin`
   - `bertmini-batch8.trigger.log`
5. rerun 后继续按
   “静态读代码 -> capture -> 更窄软件 probe”
   顺序推进；
   继续禁止硬件修改
