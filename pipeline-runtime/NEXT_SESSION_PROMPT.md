# Next Session Prompt

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

固定入口：

- profile：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
- workflow：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
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
- 当前继续走 FPGA/FireSim 软件调试；
  不做 bitstream 重建，
  不走 metasim，
  除非后续证据已经逼到必须改硬件。
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
