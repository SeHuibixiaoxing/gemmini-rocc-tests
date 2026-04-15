# Blockers And Lessons

本文件只保留当前仍有效的硬约束、关键教训和恢复顺序。
旧的逐小时时间线不再写回主文档；历史细节请看 `docs/archive/2026Q1_history.md`。

## 固定 workflow

- 当前 `spm page-table`
  分配语义有一条必须持续保留的硬约束：
  只要 runtime 需要“多页物理连续”的 page-table backing，
  就**不能**退回普通匿名页来替代 hugetlb。
  原因不是虚拟地址是否连续，而是：
  普通匿名页只保证虚拟连续，不保证物理连续；
  对当前 `spm_xlate`
  硬件路径，这不满足语义要求。
- 当前 `spm page-table hugetlb`
  调试也有一条新的实现纪律：
  不要再把 hugepage 建立过程隐藏在
  `MAP_POPULATE`
  或整段 bulk `memset`
  里。
  对 FireSim RISC-V guest，
  这会把长时间页建立工作吞进日志盲区，导致看起来像“卡死”却无法定位。
  正确做法是：
  - `mmap` 不做隐式 populate
  - 后续由显式逐页触碰 + pagemap probe 建立映射并验证物理连续性
  - 中间保留 progress log
- 做 guest image 文件核对时，
  `debugfs`
  的目标设备必须是 run host 上的
  `/home/ubuntu/sim_slot_0/*.img`
  ；
  不要再误把 run host 自己的
  `/dev/root`
  当成 guest image。
- 做 freshness / strings / sha 判断时，
  `/home/ubuntu/sim_slot_0/*-bin`
  不是最终裁决对象；
  它只能说明 FireMarshal workload 包装物的一部分。
  真正要核对 runtime 是否换新，
  必须直接从 guest image 内抽：
  `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`。
- 如果看到
  `bertmini-batch8.log`
  停在某一前沿，
  同时满足：
  - heartbeat 继续增长
  - log 文件大小正好卡在页对齐值
  - 文件尾部有大量 `NUL`
  那么不要直接把它当成“代码执行点真的停在最后一行日志”。
  对当前 pairdummy/sbus128 主线，
  这更像 guest 文件追加/刷盘路径也可能成为 blocker。
  这种情况下要立刻启用或补充独立小文件旁路，
  例如
  `checkpoint log`
  ，不要只盯主 sparse log。
- 对当前 Linux/file-log 调试链路，
  不要在 guest runner 的细粒度 stage crumb
  上做前台阻塞
  `sync`
  。
  这轮已经确认：
  `run_rerocc_pipeline_runtime_bertmini.sh`
  里的
  `write_runner_stage() -> runner_stage_sync() -> sync`
  会把 shell 卡在
  `wchan=do_wait`
  ，从而把前沿假性前移到
  `after-resolve-manager-layout`
  之前。
  正确策略是：
  - stage crumb 允许纯追加写文件
  - 刷盘依赖 wrapper 的周期性 `sync`
    或非阻塞后台 flush
  - 不要再把“每写一条 crumb 就 foreground sync”
    当成默认手段
- live 查询 guest 文件时，
  不能只依赖
  `debugfs -R "cat <path>"`
  的空输出去判断“文件不存在 / wrapper 还没跑到这里”。
  这轮已经遇到：
  - 单文件 `cat`
    一度返回空
  - 但
    `debugfs -R "ls -l /root/pipeline-runtime-debug"`
    已经能看到对应文件 inode 和非零大小
  正确顺序应当是：
  1. 先 `ls -l` 目标目录
  2. 确认文件是否存在、size 是否非零
  3. 再对单文件 `cat`
     或 `tail`
  否则很容易把真实前沿误判成“guest 还没进 wrapper”
- 但这条规则还要配另一条判别条件一起用：
  如果
  `debugfs -R "ls -l /root/pipeline-runtime-debug"`
  返回
  `File not found`
  ，同时：
  - `heartbeat.csv`
    继续增长
  - `uartlog`
    也继续增长
  那么当前只能说明 guest 还在 Linux boot / init 早期，
  尚未执行到创建
  `pipeline-runtime-debug`
  的用户态脚本；
  这不等于
  “host 假完成”
  ，也不等于
  “runtime 主逻辑已经再次卡死”
- remote freshness 排查 guest boot/regression 时，
  不能只看 `.img`
  和 image 内 runtime 文件。
  还必须记得核对
  `+prog0`
  对应的远端
  `...-bin`
  是否与本地当前 build 一致，
  否则会遗漏
  “image 是新的，但 Linux/OpenSBI 仍是另一版”
- 当前 pairdummy/sbus128 主线的 host watchdog
  默认观测面也必须保留为“文件侧全链路”：
  - `bertmini-batch8.status`
  - `bertmini-batch8.log`
  - `bertmini-batch8.wrapper.stage`
  - `bertmini-batch8.wrapper-proc.stage`
  - `bertmini-batch8.runner.stage`
  - `bertmini-batch8.runner-early.stage`
  - `bertmini-batch8.runner-proc.stage`
  - timeout capture 时额外抓
    `bertmini-batch8.checkpoint.log`
  不要再回到“只盯 sparse/status、等 timeout 后才发现少了关键 stage 文件”的旧状态。
  这类错配。
- 当前 `alloc-contig / hugetlb`
  前沿已经加了一套
  `checkpoint`
  旁路断点。
  后续如果主 sparse log 再次停在
  `spm-pt alloc-contig begin`
  ，默认先看
  `/root/pipeline-runtime-debug/bertmini-batch8.checkpoint.log`
  ，不要回退去怀疑 stale image。
- 对当前 pairdummy/sbus128 主线，
  `checkpoint.log`
  不能长期以“覆盖整个
  stage0 tensor=2
  的每个 DMA submit/wait”
  这种高频方式常开。
  `2026-04-13 03:53 UTC`
  这轮已经确认：
  旧的
  `subbatch=2 compute-done -> c2 export`
  blocker
  被越过之后，
  guest 还能继续推进到
  `subbatch=7`
  ，但随后新的冻结点稳定落在：
  `checkpoint=program-begin`
  之后、`program-post-fence`
  之前。
  这说明 checkpoint/file-log 自身已经可能变成新的人工阻塞源。
  后续纪律应当改成：
  - 只在怀疑窗口内短时启用高频 checkpoint
  - 一旦确认旧 blocker 已越过，就立刻把 checkpoint 收窄
  - 常态观测面仍以低扰动 sparse + 少量 stage/status 文件为主

- `12-pair sbus128` dummy-model 主线现在有固定 profile 和固定 workflow：
  - profile:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
  - workflow:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
  - runbook:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pairdummy_sbus128_reusable_workflow_20260412.md`
- 固定 workflow 里，
  `resolve_private_ip()`
  不能再写死
  `tag:fsimcluster=firesim`
  。
  当前 runtime yaml 真正使用的 tag 是
  `run_farm_tag`
  ，例如这条线是
  `pairbertb8d12s128a`
  。
  后续私网 IP 解析必须从 runtime config 动态读取这个值，
  否则会连错实例或者误判“没有实例”。
- 如果复用了一台已经跑过 workload 的旧 host，
  remote rootfs image 的**整图 sha**
  会因为 guest 写过日志而变化。
  这时不要把 remote freshness 失败误判成 stale runtime，
  但也不要直接放行。
  正确做法是：
  - 把本地 pristine `.img`
    和 `-bin`
    重新覆盖到
    `/home/ubuntu/sim_slot_0/`
  - 清理
    `uartlog/heartbeat/memory_stats`
    等外部残留文件
  - 然后再重做 remote freshness
- 如果 runworkload 几乎立即完成，
  `uartlog`
  只有
  `script started/done`
  两行，
  且 guest 侧没有任何
  `pipeline-runtime-debug`
  文件，
  第一件事不是回到 runtime 代码，
  而是检查：
  - `/home/ubuntu/sim_slot_0/FireSim-f2`
  - `/home/ubuntu/sim_slot_0/libriscv.so`
  - 其它 driver bundle `.so`
  是否意外变成了 `0` 字节。
  当前已经见过一次：
  `driver-bundle.tar.gz`
  本身内容正常，
  但 slot 目录下解出的 driver 文件全是 `0` 字节，
  导致
  `./FireSim-f2`
  像空脚本一样直接 `exit 0`
  ，把 manager 误导成“仿真已正常完成”。
- 如果手工重新解出 driver bundle 后，
  `./FireSim-f2 +check-fingerprint`
  仍报
  `AFI in Slot is not in READY state`
  ，同时
  `fpga-load-local-image`
  / `fpga-describe-local-image`
  继续表现为
  `rc=0`
  但空输出，
  就不要再把这台 host 当成可靠现场。
  当前经验是：
  这种复用 F2 host 的 FPGA 管理面可能已经坏掉，
  即使 reboot 也未必恢复；
  应优先 terminate 实例并回到 fresh launch。
- 以后这条主线不要再在 shell 里手工堆 env 覆写编译/运行配置；默认只允许改固定 profile 文件，或者新增独立 debug profile。
- generic
  `rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync.sh`
  不再默认
  `DUMMY_GEMMINI_MODE=1`
  ，避免旧入口名字被 silently 漂移成 dummy synthetic 路径。
- 判断“现场是否还有 live run”时，不能只看 tmux 会话名是否残留。
  必须以
  `aws ec2 describe-instances`
  查到的运行中 `f2.6xlarge`
  为准；
  `terminaterunfarm --forceterminate`
  之后留下的 tmux shell 不代表实例仍然存活。
- FireSim manager 的 tmux pane log 可能会比正式
  `sims/firesim/deploy/logs/*.log`
  滞后。
  如果 pane log 长时间只停在
  `Checking if host instance is up...`
  或类似早期文案，不要立刻误判 manager 卡死；
  先看正式 log 是否已经推进到 rsync / flash / preflight。
- 对当前 pairdummy synthetic-model 主线，
  仅有
  `before-prefault / after-prefault`
  两个点位还不够。
  如果 live 已确认推进到
  `synthetic-model alloc before-prefault`
  ，但 heartbeat 继续增长而 log 不再变化，
  不要先回退成
  “又是 Linux boot 卡死”
  或
  “mlockall 卡死”；
  应优先在
  `prefault_and_lock_blob()`
  的逐页触碰循环里补中间 progress log，
  把断点收敛到页触碰本身。

## 执行硬约束

- 只用 `f2.6xlarge`。
- FireSim 只走 manager 正规流：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- FireSim manager 命令只通过：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- FireMarshal 只通过：
  `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
- 传给
  `scripts/firemarshal-tmux-run.sh`
  的 workload json 必须按 repo 根目录语义解析；
  当前 wrapper 已做自动绝对路径归一化，
  以后不要再把
  `software/firemarshal`
  当前目录当成 workload json 的解析基准。
- 执行 FireSim manager 命令前，必须先：
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh --skip-ssh-setup`
- 这条约束现在还要按更细的实现纪律执行：
  如果当前 shell 开着
  `set -u`
  ，source 前先临时
  `set +u`
  ，source 完再恢复
  `set -u`
  。
  原因是当前 conda/riscv tools 激活脚本里会触碰未定义的
  `RISCV`
  变量；
  这是环境脚本问题，不是允许跳过
  `sourceme-manager.sh`
  的理由。
- 如果需要 SSH 到 run host 做 live 查询，必须先用
  `aws ec2 describe-instances`
  查到实例私有地址，再通过私网地址连接；
  不要使用公网地址
- 每次 `infrasetup` 或 `runworkload` 前，必须先核对 workload image/rootfs freshness。
  最少核对：
  - workload json 里的 `common_bootbinary` / `common_rootfs`
  - 对应 FireMarshal `build` / `install` 日志
  - 目标 image 的路径、mtime、size
  最好再补：
  - `sha256sum`
- 每次新增或切换 runtime/hwdb/workload 入口后，必须同步更新文档状态。
  至少同步：
  - `docs/CURRENT_STATUS.md`
  - `docs/blockers_and_lessons.md`
  不允许把“本轮到底跑了哪套入口、镜像是否已换新、现在停在哪一步”只留在对话里。
- 任何 image/rootfs/binary/AGFI 变化之后，下一轮都要重新跑 `infrasetup`。
- 任何 run 被打断、超时或人工停止之后，下一轮也必须重新跑 `infrasetup`。
- 每轮 run 结束、失败或人工中断后，先保留结果目录，再立刻 `terminaterunfarm --forceterminate`，并继续核对 EC2 状态直到实例不再 `running`。
- 以后停止 FPGA run，
  默认就用
  `terminaterunfarm --forceterminate`；
  不要再裸跑需要交互确认的
  `terminaterunfarm`
  并把 manager 卡在 `Type yes`。
- 不要只看 manager exit code。至少同时联查：
  - `uartlog`
  - `heartbeat.csv`
  - `bertmini-batch8.log` / `bertmini-batch8.deep.log`
  - 结果目录中的 `status`
- 但也不要把 manager 看到的
  `runworkload exit code=0`
  直接等价成“硬件仿真真正跑起来了”。
  在真正继续 guest runtime 调试前，
  还要至少加一道 host-side 预检：
  - `/home/ubuntu/sim_slot_0/FireSim-f2`
    非零大小
  - 手工
    `timeout ... ./FireSim-f2 +slotid=0 +check-fingerprint`
    可以通过
- 对当前 pairdummy file-only workload，
  guest 真正执行入口是 image 内的
  `/firemarshal.sh`；
  不要再把
  `/root/firemarshal.sh`
  当成实际入口来做 freshness 判断或执行路径推理。
- 对当前 pairdummy file-only workload，
  freshness 最少必须同时校验 5 项：
  - `/firemarshal.sh`
  - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
  - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
  - `/firemarshal.env`
  只校验 runner / runtime / env 不够；
  wrapper 或 entrypoint 旧了，也会造成“镜像看似新、实际执行路径仍旧”的假象。
- 当前语义对齐基线必须以
  `HybridMapper + MudnacSim`
  为准；
  不能把
  “遇到重复 alias target 就跳过一次 copy”
  当成最终修复。

## 2026-04-09 语义结论

- 当前 `pairdummy/sbus128` 线上，
  最需要优先对齐的不是新的 `mlockall` 解释，
  而是 `ALL_RINGBUFFER` 的实现语义。
- 已静态确认：
  - `HybridMapper` 中，
    `ALL_RINGBUFFER`
    只来自
    `INTER_PURE_DECOUPLING`
  - segment 边界 tensor 在初始化时会被标成
    `TYPE_IO / IO_SINGLE`
  - 现有 SA 变异逻辑会跳过 `TYPE_IO`
  - 因而在当前搜索流程下，
    segment 边界 tensor 原则上不应被改成
    `ALL_RINGBUFFER`
- 已静态确认：
  - `MudnacSim` 中，
    `ALL_RINGBUFFER` 是 pure ring transport：
    - 不额外分配本地 `pSpmPages`
    - entry 直接从 ring slot 取页并在消费后 `use()`
    - export 直接向 ring slot 写并在生产后 `fill()`
- 已静态确认：
  - 当前 `bertmini` 的 `tensor 6`
    不是 segment 边界张量，
    而是 `segment 3` 内部的 stage-to-stage 运输张量
  - 所以即便“segment 边界不允许 ALL_RINGBUFFER”
    这条语义成立，
    当前 `tensor 6` 这个具体 case 也不应直接被归类为该错误
- 当前更大的实现偏离是：
  - `pipeline-runtime`
    仍会在 export 后无条件把这类 tensor
    materialize 回 model alias
  - 这会把原本应只走 ring 的内部运输张量，
    又拉回 host alias/export DMA 热路径
- 当前 worktree 中用于止血的 alias dedup patch
  只能视为调试辅助；
  文档、代码和后续结论都不能把它写成
  “根本性语义修复”
- 如果后续再核查 yaml 是否生成错误，
  应优先区分两类问题：
  - 真正的 segment 边界 tensor 被错误生成为 `ALL_RINGBUFFER`
  - segment 内部 tensor 本来就允许 `ALL_RINGBUFFER`，
    但 runtime 错误地把它重新 materialize 到 alias

## 日志纪律

- `2026-04-08` 的最新 `sbus128 pairdummy` 调试已经**覆盖**了前面那条
  “先不要碰 `mlockall`” 的临时建议。
  当前工作树里已经加入一个临时编译开关：
  - `PRT_MLOCKALL_MODE=0`: 原行为 `MCL_CURRENT | MCL_FUTURE`
  - `PRT_MLOCKALL_MODE=1`: `MCL_CURRENT`
  - `PRT_MLOCKALL_MODE=2`: 跳过 `mlockall`
  - `PRT_MLOCKALL_MODE=3`: `MCL_FUTURE`
  其中 `pairdummy` 调试入口默认是 `1`，其它入口仍默认 `0`。
- 最新 decisive 结论：
  在 `sbus128 pairdummy + PIPELINE_RUNTIME_MLOCKALL_MODE=1` 下，
  runtime 已明确走过：
  - `before mlockall`
  - `after mlockall rc=0`
  - `synthetic-model alloc before/after-mmap`
  - `init synthesize-model-bin end`
  然后才在
  `segment[0] failed: not_ready (-7)`
  退出。
  所以对当前这条调试线来说，
  `MCL_CURRENT` 本身已经不是 blocker；
  更可疑的是
  `MCL_FUTURE`
  或
  `MCL_CURRENT | MCL_FUTURE`
  的组合。
- 这也意味着：
  当前不要再把最新 `pairdummy sbus128 current-only` 结果写成
  “synthetic-model init hang”。
  更准确的说法是：
  - 原始锁页模式下，先前卡点在 `mlockall` 前后
  - `current-only` 调试模式下，程序继续推进并暴露出新的
    `segment 0 not_ready (-7)`
    blocker
- 当前下一轮最值得追的不是 `mlockall`，而是
  `PRT_ERR_NOT_READY (-7)` 的返回链。
  静态上优先看：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
    - `4019` 附近的 `worker stage=%u exit ...`
    - `4529` / `4543` 附近的 `segment[%u] failed`
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c`
    `563-631` 一带的 `PRT_ERR_NOT_READY`
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
    内部更早的一批 `PRT_ERR_NOT_READY` 返回点，
    特别是 stage/topology/SPM-xlate 相关路径
- 当前本地结果目录里：
  - `bertmini-batch8.log` 有效
  - `bertmini-batch8.status` 有效
  - 但 `bertmini-batch8.deep.log` 和 `bertmini-batch8.audit.log` 是空文件
  所以继续追 `not_ready` 时，优先补最小 guest 文件粗日志；
  不要先假设深日志已经有足够信息。
- `runworkload` manager 正常退出
  **不等于**
  guest runtime pass。
  `2026-04-08` 这轮 manager `exit code=0`，
  但 guest status 同时是：
  - `state=finished`
  - `exit_code=1`
  - `BERTMINI_PIPELINE_RUNTIME_FAIL`
  以后必须同时看 manager 结果目录和 guest `status/log`。

- Linux 启动日志可以继续走 UART。
- 但 bin/runtime 日志默认不要走 UART；当前主线策略是文件日志优先。
- 粗粒度日志：
  `/root/pipeline-runtime-debug/bertmini-batch8.log`
- 细粒度日志：
  `/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
- 如果要追某个卡点，优先开 segment/stage/subbatch gating，而不是往 UART/stdout 再塞更多输出。
- 当前细日志 gating 入口已经存在：
  - `--deep-log-segment`
  - `--deep-log-global-stage`
  - `--deep-log-local-stage`
  - `--deep-log-subbatch`
  - `--deep-log-stage-radius`
  - `--deep-log-subbatch-radius`
- Linux 启动本来就慢。只要没有 panic/crash，且 heartbeat 或启动日志还在前进，就不要把安静窗口误记成新的 boot blocker。
- 旧 frozen mainline 的 “run 到 final golden mismatch” 不能直接拿来证明当前
  pairdummy bring-up 的 `synthesize-model-bin` 路径没问题。
  原因是两者在 `validate-artifacts` 之后分叉：
  - 旧 mainline：`load-model-bin`
  - 当前 pairdummy：`skip-model-bin-load -> synthesize-model-bin`
  所以如果当前停在 `init synthesize-model-bin begin`，不要写成
  “旧硬件同一路径不挂，新硬件才挂”。
- `2026-04-08` 又补了一轮旧 coupled-DMA baseline 复现，结论更强：
  在
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
  这条旧主线 workload 上，只要默认值仍是
  `DUMMY_GEMMINI_MODE=1`
  且走
  `skip-model-bin-load`
  synthetic 路径，旧硬件也会：
  - 正常 boot Linux
  - 到达 `running /etc/init.d/S99run`
  - 正常把 runtime 日志写到 guest 文件
  - 然后冻结在
    `init synthesize-model-bin begin reason=skip-model-bin-load`
  因此当前 blocker 应描述为
  “synthetic-model init 路径 hang”，
  而不是
  “只有 pairdummy / 新硬件才挂”。
- 静态上还确认了一条反证：
  `model.layers.yaml` 顶层 `address: [0, 17055744]`
  会被 YAML loader 解析成
  `model.addr_base / model.addr_end`，
  而 `compute_synthetic_model_blob_size()` 会优先用这段区间推导 synthetic blob 大小。
  所以当前 synthetic blob 规模只有
  `17055744` 字节，约 `16.3 MiB`。
  这意味着“因为 synthetic mmap 特别大，单凭大小就把系统拖死”目前证据并不强；
  如果继续查 `synthesize-model-bin`，要优先盯函数内实际停点，而不是先假设它在申请一个巨型匿名映射。
- 做硬件 A/B 时要一次只改一个变量。
  当前用户已明确要求下一轮先保持：
  - pipeline runtime 代码不改
  - `mlockall(MCL_CURRENT | MCL_FUTURE)` 原行为不改
  - workload / image / rootfs 不改
  然后只把 pairdummy 硬件从 `sbus128` 切到 `sbus64`。
- `sbus64` 新 AGFI
  `agfi-03b04a5e09190465d`
  的首次标准 `infrasetup` 已证明一个独立约束：
  FireSim 会按 AGFI metadata 取 deploy quintuplet；如果本地源码缺少对应
  Scala config 类，driver build 会先失败，甚至还到不了 workload。
  当前这颗 AGFI 的 metadata 需要 `...Dummy16x16Sbus64`，
  但本地只存在 `...Sbus128` / `...Sbus256`。
  所以在当前代码树上继续做 A/B 时，必须明确区分：
  - 是 driver/config 不匹配导致的 infra 失败
  - 还是 guest/runtime 真正进入 workload 后的新停点
- 当前为推进 A/B，`sbus64` hwdb 里临时用了
  `deploy_quintuplet_override -> ...Dummy16x16Sbus128...`
  来复用现有 driver。
  这只是 bring-up workaround，不等价于“本地已经补齐了真正的 sbus64 deploy config”。
- 更重要的是，当前这条 workaround 只解决了 infra 侧的早期 driver build 阻塞，
  **没有**解决 guest 内部停点：
  在 `sbus64 + deploy_quintuplet_override->sbus128` 下，
  `runworkload` 仍能成功 boot Linux、进入 `S99run`，但 guest 文件日志再次冻结在
  `init synthesize-model-bin begin reason=skip-model-bin-load`。
  复查时 `heartbeat.csv` 仍持续增长，说明这是
  “仿真继续跑、runtime 逻辑卡住”的 hang，而不是 Linux 没起来。
- 如果只是 AGFI/hwdb/runtime config 变化而 image 没变，不要无意义地重建 FireMarshal 镜像；
  但下一轮仍然必须重新 `infrasetup`，因为 AGFI 已变化。

- 当切到新的硬件线时，不要直接覆盖旧 frozen mainline。
  正确做法是：
  - 新增独立 workload/runtime/hwdb 入口
  - 保留旧主线作为 regression baseline
  - 文档中明确写清楚“哪条是 frozen mainline，哪条是新的 bring-up 分支”
- 还要额外警惕“共享脚本被顺手改动”导致 frozen mainline 失去可重建性。
  当前工作树里，除了新增 `pairdummy` 入口之外，共享 fileonly / bertmini
  脚本默认值也已经漂移；例如共享
  `rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync.sh`
  当前默认会打开 `DUMMY_GEMMINI_MODE=1`。
  所以未来如果有人说“重建旧 mainline”，必须先说清楚是：
  - 复用 `2026-04-02` 的 frozen artifact
  - 还是用当前工作树重新 build 一版“旧入口名字但新默认值”的镜像

## 当前冻结 blocker

- 当前 mainline blocker 不是 DMA hang，而是：
  **`bertmini` 在 Linux/F2 上完整执行完成后，final golden mismatch 停在 `tensor=48`。**
- 冻结证据在：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/`
- 关键 verdict：
  - `segment=31 threaded backend complete target_subbatch=8`
  - `golden mismatch: tensor=48 bytes=65536 mismatch=1823 ...`
  - `state=finished`
  - `exit_code=1`
  - `Simulation complete.`
  - `*** PASSED *** after 44898716942 cycles`
  - `COMMAND_EXIT_CODE="0"`

## 当前最重要的教训

- 当前主线已经证明：
  - Linux/F2 启动链路可以通过
  - 文件日志链路可以通过
  - `segment=31` 可以完整结束
  - guest 可以正常触发关机和 FireSim copy-back
  所以不要继续把当前主线默认写成 “export DMA infinite wait”。

- 当前 golden mismatch 先不要直接解释成 RTL 错误。
  当前更准确的解释是：
  FPGA backend 与当前 CPU-derived reference，在 synthetic bertmini runtime artifacts 上出现了分歧。

- 当前 reference/golden 链不是绝对真值源。
  原因包括：
  - manifest 仍是 `mode: fresh`
  - fresh 导出链明确会走 dummy runtime data 生成路径
  - `golden.*.bin` 来自 host closure 里的 CPU backend
  - runtime 里仍有调试期硬编码语义：
    - conv activation = `RELU`
    - conv output scale = `1.0`
    - resadd `A/B/C_scale = 1.0`
    - resadd `relu = 0`

- 当前 scene 不要按“完全异步 overlap”去推理。
  在 `spm_xlate_enable=1` 时，runtime 会把 `sync_mode` 强制回 `blocking_debug`，进而把：
  - DMA 收敛到 `PRT_DMA_BACKEND_BLOCKING_FENCE`
  - Gemmini 收敛到 `PRT_GEMMINI_MODE_BLOCKING_FENCE`

- 当前 `num_cores >= num_gemmini_mgrs` 不是需求约束，而是实现残留。
  更准确地说：
  `num_cores`
  现在被 runtime 同时拿去做 CPU/hart 上界、accelerator slot/page-domain 上界、以及 page allocator 规模参数。
  所以代码才会强制把它拉到不少于 `num_gemmini_mgrs`。
  这条绑定不要再被解释成
  “CPU 数必须跟 Gemmini 数绑定”。
  如果未来要恢复大规模架构目标，正确方向是拆出独立的
  `num_cpu_harts`
  、
  `num_acc_slots/page_domains`
  、
  `num_gemmini_mgrs/num_dma_mgrs`
  语义。

- 当前 Gemmini 基本数据类型要按实际硬件看，而不是靠记忆：
  - `inputType = SInt(8.W)`
  - `accType = SInt(32.W)`
  - `spatialArrayOutputType = SInt(20.W)`

- 当前卷积越界语义是 zero padding，不是 edge clamp。

- 当前 pair-manager dummy16x16 bring-up 先不要误写成“已经在新硬件上吃满 12 pair”。
  目前更现实的 bring-up 顺序是：
  - 先保留旧 `bertmini` mapping
  - 在新硬件上只启用一部分 manager 跑通软件/infra
  - 等新的 mapping artifact 准备好，再扩大 manager 数

- 输出路径本身会扰动现象。
  以前已经出现过：
  - 中间标准输出把场景拖死
  - 改成文件输出能继续通过
  所以“加更多打印再看”不是默认正确动作。当前默认动作是保留少量粗日志，再按 segment 打开文件细日志。

## 稳定 workload 清单

- 主线回归：
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
  用途：验证主线是否仍能完成执行，并观察 mismatch 是否仍停在 `tensor=48`。

- 小 Linux smoke：
  `2026-03-20--06-16-29-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles`
  当前可采信 marker：
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`
  - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS`
  - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1`
  - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
  - `ALL_TESTS_PASS`
  - `COMMAND_EXIT_CODE="0"`

## 旧 blocker 只作为 regression 签名保留

- `2026-04-01` 的旧 hang 证据现在只作为“如果 future run 回退，再拿来对照”的 regression 签名。
- 最关键的旧 capture 是：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/bertmini-b8-fileonly-sync6-run-manualmon3-20260401-192.168.1.44-host-watchdog-20260401T152744Z.guest-deep-log.txt`
- 它冻结的旧边界是：
  `segment=3 stage=0 tensor=6 page=124 ... submit-begin -> [prt-marker] dma`
- 只有当未来 fresh run 再次回到 hang，而不是 mismatch，才需要重新展开这条旧线。

## 恢复 mismatch 调查的顺序

1. 先修掉 host `pipeline_runtime` 当前全量构建里的现存 `-Werror` 阻塞。
2. 重新跑 host closure，刷新 `golden.*.bin`。
3. 重新 `marshal build` 和 `marshal install`。
4. 在 `infrasetup` / `runworkload` 之前，再做一次 image/rootfs freshness 校验。
5. 用当前冻结 workload 再跑 fresh FireSim 回归。
6. 每轮 run 后立刻回收 run farm。
