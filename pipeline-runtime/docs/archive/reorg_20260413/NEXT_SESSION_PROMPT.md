# Next Session Prompt

```text
你正在接手：

/home/ubuntu/chipyard

先按顺序阅读这些文件：

1. /home/ubuntu/chipyard/AGENTS.md
2. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/README.md
3. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/CURRENT_STATUS.md
4. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/blockers_and_lessons.md
5. /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/DECISIONS.md

新增固定 workflow，默认必须优先使用：

- 固定 profile：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
- 固定 workflow：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
- 固定 runbook：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pairdummy_sbus128_reusable_workflow_20260412.md`

除非明确在做新的 debug profile bring-up，否则不要再手工拼 env + 手工跑 wrapper。

新增硬约束，必须保留：

1. 后续所有 FireMarshal `build` / `install`，**不能只看 exit code**。
   - 对
     `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
     这条 workload，
     每次都必须确认 image freshness check 通过之后，
     才允许继续 `infrasetup` / `runworkload`。
   - 当前自动校验入口是：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh`
   - 当前 `scripts/firemarshal-tmux-run.sh`
     已对这条 workload 的 `build` / `install`
     自动跑这项检查；如果检查失败，tmux wrapper 必须整体判失败。
2. 这项 freshness check 至少要比对 image 内三项内容与 host 当前产物完全一致：
   - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
   - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
   - `/firemarshal.env`
3. 如果 image 内任一项与 host 当前源码/二进制不一致，
   不要继续怀疑 FireSim / F2 / runtime 新停点；
   先把问题归类为 **stale image / wrong source layer**，
   重新 `marshal clean -> build -> install`，直到 freshness check 通过为止。
4. 调试过程中要持续更新这两个记录文件，不要把最新结论只留在终端历史里：
   - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/CURRENT_STATUS.md`
   - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/NEXT_SESSION_PROMPT.md`
5. 新增硬约束：
   - 后续每一轮调试必须写入独立的时间戳记录文件
   - 记录目录固定为：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/`
   - 不要复用同一个记录文件追加多轮内容
   - 每轮至少记录：
     - 改动
     - 调试命令
     - 配置
     - 新日志
     - 新卡点
     - 卡点分析
     - 下一步
6. 新增硬约束：
   - 后续所有 FireMarshal `build/install`
     在执行前都必须先：
     `cd /home/ubuntu/chipyard/sims/firesim`
     然后：
     `source sourceme-manager.sh --skip-ssh-setup`
   - 这里的约束要按更严格版本理解：
     **任何编译动作之前**都必须先满足这一步，
     不只是 wrapper 里那次 `marshal build/install`
   - 后续如果要人工编译 workload / binary / image，
     也必须先这么做，然后再回 repo 根目录执行：
     `source env.sh`
   - 这条约束现在已经固化到：
     `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
     里。接手时不要再绕过这个 wrapper 直接裸跑 `marshal`。

补充一个比下面所有描述都更新的 2026-04-12 新状态。
如果接手时还没来得及看别的，先看这段：

0.0.29. 更新到 `2026-04-13 04:42 UTC`
   - 新增独立调试记录目录：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/`
   - 当前首份记录：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260413T044223Z.md`
   - 对最新 timeout capture
     `pairdummy-sbus128-runworkload-20260413-040643 / 192.168.1.123 / 20260413T042704Z`
     的静态复核后，必须用这段覆盖掉“还只停在 `subbatch=2 done` 之后 rr-acquire”那种较粗说法：
     1. checkpoint 已经明确推进到：
        `worker stage=0 checkpoint=before-build-stage-task subbatch=3`
     2. 但没有出现：
        `worker stage=0 checkpoint=after-build-stage-task subbatch=3`
     3. 因而当前主怀疑路径已经收敛成：
        `build_stage_task_desc -> build_stage_conv_desc -> stage_prepare_exec_views`
   - 同时要记住一个新的系统性风险：
     1. 当前 host watchdog 只把
        `uartlog / guest_log / guest_sparse / status / runner stage`
        的 size 增长当作 progress
     2. **没有**把
        `guest checkpoint log`
        的增长算作 progress
     3. 因此如果 guest 主要只在 checkpoint 文件里前进，
        watchdog 可能误判 idle timeout，
        然后把 run 提前杀掉
   - 另外，从当前 `ours2` 编排文件可直接确认：
     1. `segment=0 stage=0`
        的 fixed tensor 为
        `1000000`
        和
        `1000001`
     2. `tensorUseLazyFetch: [0, 0, 0, 0]`
     3. 所以每个 subbatch 都会重新装载固定张量：
        - `1000000 -> 1` 页
        - `1000001 -> 64` 页
   - 接手后的正确下一步不是回头查旧 export 卡点，
     而是：
     1. 让 watchdog 把 checkpoint 文件也计入 progress
     2. 在
        `stage_prepare_exec_views / stage-fixed-load`
        周围补低扰动 sparse
     3. 重新走 fresh image / fresh host / fresh run

0.0.28. 更新到 `2026-04-13 04:11 UTC`
   - `03:53 UTC`
     那轮结论之后，
     已经真正执行了：
     `收窄 checkpoint -> image-closure -> launch -> infrasetup -> run`
   - 最新源码改动：
     1. `prt_dma.c`
        已撤掉
        `stage0 tensor=2`
        的高频 checkpoint：
        - `dma_should_checkpoint_doneflag_tok()`
          不再覆盖
          `tensor=2`
        - `dma_should_checkpoint_submit_wait_tok()`
          不再覆盖
          `tensor=2`
     2. 改为低扰动页级 sparse：
        - `dma-export-host ... phase=page-submitwait-begin`
        - `dma-export-host ... phase=page-submitwait-end`
        - 当前覆盖：
          `stage0 tensor=2`
          的
          `page=0`
          / 每
          `8`
          页 / 最后一页
   - 新 image-closure 已完整 PASS：
     - runtime-binary sha256：
       `3cd0a9ed498bc6f9ffc0efe3de0800507b09eca07013ba876c5a0597e318a180`
     - image sha256：
       `0891c9ac71f13534fe078899ab924c079ffa7dfeefbadeb1a7bc59575fd3acde`
   - 新 host / 新 run：
     - instance：
       `i-077f81aa1b7f11cb7`
     - private ip：
       `192.168.1.123`
     - run session：
       `pairdummy-sbus128-runworkload-20260413-040643`
     - manager log：
       `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-13--04-06-44-runworkload-19UOR7UK1VBKVSMW.log`
     - watchdog log：
       `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260413-040643.host-watchdog.log`
   - 新 host 已确认：
     1. `infrasetup`
        完成
     2. `+check-fingerprint`
        通过，
        输出
        `FireSim fingerprint: 0x46697265`
     3. 私网
        `remote-freshness`
        PASS
   - 但截至 `04:11 UTC`
     ，这轮 live run 还没有进入 runtime 主体：
     1. `heartbeat`
        已到
        `4606152876, 261`
     2. `uartlog`
        仍主要是 Linux kernel boot 输出
     3. guest image 内仍未出现：
        - `/root/pipeline-runtime-debug/bertmini-batch8.status`
        - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper.stage`
        - `/root/pipeline-runtime-debug/bertmini-batch8.log`
   - 因而接手后的第一动作不是再改代码，
     而是继续 monitor 当前 live run：
     1. 等
        `/root/pipeline-runtime-debug`
        出现
     2. 核对新的页级 sparse 是否生效
     3. 再判断是否越过旧
        `subbatch=2`
        blocker，
        以及
        `program-begin -> post-fence`
        人工冻结点是否消失

0.0.27. 更新到 `2026-04-13 03:53 UTC`
   - 不要再把当前主线 blocker 认成旧的
     `stage0 subbatch=2 compute-done -> c2 export`
     了。
     这一点已经被当前 live run 明确越过：
     - instance：
       `i-05425478faec8f72c`
     - private ip：
       `192.168.1.19`
     - session：
       `pairdummy-sbus128-runworkload-20260413-033712`
   - 当前 live 关键证据：
     1. sparse log
        已推进到：
        - `worker stage=0 subbatch=6 done`
        - `worker stage=0 subbatch=7 begin`
        - 8 个
          `oc-split-pointwise`
          tile
          完成
        - 随后再次进入
          `tensor=2`
          export DMA
     2. `checkpoint.log`
        已反复出现：
        `wait-fence-done ... hw_done=1`
        ，说明旧的 export wait 路径确实已经通过
     3. 但随后：
        - `heartbeat`
          继续从
          `12466795137, 675`
          增长到
          `14062688059, 756`
        - `bertmini-batch8.log`
          固定停在
          `369073`
          字节
        - `bertmini-batch8.checkpoint.log`
          固定停在
          `2751866`
          字节
     4. checkpoint 尾部稳定停在：
        `dma stage=0 checkpoint=program-begin tensor=2 src=0x40603000 dst=0x103918c00 bytes=1024 done_pa=0x102277000`
        并且后面始终没有：
        `program-post-fence`
   - 当前最重要的判断更新：
     - 旧 blocker
       已经越过
     - 新冻结点更像是
       **高频 checkpoint/file-log 调试路径本身诱发的人工冻结点**
       ，而不是 export DMA 原始功能点仍未修通
   - 接手后不要先去改早期 boot / hugetlb / runner sync。
     应先做：
     1. 收窄
        `stage0 tensor=2`
        的 checkpoint 覆盖面
     2. 保留低扰动 sparse，
        只盯
        `subbatch=7`
        附近和
        `program-begin/program-post-fence`
        边界
     3. 结束当前 run，
        重新走固定 workflow：
        `image-closure -> launch -> infrasetup -> run`

0.0.26. 更新到 `2026-04-13 03:29 UTC`
   - 上一轮 live run 已经结束，不要再继续 monitor 那个实例：
     - instance：
       `i-02b3f31f5aecbd73b`
     - private ip：
       `192.168.1.185`
     - session：
       `pairdummy-sbus128-runworkload-20260413-025728`
   - 这轮不是人工中断，而是 host watchdog 在
     `2026-04-13 03:17:46 UTC`
     采到稳定冻结后，
     于
     `2026-04-13 03:17:49 UTC`
     自动调用了
     `terminaterunfarm --forceterminate`
   - 关键证据：
     1. `heartbeat`
        继续涨到
        `22987944430, 1203`
     2. `guest_sparse`
        固定停在
        `282695`
        字节超过
        `624s`
     3. timeout capture：
        `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-runworkload-20260413-025728-192.168.1.185-host-watchdog-20260413T031746Z.*`
        中，
        `guest-sparse-log.txt`
        尾部仍是：
        - `worker stage=0 subbatch=2 compute-done`
        - 后接若干
          `rr-acquire-inner ...`
          / `rr-acquire-wrap phase=after-call`
     4. 仍然没有：
        `worker stage=0 subbatch=2 done`
   - 当前主线最新静态整改已经在源码里：
     1. `prt_dma.c`
        已把高扰动 checkpoint 缩回：
        - `doneflag-acquire-*`
          继续保留给
          `stage0`
          固定 tensor，
          并新增
          `tensor=2`
        - `program/wait`
          类 checkpoint
          现在只盯
          `stage0 tensor=2`
     2. `prt_scheduler.c`
        新增窄门控 sparse：
        - `c2-export ... phase=dispatch`
        - `c2-export ... phase=copy-begin`
        - `c2-export ... phase=copy-end`
        - `c2-export ... phase=retire`
     3. `prt_dma.c`
        新增首个 export chunk 的窄门控 sparse：
        - `dma-export-host ... phase=first-chunk-submitwait-begin`
        - `dma-export-host ... phase=first-chunk-submitwait-end`
     4. `scripts/firesim-prt-host-watchdog.sh`
        默认已补齐：
        - `wrapper.stage`
        - `wrapper-proc.stage`
        - `runner.stage`
        - `runner-early.stage`
        - `runner-proc.stage`
        - timeout 时自动抓
          `checkpoint.log`
   - 下一步不要回头再排 shell / boot / old sync blocker。
     直接按固定 workflow 重新跑：
     `image-closure -> launch -> infrasetup -> run`
   - 注意：
     我尝试先本地
     `make rerocc_pipeline_runtime-linux`
     做编译级检查，
     但当前 host 缺少
     `riscv64-unknown-linux-gnu-gcc`
     / `riscv64-linux-gnu-gcc`
     ，所以这一步需要交给固定 FireMarshal image-closure 闭环来验证

0.0.25. 更新到 `2026-04-13 03:06 UTC`
   - 同一 live run 还在继续，不要 terminate：
     - instance：
       `i-02b3f31f5aecbd73b`
     - private ip：
       `192.168.1.185`
     - session：
       `pairdummy-sbus128-runworkload-20260413-025728`
   - 截至 `03:06 UTC` 的最重要结论：
     1. 这轮已经**明确越过**旧 blocker
        `after-resolve-manager-layout`
     2. `wrapper.stage`
        已到
        `before-child-wait pid=132`
     3. `runner-early.stage`
        已到
        `before-runner-enter`
     4. `runner.stage`
        已到：
        - `after-runner-enter batch=8`
        - `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
        - `before-prepare-hugetlb`
        - `after-prepare-hugetlb`
        - `before-bin method=ours2`
        - `after-bin-spawn method=ours2 pid=196`
     5. 因而可以确认：
        上一轮修掉的
        runner-stage foreground `sync`
        确实是有效修复，
        这轮没有再卡在该点
   - 当前 live 前沿不是新的静态死锁：
     1. `runner-proc.stage`
        里父 shell
        `pid=132`
        的
        `wchan=do_wait`
        现在只是正常等待子进程
        `rerocc_pipeline_runtime-linux`
     2. 子进程
        `pid=196`
        已被采到
        `State: R (running)`
     3. `bertmini-batch8.log`
        已进入真实执行：
        - `conv-sync-strided`
        - `pointwise-matmul-fallback`
        - `fence-end rc=0`
        - `worker stage=0 subbatch=2 compute-done`
     4. `bertmini-batch8.checkpoint.log`
        已快速增长，
        出现：
        - `wait-fence-done ... hw_done=1`
        - `doneflag-acquire-*`
        - `after-build-stage-task subbatch=2 rc=0 op=1 tile_count=8 mgr0=0`
     5. `heartbeat.csv`
        继续增长到
        `10718708876, 585`
   - 接手后的第一步不再是修代码，而是继续监控当前 live run：
     1. 先看它是否直接跑通
     2. 如果后面再停住，
        只围绕当时最新的
        segment/stage/checkpoint
        前沿做定点排查
     3. 不要再回退去重查已经通过的
        `runner stage sync`
        / `before-prepare-hugetlb`
        早期路径

0.0.24. 更新到 `2026-04-13 03:00 UTC`
   - 新 fresh host：
     - instance：
       `i-02b3f31f5aecbd73b`
     - private ip：
       `192.168.1.185`
   - 固定流程已经完成：
     - `image-closure`
     - `launchrunfarm`
     - `infrasetup`
     - `remote-freshness`
     - 手工
       `./FireSim-f2 +slotid=0 +check-fingerprint`
       ，输出
       `FireSim fingerprint: 0x46697265`
   - 当前本地 hash：
     - image：
       `a76a315f2a4ed9551286ba05b5a167c21c6120055420e40bc1f1f9433dbb07db`
     - `pairdummy-bin`：
       `d663027f73cf73352bfcdb0614a3129cc68b7e04e88e9538852a77afef2fc1cc`
     - runner-script：
       `691192544e250c7e3d509a4015136ac49dec2c8cc41e2d9c9ec9cd04de52efc0`
     - `firemarshal.env`：
       `09703404918c9e4725d113ca4de3e6d09e23c0a8c4584476c21b04e7f38c79a1`
     - runtime-binary：
       `da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
   - 当前 live run：
     - session：
       `pairdummy-sbus128-runworkload-20260413-025728`
     - manager log：
       `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-13--02-57-29-runworkload-VRH49UVJTXBNLB5Y.log`
     - result dir：
       `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-13--02-57-29-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync`
   - 截至 `03:00 UTC` 的 live 观察结论：
     1. `remote-freshness` 已 PASS，说明当前 run host 上的 image / runner-script / env / runtime-binary 与本地一致
     2. `heartbeat.csv` 持续推进，最近已到
        `2428237454, 140`
     3. `uartlog` 也在增长，约从
        `6128`
        字节涨到
        `7480`
        字节
     4. 所以当前不是“1 秒假完成”或 host driver bundle 再次空退出
     5. 但当前 `uartlog` 仍只到 Linux boot 早期
     6. 对 run host 上的 workload image 做
        `debugfs -R "ls -l /root"`
        可见 overlay 已存在；
        但
        `debugfs -R "ls -l /root/pipeline-runtime-debug"`
        仍报
        `File not found by ext2_lookup`
     7. 因而截至目前只能判断：
        guest 正在推进，但还没进入创建
        `pipeline-runtime-debug`
        的用户态脚本
   - 接手时不要误判：
     1. `pipeline-runtime-debug` 目录暂不存在，
        不等于 host 假完成
     2. 先看
        `heartbeat.csv`
        和
        `uartlog`
        是否继续增长
     3. 只有在 heartbeat 也停住、或 manager 异常完成时，才回到 host/driver 方向
   - 接手后的第一步：
     1. 继续监控当前 live run
     2. 一旦出现
        `/root/pipeline-runtime-debug`
        ，马上读：
        - `bertmini-batch8.status`
        - `bertmini-batch8.wrapper.stage`
        - `bertmini-batch8.runner-early.stage`
        - `bertmini-batch8.runner.stage`
        - `bertmini-batch8.log`
        - `bertmini-batch8.checkpoint.log`
     3. 核心判断点：
        是否越过旧前沿
        `after-resolve-manager-layout`
   - 当前更旧但仍重要的一条结论：
     上一轮 fresh host `192.168.1.89`
     上，真实 blocker 已确认是
     `run_rerocc_pipeline_runtime_bertmini.sh`
     中新增的
     runner-stage foreground `sync`
     ；
     这部分源码修复已经落地，不要回退成旧实现。

0.0.23. 更新到 `2026-04-13 02:46 UTC`
   - 这轮 fresh host：
     - instance：
       `i-01e0ba8475adefe51`
     - private ip：
       `192.168.1.89`
     已经完成：
     - `launchrunfarm`
     - `infrasetup`
     - `remote-freshness`
     - 手工 host 预检：
       - `FireSim-f2` 非零
       - `./FireSim-f2 +slotid=0 +check-fingerprint` 通过
   - 这轮 run：
     `pairdummy-sbus128-runworkload-20260413-023601`
     不是 1 秒假完成；
     `heartbeat.csv`
     一直在涨。
   - 最关键的新事实：
     1. 不要再把
        `uartlog`
        停在早期 boot
        + 单独
        `debugfs cat <status/stage>`
        空输出
        直接解释成
        “guest 还没进 wrapper”
     2. 这轮改用
        `debugfs -R "ls -l /root/pipeline-runtime-debug"`
        后，已经确认 image 内以下文件都存在且非零：
        - `bertmini-batch8.status`
        - `bertmini-batch8.wrapper.stage`
        - `bertmini-batch8.wrapper-proc.stage`
        - `bertmini-batch8.runner-early.stage`
        - `bertmini-batch8.runner.stage`
        - `bertmini-batch8.runner-proc.stage`
        - `bertmini-batch8.log`
     3. 真正前沿是：
        - `status=running`
        - `wrapper.stage` 到 `before-child-wait pid=132`
        - `runner-early.stage` 到 `before-runner-enter`
        - `runner.stage` 到 `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
        - sparse log 只到 `[bertmini] runner-enter batch=8 methods=ours2`
        - `checkpoint.log` 为空
   - 真正 blocker：
     - `runner-proc.stage`
       显示 shell 进程
       `State: S (sleeping)`
       且
       `wchan=do_wait`
     - 在
       `after-resolve-manager-layout`
       之后，本该出现的
       `runner-config ...`
       没有出现
     - 结合
       `run_rerocc_pipeline_runtime_bertmini.sh`
       源码，
       最合理结论是：
       shell 卡在
       `write_runner_stage()`
       里的前台
       `sync`
       上
     - 即：
       **这轮“卡点前移”是我们新增的 runner-stage foreground sync 自己把 guest 拖住了**
   - 这轮还额外确认：
     - 本地与 host 上
       `...pairdummy-bin`
       sha256
       一致，都是
       `5ad5bed580cb31b952d3bcdd0c6ba76653c589f88d485b5cc6f14424fe701645`
     - 所以这轮不是
       `remote image 新，但 +prog0 -bin 旧`
       的错配
   - 已做修复：
     1. 文件：
        `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
     2. 文件：
        `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
     3. 把
        `runner_stage_sync()`
        从前台阻塞
        `sync`
        改成非阻塞后台 flush
     4. 固定 profile：
        `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
        里的
        `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE`
        已从 `1`
        改成 `0`
     5. overlay env：
        `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/firemarshal.env`
        也同步改成 `0`
   - 当前状态：
     1. 旧 live run
        已通过
        `pairdummy_sbus128_workflow.sh terminate`
        回收
     2. 当前正在重做：
        `pairdummy_sbus128_workflow.sh image-closure`
   - 接手时下一步固定为：
     1. 等新的 `image-closure` PASS
     2. 按固定 workflow 重新：
        `launch -> infrasetup -> current-private-ip -> remote-freshness -> run`
     3. 新 run 观察时，
        不要再先用
        `debugfs cat`
        的空输出判定“还没进 wrapper”
        ；
        先做
        `debugfs -R "ls -l /root/pipeline-runtime-debug"`
     4. 如果新 run 能越过
        `after-resolve-manager-layout`
        并出现
        `before-prepare-hugetlb`
        或更后面的 sparse/checkpoint，
        就说明这次前移 blocker 的根因确实是
        `runner stage sync`
        的前台刷盘
     5. 然后继续回到
        `hugetlb / alloc-contig / runtime`
        主线

0.0.22. 更新到 `2026-04-13 01:48 UTC`
   - 本轮已经修掉
     `pairdummy_sbus128_workflow.sh`
     里一个流程 bug：
     `current-private-ip / remote-freshness / run`
     之前还写死按
     `tag:fsimcluster=firesim`
     查实例；
     现在改成从 runtime yaml 读取
     `run_farm_tag`
     。
   - 当前旧实例：
     - instance：
       `i-0f338e9910fbae43d`
     - private ip：
       `192.168.1.163`
     不要再继续复用。
   - 在这台旧实例上，
     本轮已经确认：
     1. remote freshness 一开始失败，不是 stale runtime，而是 remote `.img`
        已被上一轮 guest 写脏。
        正确补救是：
        重新把本地 pristine `.img`
        和 `-bin`
        覆盖到
        `/home/ubuntu/sim_slot_0/`
        ，然后再做 remote freshness。
     2. 覆盖后 remote freshness 已重新严格 PASS：
        - runtime sha256：
          `da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
        - local/remote image sha256：
          `86515fc1dadb8278db468c495126c2e85f6c406c0bcadd8ba08e8857471c9311`
     3. 但新的 run
        `pairdummy-sbus128-runworkload-20260413-013846`
        几乎立即“成功结束”，
        实际没有真正起 guest。
        `uartlog`
        只有 `script started/done`
        两行，没有 heartbeat，没有 guest 文件日志。
     4. 现场根因不是 runtime，
        而是远端
        `/home/ubuntu/sim_slot_0/FireSim-f2`
        和多份 `.so`
        当时都是 `0` 字节；
        这样
        `./FireSim-f2`
        会像空脚本一样直接 `exit 0`
        ，从而把 manager 的 preflight / runworkload 都骗成成功。
     5. `driver-bundle.tar.gz`
        本身内容正常；
        手工在
        `/home/ubuntu/sim_slot_0`
        重新
        `tar -xvzf driver-bundle.tar.gz`
        后，
        `FireSim-f2`
        和 `.so`
        已恢复成正常非零大小。
     6. 真正的 preflight 随后暴露出更底层问题：
        `timeout ... ./FireSim-f2 +slotid=0 +check-fingerprint`
        仍报
        `AFI in Slot is not in READY state !`
     7. 在这台 host 上：
        - `fpga-load-local-image -S 0 -I agfi-0dc8dcfa4c7735f40 -A`
          返回 `0`
          但没有任何输出
        - `fpga-describe-local-image -S 0 -R -H`
          也是 `rc=0`
          但空输出
        - reboot 实例之后，问题仍然存在
     8. 因而这台旧实例当前应归类为：
        **复用 host 的 FPGA 管理面失效**
        ，不是 pipeline-runtime 新卡点。
   - 接手时下一步固定为：
     1. 坏实例
        `i-0f338e9910fbae43d`
        已通过
        `pairdummy_sbus128_workflow.sh terminate`
        进入
        `shutting-down`
     2. 当前新的 fresh launch
        已在跑：
        `pairdummy-sbus128-launchrunfarm-20260413-015011`
        ，但截至这次记录仍卡在 AWS
        `insufficient capacity`
     3. 不要再回退去复用旧 host；
        继续等 fresh host
     4. 拿到 fresh host 后，
        回到固定流程：
        `infrasetup -> current-private-ip -> remote-freshness -> run`
     5. 在 fresh host 上，
        真正开始 guest 调试前，必须额外手工确认两件事：
        - `/home/ubuntu/sim_slot_0/FireSim-f2`
          非零大小
        - `timeout ... ./FireSim-f2 +slotid=0 +check-fingerprint`
          能通过
     6. 只有通过这两步之后，
        才值得继续追
        `bertmini-batch8.checkpoint.log`
        与 `alloc-contig/hugetlb`
        前沿。

0.0.21. 更新到 `2026-04-13 01:09 UTC`
   - 旧 live run：
     - session：
       `pairdummy-sbus128-runworkload-20260413-005007`
     - instance：
       `i-04bac52cc7df377ac`
     - private ip：
       `192.168.1.106`
     已通过
     `pairdummy_sbus128_workflow.sh terminate`
     停掉。
   - 对这轮旧 live 的重新核对，已经确认：
     1. 不能再对 run host 的 `/dev/root` 做 `debugfs`；
        正确目标必须是
        `/home/ubuntu/sim_slot_0/*.img`
     2. 不能把
        `/home/ubuntu/sim_slot_0/*-bin`
        当成 image 内 runtime freshness 的最终依据；
        真正要核对的是 image 内
        `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
   - 这轮旧 live 的 decisive 结论：
     - guest image 内 runtime binary sha256
       的确是
       `f6516aa02acadfe3736d83d1524340e0c88916054db233f32e7523204ed613d4`
     - image 内 binary 已包含：
       - `spm-pt alloc-contig hugetlb-check`
       - `spm-pt hugetlbfs open begin`
       - `spm-pt hugetlbfs ftruncate begin`
       - `spm-pt hugetlbfs mmap begin`
       - `spm-pt probe progress`
     - 所以那一轮不是 stale image / stale runtime。
   - 同时这轮旧 live 还暴露了一个新的现场特征：
     - `bertmini-batch8.log`
       文件大小是
       `229376`
       ，正好页对齐
     - 文件末尾带大量 `NUL`
     - sparse log `mtime`
       停在 guest 时间
       `00:00:06`
     - `wrapper-proc.stage`
       停在 guest 时间
       `00:00:02`
     - 但 heartbeat 仍推进到约
       `13794329012, 739`
   - 当前更准确的判断：
     旧 live run 的新 binary
     的确跑到了
     `spm-pt alloc-contig begin`
     ，但主 sparse log 可能在这一前沿附近自己发生了 guest 文件追加/刷盘阻塞。
     因而“sparse log 最后一行”
     不再能直接等价成
     “代码真实停点”。
   - 针对这个新现象，
     本轮已经继续修改：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
     在以下点位新增
     `checkpoint`
     旁路断点：
     - `hugetlb-can-cover enter/exit`
     - `hugetlb meminfo enter/exit`
     - `alloc-contig after-cover`
     - `alloc-contig before/after hugetlbfs`
     - `hugetlbfs open/ftruncate/mmap begin/end`
   - 当前新本地 runtime sha256：
     `da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
   - 当前新 `image-closure`
     已 PASS，freshness 明确显示：
     `runtime-binary sha256=da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
   - 当前新本地 image sha256：
     `5697b65055f461028c70bc80708211309813035e4af902acd4b9b2e8cc312bda`
   - 接手时下一步固定为：
     1. `pairdummy_sbus128_workflow.sh launch`
     2. `pairdummy_sbus128_workflow.sh infrasetup`
     3. `pairdummy_sbus128_workflow.sh current-private-ip`
     4. `pairdummy_sbus128_workflow.sh remote-freshness <private-ip>`
     5. `pairdummy_sbus128_workflow.sh run <private-ip>`
     6. 新 run 监控时，不能只看
        `bertmini-batch8.log`
        ；
        必须同时抓：
        - `/root/pipeline-runtime-debug/bertmini-batch8.log`
        - `/root/pipeline-runtime-debug/bertmini-batch8.checkpoint.log`
        - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper-proc.stage`
     7. 如果 sparse log 仍停在
        `alloc-contig begin`
        ，但 checkpoint log 继续推进，
        就把 blocker 归类为：
        “主 sparse log 文件写/刷盘路径卡住”，
        而不是 hugetlb 逻辑本身没有继续执行。

0.0.15. 截至 `2026-04-12 16:41 UTC`
   - AWS 上没有运行中的 `f2.6xlarge` FireSim 实例。
   - 当前只剩一个旧 tmux 会话
     `pairbert-b8-d12s128-term39`
     ；
     这不代表 run farm 仍活着，只是旧 `terminaterunfarm --forceterminate`
     的 shell 残留。
   - 因而下一步不要接着追旧 run；
     直接从固定 workflow 重新开始：
     1. `pairdummy_sbus128_workflow.sh image-closure`
     2. `pairdummy_sbus128_workflow.sh launch`
     3. `pairdummy_sbus128_workflow.sh infrasetup`
     4. `pairdummy_sbus128_workflow.sh current-private-ip`
     5. `pairdummy_sbus128_workflow.sh remote-freshness <private-ip>`
     6. `pairdummy_sbus128_workflow.sh run <private-ip>`

0.0.16. 更新到 `2026-04-12 16:47 UTC`
   - 新 fixed-workflow 闭环已经重新拉起，不再处于“无 live run”状态。
   - 当前 live：
     - instance：
       `i-09ce54845adc28eac`
     - private ip：
       `192.168.1.195`
     - AGFI：
       `agfi-0dc8dcfa4c7735f40`
     - run session：
       `pairdummy-sbus128-runworkload-20260412-164743`
   - 当前 sha：
     - image：
       `e7aa13adfffbb4e6a34b66a5497a21f37fc73297e8778aa74c2b7d70c2e0fc30`
     - runtime：
       `575414015130d8a0f7af37ad35a31905cec601ea59038065f8f874eb944ab848`
     - firemarshal/fileonly-wrapper：
       `0d3aa9398bfbf0fa6bec0a269be9669261ba1e9b5c48e3be6528a31c94e6366e`
     - runner：
       `2e2717d1d4502754c23c1d11f9781b1a1df0ff58abc649b89904f13f4e5b12e3`
     - firemarshal env：
       `26272ae0e24844d9ee4f86047a198bf8a6f8f4141a9af98ddb0f86278ff7033c`
   - `image-closure`、`launchrunfarm`、`infrasetup`、`remote-freshness`
     都已 PASS。
   - 接手时如果 run 还活着，继续监控
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260412-164743.host-watchdog.log`
     和新的 capture 文件；
     不要回退成只盯 `uartlog`。

0.0.17. 更新到 `2026-04-12 17:01 UTC`
   - 当前 live run 进一步确认已越过：
     - `before process memory lock`
     - `after process memory lock`
     - `dma-completion-pool before/after-prefault`
     - `dma-completion-pool before/after-mlock`
     - `artifacts mapping parse end`
     - `init validate-artifacts end`
   - 当前最新前沿不是 boot，也不是 artifact parse，而是：
     `synthetic-model alloc before-prefault path=(none) ... mode=write-preserve`
   - 现场特征：
     - heartbeat 继续涨，已到约
       `15026934005, 801`
     - 但 sparse log 在该点后不再增长
   - 当前判断：
     这更像是
     `prefault_and_lock_blob()`
     的逐页触碰阶段太慢或停住，
     不是 `mlockall` 解释问题，也不是旧的 Linux boot 问题。
   - 下一步：
     给
     `prefault_and_lock_blob()`
     补 1MiB 粒度 progress log，
     然后 terminate 当前 run，按固定 workflow 重跑。

0.0.18. 更新到 `2026-04-12 17:04 UTC`
   - `prefault_and_lock_blob()`
     已补上每 `1 MiB`
     一条的
     `prefault-progress`
     日志。
   - 对应新 runtime sha256：
     `7bf68548f50636875cd536535edc7430f7f8cba324b58fa6d7d446a4ce53d4a9`
   - 新一轮 `image-closure`
     已 PASS，freshness 也已通过。
   - 当前没有继续推进到 `infrasetup/run`
     的原因不是代码，而是 AWS 容量：
     `launchrunfarm`
     正因
     `f2.6xlarge`
     insufficient capacity
     自动循环重试。
   - 接手时先看
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-launchrunfarm-20260412-170436.pane.log`
     是否已拿到新实例；
     一旦拿到，继续原固定 workflow：
     `infrasetup -> remote-freshness(private ip) -> run`

0.0.19. 更新到 `2026-04-12 17:28 UTC`
   - 当前 live run：
     - session：
       `pairdummy-sbus128-runworkload-20260412-171219`
     - instance：
       `i-00eefd089273cd848`
     - private ip：
       `192.168.1.217`
   - 这轮最关键的新结论：
     `synthetic-model prefault + mlock`
     已被新日志明确证实能完整通过。
     image 内已经看到：
     - `prefault-progress` 从 `1 MiB` 到 `16 MiB`
     - `synthetic-model alloc after-prefault`
     - `synthetic-model alloc after-mlock rc=0 errno=0`
   - 当前新的真实前沿已经前移到：
     - `segment=0 action-alloc-spm begin`
     - `spm-xlate-ctx alloc begin`
     - `spm-pt alloc-contig begin`
     - `spm-pt alloc-contig hugetlb-check need_pages=1 total_pages=1 free_pages=1 can_try=1`
     - `spm-pt hugetlbfs begin req_bytes=2097152 alloc_bytes=2097152 probe_page=4096 path_dir=/dev/hugepages`
   - 当前静态判断：
     `prt_page_table.c`
     里 hugetlb page-table 分配路径把大量工作隐藏在两层“日志盲区”里：
     1. `mmap(... MAP_POPULATE ...)`
     2. `probe_phys_contig_range()` 开头整段 `memset(base, 0, bytes)`
     它们都不是语义必须项，因为后面的逐页触碰 + pagemap probe
     本来就会建立映射并验证物理连续性。
   - 本轮已经修改：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
     - 去掉 hugetlb anon / hugetlbfs 路径里的 `MAP_POPULATE`
     - 去掉 `probe_phys_contig_range()` 开头整段 `memset`
     - 新增：
       - `spm-pt hugetlbfs open begin/end`
       - `spm-pt hugetlbfs ftruncate begin/end`
       - `spm-pt hugetlbfs mmap begin`
       - `spm-pt probe progress`
   - 当前最新闭环 sha：
     - runtime：
       `f6516aa02acadfe3736d83d1524340e0c88916054db233f32e7523204ed613d4`
     - image：
       `6a620252ef42b8a45673ded3a9279366fbc8dc8d096d4b93a9ada9065663d1d5`
   - 这条修复不是“降级成普通匿名页”。
     必须保留的硬约束是：
     对需要多页物理连续的 `spm page-table` backing，
     不能退回普通匿名页；
     虚拟连续不等于物理连续。
   - 接手时下一步：
     1. 先用固定 workflow：
        `pairdummy_sbus128_workflow.sh terminate`
        停掉当前 live run
     2. 再执行：
        `image-closure -> launch -> infrasetup -> current-private-ip -> remote-freshness -> run`
     3. 新 run 重点看是否出现：
        - `spm-pt hugetlbfs open end`
        - `spm-pt hugetlbfs ftruncate end`
        - `spm-pt hugetlbfs mmap ok`
        - `spm-pt probe begin/progress/end`
        - `spm-pt pool chunk-add alloc-contig end`
        - `segment=0 action-alloc-spm end`

0.0.20. 更新到 `2026-04-12 17:42 UTC`
   - 上面 0.0.19 里的旧 live
     `i-00eefd089273cd848 / 192.168.1.217`
     已经用
     `pairdummy_sbus128_workflow.sh terminate`
     停掉。
   - 新一轮固定 workflow 已完成：
     - `image-closure`: PASS
     - `launchrunfarm`: PASS
     - `infrasetup`: PASS
     - `remote-freshness`: PASS
     - `runworkload`: 已启动
   - 当前新 live：
     - session：
       `pairdummy-sbus128-runworkload-20260412-173722`
     - instance：
       `i-079eae4f488d0d904`
     - private ip：
       `192.168.1.23`
   - 当前 host watchdog：
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260412-173722.host-watchdog.log`
   - 当前闭环 sha：
     - runtime：
       `f6516aa02acadfe3736d83d1524340e0c88916054db233f32e7523204ed613d4`
     - image：
       `6a620252ef42b8a45673ded3a9279366fbc8dc8d096d4b93a9ada9065663d1d5`
   - 这一刻的新 run 还在 Linux boot：
     - heartbeat 已到约
       `4446645349, 253`
     - host watchdog 还没 arm 到 guest `status`
     - 通过 `debugfs`
       查看 image 时，`/root/pipeline-runtime-debug`
       还没开始稳定出现文件
   - 接手时不要回退去重查旧实例，也不要误判为“新 hugetlbfs 修复无效”。
     当前更准确的状态是：
     新镜像、新 runtime、新实例都已经就位，
     只是 guest 还没 boot 到用户态 workload。
   - 接手后的第一步：
     继续监控：
     - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260412-173722.host-watchdog.log`
     - 私网 SSH `192.168.1.23` 上的：
       - `/home/ubuntu/sim_slot_0/heartbeat.csv`
       - `/home/ubuntu/sim_slot_0/uartlog`
     一旦 guest status/log 出现，立刻抓 image 内：
     - `/root/pipeline-runtime-debug/bertmini-batch8.status`
     - `/root/pipeline-runtime-debug/bertmini-batch8.log`
     核对新的 hugetlbfs 日志链是否出现：
     - `spm-pt hugetlbfs open begin/end`
     - `spm-pt hugetlbfs ftruncate begin/end`
     - `spm-pt hugetlbfs mmap begin/ok`
     - `spm-pt probe begin/progress/end`

0.0.14. 先把这轮新踩到的经验教训记成硬约束，不要再犯
   - 硬约束 1：
     对当前 pairdummy file-only workload，
     guest 真正执行入口是 image 内
     `/firemarshal.sh`
     ，不是
     `/root/firemarshal.sh`
     。
     以后做 freshness 判断、guest 执行路径推理、debug 入口核查时，都必须以
     `/firemarshal.sh`
     为准。
   - 硬约束 2：
     对当前 pairdummy file-only workload，
     freshness 不能只校验 runner / runtime / env；
     必须至少同时校验这 5 项：
     - `/firemarshal.sh`
     - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
     - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
     - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
     - `/firemarshal.env`
     只校验其中 3 项，会留下“镜像看似新、实际执行入口仍旧”的漏洞。
   - 硬约束 3：
     以后停止 FPGA run 默认就带：
     `terminaterunfarm --forceterminate`
     不要再裸跑会卡在
     `Type yes`
     的交互版。
   - 硬约束 4：
     以后通过
     `scripts/firemarshal-tmux-run.sh`
     跑 FireMarshal 时，
     workload json 必须按 repo 根目录语义解析。
     当前 wrapper 已补自动绝对路径归一化，但接手时仍要记住：
     **不要把 `software/firemarshal` 当前目录当成 workload json 的路径基准。**
   - 本轮为避免再次犯错，已经补了 tooling：
     - `scripts/firemarshal-tmux-run.sh`
       现在自动把 workload json 归一化为绝对路径
     - `verify_pairdummy_firemarshal_image_freshness.sh`
       现在做 5 项校验
     - `host-init.sh`
       已确保 file-only wrapper 真正打进 guest image
   - 本轮 fresh-image / fresh-run 新状态：
     - 旧 `run39`
       实例
       `i-0d15f38a246cf50bb`
       已用
       `terminaterunfarm --forceterminate`
       停掉
     - `clean10b`
       `PASS`
     - `build48`
       产出新 image 后，修正后的 local freshness 已 `PASS`
     - `install45`
       `PASS`
     - 当前本地 fresh image sha256 =
       `6556d1f0e073718382a293e96b40590e60378a0b6f6ff5deb87ed2159fffce4d`
     - `launch40`
       `PASS`
     - 新实例：
       `i-0f40fd006183931cb`
     - 私网：
       `192.168.1.24`
     - `infrasetup40`
       `PASS`
       - AGFI =
         `agfi-0dc8dcfa4c7735f40`
       - driver readiness preflight 已通过
   - 接手后的下一步：
     1. `192.168.1.24`
        上的 remote image freshness 已 `PASS`
     2. `run40`
        已启动：
        - session:
          `pairbert-b8-d12s128-run40`
        - pane log:
          `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run40.pane.log`
        - monitor log:
          `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run40.monitor.log`
     3. 截至当前记录时：
        - `heartbeat.csv`
          已推进到
          `8327660738, 458`
        - `uartlog`
          已推进到
          `running /etc/init.d/S99run`
        - host watchdog 已 arm 到 guest status
        - guest 文件已出现：
          - `bertmini-batch8.status`
          - `bertmini-batch8.log`
          - `bertmini-batch8.runner.stage`
          - `bertmini-batch8.runner-early.stage`
     4. 当前 `run40`
        的最新 runtime 前沿：
        - 已越过：
          - wrapper / runner 入口
          - `after-resolve-manager-layout`
          - `before/after-prepare-hugetlb`
          - `before-bin method=ours2`
          - `after-bin-spawn method=ours2`
          - `before process memory lock`
          - `after process memory lock`
          - `calling runtime_init`
          - `init page-table begin`
        - 当前 log 已推进到：
          - `init load-pipeline-yaml end`
          - `init validate-artifacts begin`
          - `artifacts mapping load begin`
          - `artifacts file pread chunk-begin ...`
     5. 这说明 fresh-image 主线已经明确越过旧 run39 当时看到的
        `before-mlock`
        位置；接手后继续追它是否会再次停在
        `synthetic-model`
        路径，还是推进到更后的
        `spm-pt`
        / segment 执行主线

0.0.13. 先纠正一个刚确认的新判断：当前 `run39` 跑的已经是 strict-hugetlb 修复后的 fresh image，而且这轮并没有回退卡死在 `hugetlb` 之前；它当前只是刚推进到 `S99run`
   - 当前实例：
     - instance id:
       `i-0d15f38a246cf50bb`
     - private ip:
       `192.168.1.193`
   - 本轮 fresh-image 闭环已经确认：
     - `marshal clean`
       `clean9`
       `PASS`
     - `marshal build`
       `build47`
       `PASS`
     - `marshal install`
       `install44`
       `PASS`
     - `launch39`
       `PASS`
     - `infrasetup39`
       `PASS`
     - remote freshness
       `PASS`
   - 当前 image / runtime / runner 哈希：
     - runner-script sha256 =
       `fdabde31d87372dfd82810ee79e0f9e8751e3af19d7b71400662ef1d491b25fa`
     - runtime-binary sha256 =
       `849e8e60f766acc00076b6b7e6e824a5fae3e32ce43d4f67dfa4192d7c9bf62d`
     - firemarshal-env sha256 =
       `2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
   - 当前源码里的 `spm-pt` 分配逻辑已经是：
     - `runtime_pt_chunk_bytes()`
       在 `spm_xlate_enable=1`
       时固定单 hugepage chunk
     - `alloc_contig_pt_storage()`
       中只要需要多于 1 个基础页的物理连续区间，就强制
       `require_hugetlb=1`
     - 分配顺序优先
       `hugetlbfs`
     - **拒绝多页普通匿名页 fallback**
     - 只有单页请求，才可能继续尝试匿名页
   - bertmini runner 已显式带：
     - `--spm-pt-require-hugetlb 1`
   - 截至当前最新现场：
     - `heartbeat.csv`
       已推进到
       `6788150238, 377`
     - `uartlog`
       已推进到：
       `running /etc/init.d/S99run`
     - image 内这些 guest 文件仍未出现：
       - `/root/pipeline-runtime-debug/bertmini-batch8.status`
       - `/root/pipeline-runtime-debug/bertmini-batch8.log`
       - `/root/pipeline-runtime-debug/bertmini-batch8.checkpoint.log`
       - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
       - `/root/pipeline-runtime-debug/bertmini-batch8.runner-proc.stage`
       - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper.stage`
       - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper-proc.stage`
   - 所以：
     - 这轮还不能说 runtime 已推进到
       `spm-pt hugetlbfs`
       分配点
     - 但也不能再把“卡点前移到 hugetlb 之前”当成当前事实；这轮目前只是 guest boot 才刚过
       `S99run`

0.0.12. 先纠正一个很重要的判断：对当前 `pairmanager_dummy16x16 / g12 d12 / sbus128 / mac256` 这条主线来说，`hugetlb` 前沿是一次真实回退，不只是 probe 更细
   - 必须把两类证据分开：
     - 更早那批
       `c2_g2_d2 / mac1024`
       的确不是同一套 artifact
     - 但当前这条
       `pairmanager_dummy16x16 / g12 d12 / sbus128 / mac256`
       主线，之前也确实已经推进到
       `segment=1 / stage=0`
       之后，并持续排查过更后的
       `export DMA / rr-acquire / doneflag`
   - 最直接的同-artifact 旧证据在：
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-nochkpt-noaudit-exportsubmithang-20260411/bertmini-batch8.log`
   - 这份 log 已明确出现：
     - `init ready segments=13`
     - `segment=0 action-alloc-spm begin action=1`
     - `spm-pt hugetlb anon ok req_bytes=2097152 ...`
     - `spm-pt pool chunk-added idx=0 ...`
     - `segment=0 action-alloc-spm end action=1`
     - `worker stage=0 subbatch=0 begin ...`
   - 因而：
     **同一套 `segments=13` 的 pairmanager/mac256/sbus128 artifact 之前能稳定越过 `alloc-spm`，并继续推进到后面的 DMA 主线**
   - 当前 `run37`
     的 fresh image 现场则已经收敛到：
     - `segment=0 action-alloc-spm begin`
     - `alloc-spm xlate-ctx begin`
     - `spm-pt alloc-contig begin req_bytes=2097152 ...`
     - `spm-pt hugetlb anon begin ...`
     - 然后不再出现
       `spm-pt hugetlb anon mmap ok / spm-pt pool chunk-added`
   - 所以这次对当前主线来说是**真实回退**：
     - 之前更后的
       `segment=1/stage=0/DMA`
       问题没有被证明消失
     - 只是现在先被更早的
       `spm-pt hugetlb`
       挡住了
   - 当前更可信的根因判断：
     - `spm-pt`
       默认先赌一次
       `2 MiB MAP_HUGETLB`
     - 但 guest 并没有稳定预留 hugepage 池
     - 所以这条路本来就是“有时碰巧成功”的脆弱实现，不应继续依赖
     - binary layout / probe / 前置分配稍有变化，就可能从旧的
       `hugetlb anon ok`
       退化成当前的
       `hugetlb anon begin` 之后冻结
   - 当前已在
     `pipeline-runtime/src/prt_page_table.c`
     落地一轮修复：
     - 只有在确认 hugepage 池足够覆盖当前请求时，才尝试 hugetlb
     - 否则不再默认把
       `spm-pt`
       chunk 无条件拉成
       `2 MiB`
   - 当前闭环状态：
     - 旧 `run37`
       实例
       `i-06bd49efe6f4a7950`
       已终止
     - `marshal clean`
       `pairdummy-prt-clean7`
       `PASS`
     - `marshal build`
       `pairdummy-prt-build45`
       `PASS`
     - `marshal install`
       `pairdummy-prt-install43`
       `PASS`
     - 当前新的本地 fresh hash：
       - runtime binary sha256 =
         `c23ee031563fa313e10c02d258312b296e5636fdedee83529c9d918565b78f67`
       - local image sha256 =
         `941ce48ea3341e05810aa62c7a5e5444ad07bc4c01c4264382da49b90c41503c`
     - 新 run farm：
       - `launch38`
         `PASS`
       - `infrasetup38`
         `PASS`
       - 实例：
         `i-089aa6c1a0ec02334`
       - 私网：
         `192.168.1.122`
     - 当前 remote freshness 已确认：
       - remote image sha256 =
         `941ce48ea3341e05810aa62c7a5e5444ad07bc4c01c4264382da49b90c41503c`
       - image 内 runtime binary sha256 =
         `c23ee031563fa313e10c02d258312b296e5636fdedee83529c9d918565b78f67`
       - image 内 env 继续确认：
         - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
         - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
         - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
         - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='1'`
         - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
     - 当前 `run38`
       已启动，host watchdog 已挂上
     - 截至当前记录时：
       - `heartbeat.csv`
         已推进到
         `4134132152, 236`
       - `uartlog`
         仍在 Linux boot 早期
       - image 内：
         - `bertmini-batch8.status`
         - `bertmini-batch8.log`
         - `bertmini-batch8.checkpoint.log`
         - `bertmini-batch8.runner.stage`
         - `bertmini-batch8.runner-proc.stage`
         - `bertmini-batch8.wrapper.stage`
         - `bertmini-batch8.wrapper-proc.stage`
         仍未出现
       - 因而现在还不能判断这版是否已经重新越过
         `spm-pt hugetlb`
         前沿；下一步仍是继续只用私网
         `192.168.1.122`
         监控 guest 文件系统里的这些文件

0.0.11. 新 runtime 修复后的 fresh image 已完成 `marshal clean -> build -> install -> launch35 -> infrasetup35 -> remote freshness -> run35` 闭环；当前 `run35` 仍在 fresh image 上继续跑，主观测面仍然必须是 guest 文件系统
   - 当前这轮只允许继续用私网：
     - 实例：
       `i-0f29265083202b0cb`
     - 私网：
       `192.168.1.91`
   - 本轮已经完成：
     - `marshal build`
       `pairdummy-prt-build42`
       `PASS`
     - `marshal install`
       `pairdummy-prt-install40`
       `PASS`
     - `launch35`
       `PASS`
     - `infrasetup35`
       `PASS`
     - remote image freshness
       `PASS`
     - `run35`
       已启动，当前仍在运行
   - 当前 fresh hash：
     - runtime binary sha256 =
       `6edccb9ee27538dae78a39bb9aa5d3538d14b99cf278e2ccd3b2a639dbe13e35`
     - local/remote image sha256 =
       `56dd00c8c7038df02e485019949eaf5fa620e290077591dd6504dbe488f809bc`
   - remote image freshness 还确认：
     - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='1'`
     - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
   - 这轮继续保留的硬约束：
     - 正式主观测面必须是 guest 文件：
       `/root/pipeline-runtime-debug/*.log`
     - 不能把 `uartlog`
       当主日志
     - `uartlog`
       仅用于辅助判断 Linux boot / `S99run`
       是否已经发生
   - 当前 `run35` 的现场状态：
     - `uartlog`
       已经推进到 Linux boot / rootfs mount：
       `Loaded platform drivers, booting from disk:`
     - `heartbeat.csv`
       已推进到：
       `5070443126, 284`
     - 但 image 内：
       - `bertmini-batch8.status`
       - `bertmini-batch8.log`
       - `bertmini-batch8.deep.log`
       - `bertmini-batch8.wrapper.stage`
       - `bertmini-batch8.runner.stage`
       仍为空
     - 这说明当前 guest 还没进入 runtime wrapper，
       还不能判断是否越过新的
       `spm-xlate flush / rr-acquire opcode=3`
       前沿
   - 接手后的正确下一步：
     1. 继续只用私网
        `192.168.1.91`
        监控：
        - `/home/ubuntu/sim_slot_0/heartbeat.csv`
        - `/home/ubuntu/sim_slot_0/uartlog`
        - 以及 remote image 内
          `/root/pipeline-runtime-debug/*`
     2. 一旦 image 内
        `bertmini-batch8.status/log`
        非空，就切回以 guest 文件为主判断 frontier
     3. 新一轮核心验证点仍然是：
        - 是否越过
          `rr-acquire wait stage=4294967295 manager=4 opcode=3 cfg=15`
        - 新的
          `exec-bind-flush mgr_count/mgr0..mgr3`
          是否只覆盖当前 stage manager

0.0.10. `run33` 已重新回到 `run31` 的真实 `tensor=1000001` doneflag 前沿，并确认这次不会再卡在 `doneflag`；`acquire-only` 最小 instrumentation 没有继续扰动 live 行为
   - 当前这轮只允许继续用私网：
     - 实例：
       `i-015aca2af0fddd113`
     - 私网：
       `192.168.1.54`
   - 这轮已经完成：
     - `launch33`
     - `infrasetup33`
     - remote image freshness check
       `PASS`
   - fresh hash：
     - runtime binary sha256 =
       `615c5c1dd7ab8082b13173a3484086812d9b6cdfa718028b57a433fde280123b`
     - local/remote image sha256 =
       `5fbf27d635d905f1a20541608900b06535b2123bea1654e15e43140e4f59ac57`
   - 这轮再次确认：
     - 正式主观测面仍是 guest 文件
       `/root/pipeline-runtime-debug/*.log`
     - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
     - wrapper 会把 stdout/stderr 直接写入
       `/root/pipeline-runtime-debug/bertmini-batch8.log`
       并周期性 `sync`
   - `run33`
     已明确走回旧基线：
     - `uartlog`
       出现：
       `running /etc/init.d/S99run`
     - host watchdog 在
       `hb='7086760756, 392'`
       看到：
       `guest-status arm observed`
     - 在
       `hb='7702674561, 425'`
       看到：
       `guest_sparse=1899`
     - 在
       `hb='8333328622, 460'`
       看到：
       `guest_sparse=197618`
     - 在
       `hb='9456459041, 520'`
       看到：
       `guest_sparse=215260`
     - 在
       `hb='10078775131, 552'`
       看到：
       `guest_sparse=311621`
   - 这组 heartbeat / sparse size 与
     `run31/run32`
     的旧轨迹基本重合，所以现在可以排除：
     - “新的 acquire-only 打点又把 frontier 拉回更早假前沿”
   - 当前这轮的决定性 live 证据：
     - sparse log 已重新出现：
       - `artifacts mapping parse done`
       - `artifacts mapping parse end`
       - `rr-acquire-inner phase=before-csr-write`
       - `rr-acquire-inner phase=before-return`
       - `rr-acquire-wrap phase=after-call`
     - checkpoint 文件已经非零，并明确出现旧真实前沿：
       - `tensor=1000001`
       - `doneflag-begin`
       - `doneflag-acquire-enter`
       - `doneflag-acquire-pool`
       - `doneflag-acquire-lock`
       - `doneflag-acquire-scan`
       - `doneflag-acquire-exit`
       - `doneflag-end`
       - `program-begin`
       - `program-post-fence`
       - `program-post-dst`
       - `program-post-src`
       - `wait-enter`
       - `wait-fence-done`
     - 并且 token
       已连续推进到
       `85 -> 96`
       ，每个 token 都能完整走完
       `doneflag -> program -> wait-fence-done`
   - 这说明：
     - **旧 blocker “卡在 `doneflag-begin` 到 `doneflag-end` 之间” 已不再成立**
     - **`acquire-only` 最小 checkpoint 方案足够轻，不会再把 live 行为扭曲回更早位置**
   - 接手后的正确下一步：
     1. 继续盯
        `run33`
        后续是否在更晚位置冻结
     2. 优先看是否重新收敛到历史上的更后期 frontier：
        - `worker stage=0 subbatch=4 compute-done`
        - `c2 export`
        - 或新的 `rr/program/wait`
          子阶段
     3. 如果再冻结，继续以 guest 文件和 checkpoint 为主，不回退到 UART 主观测面

0.0.9. `run27` 已确认越过 `S99run` 并进入 runtime 主体；`rr-acquire-wrap phase=after-call` 已出现，但这轮 checkpoint 默认仍关闭，所以当前只收敛到 “wrapper-after-call 之后”
   - 当前这轮只允许继续用私网：
     - 实例：
       `i-04fa58168cd6a59c2`
     - 私网：
       `192.168.1.191`
   - 这轮已经完成：
     - `launch27`
     - `infrasetup27`
     - remote image freshness check
       `PASS`
   - fresh hash：
     - runtime binary sha256 =
       `b9e66a5e6c654250ffda1d30004c0f9c3a910edcbec1e2845910d095773ac6da`
     - local/remote image sha256 =
       `8ed962f2aad93671ab7cdf778d997b6942b75b8a7d24a348a290d6b32ffb083a`
   - `run27` 已明确越过：
     - Linux boot
     - `running /etc/init.d/S99run`
     - wrapper / runner 启动
     - `segment=0` init / alloc / topology / spm-xlate
     - `rr-acquire-inner phase=before-return`
   - 当前最关键的新 live 证据：
     - image 内 `bertmini-batch8.runner.stage`
       已到：
       - `after-bin-spawn method=ours2 pid=207`
     - image 内 `bertmini-batch8.log`
       已出现：
       - `rr-acquire-inner phase=before-return stage=0 manager=0 opcode=2 cfg=0`
       - `rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1`
     - 同时：
       - `heartbeat.csv`
         从
         `584`
         长到
         `673`
       - `bertmini-batch8.log`
         大小稳定卡在
         `309824`
         字节
   - 这说明：
     - 新加的
       `noinline + wrapper-after-call`
       instrumentation
       已经真实进了 fresh image
     - 当前 freeze 已至少推进到：
       **`prt_rr_acquire_scope()` wrapper 内部 log 之后**
   - 但当前这轮不能过度下结论：
     - 因为 image 内
       `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
     - 所以还不能仅凭这轮 sparse log 判定：
       - caller 已执行到 `rr-acquire-end`
       - 或已经到 `doneflag`
       - 或已经到 `program/wait`
     - 更准确地说：
       **freeze 目前只收敛在 `wrapper-after-call` 之后，到下一个 caller 侧 checkpoint 之前**
   - 这轮 runner / wrapper 文件还说明：
     - `bertmini-batch8.status = state=running`
     - `bertmini-batch8.wrapper.stage`
       到了
       `before-child-wait`
     - `bertmini-batch8.runner-proc.stage`
       显示：
       - runner shell 正在等待子进程
       - 子进程 `pid=207`
         即
         `rerocc_pipeline_runtime-linux`
         仍是 `R (running)`
   - 接手后的正确下一步已经收敛为：
     1. 终止 `run27`
     2. 把
        `host-init-fileonly-sync-pairdummy.sh`
        默认
        `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE`
        从
        `0`
        改成
        `1`
     3. 重新走 fresh：
        `marshal build -> marshal install`
     4. 再做 local/remote freshness check
     5. 重新跑：
        `launchrunfarm -> infrasetup -> runworkload`
     6. 优先看 checkpoint 文件最后停在：
        - `rr-acquire-end`
        - `doneflag-end`
        - `program-post-src`
        - `wait-enter`
        - `wait-fence-done`
        的哪一步
     7. 然后再决定是改 caller 路径，还是改 doneflag/program/wait 路径

0.0.8. `run22` 已经越过 `run21` 的旧 `tok=1642`，但新冻结点已经收敛到 `compute-done -> c2 export`：
   - 当前 live 只允许继续用私网：
     - 实例：
       `i-0f7202156b104bcc2`
     - 私网：
       `192.168.1.226`
   - `run22` 已明确越过：
     - boot / runner / YAML parse
     - `run21` 的旧 frontier
       `tok=1642`
     - `worker stage=0 subbatch=4 compute-done`
   - 但它现在也已明确不是“只是慢”：
     - `heartbeat.csv`
       从
       `817`
       增长到
       `914`
     - image 内
       `/root/pipeline-runtime-debug/bertmini-batch8.log`
       大小稳定保持
       `375116`
       字节不变
     - `bertmini-batch8.status`
       仍是
       `state=running`
   - 当前静态/动态收敛结论：
     - `segment=0 build-topology end pipebufs=2 ringbufs=0 stage_threads=1`
     - `worker stage=0 ready entries=1 exports=1 isolate_pairs=0 shared_pairs=0`
     - 所以
       `compute-done`
       之后不会走
       `c3/c4/c6/c8`
       路径
     - 当前新 blocker 应直接收敛为：
       **唯一的 `c2` 导出，也就是 `SPM -> DRAM/host` 的同步导出 DMA**
   - 更细判断：
     - `compute-done`
       之前已经有两次同类
       `opcode=2`
       RR acquire
       正常出现，说明通用 alias 导出 helper 不是“完全坏掉”
     - `compute-done`
       之后最后只剩一组新的
       `rr-acquire-inner ... after-set-opc`
       然后再无后续
     - 因而最可疑区间已经从 RR acquire 本身收敛到：
       **`c2` 导出里的 `submit/program/wait(fence)` 子阶段**
   - 还确认了一个语义线索：
     - `segment=0 / stage=0`
       的唯一 export tensor 是
       `tensor=2`
       类型为
       `DRAM`
     - `sync_stage_export_aliases()`
       在
       `compute-done`
       前已经把这个 tensor 导出到模型 alias
     - `compute-done`
       后的 `c2`
       又会把它写到
       `buf->dram_base_addr[0]`
     - 所以后续还要核查
       `alias export`
       和
       `c2 主导出`
       的重复语义是否本身就有实现缺陷
   - 接手后的正确下一步已经收敛为：
     1. 终止当前 `run22`
     2. 保持“文件日志为主，不回退到 UART 主观测面”
     3. 重新 build/install 一个打开
        `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=1`
        的 fresh image
     4. 重新走：
        `launchrunfarm -> infrasetup -> runworkload`
     5. 只用 guest 文件里的 checkpoint，判断最后停在：
        - `rr-acquire-end`
        - `doneflag-end`
        - `program-post-src`
        - `wait-enter`
        - `wait-fence-done`
        的哪一步
     6. 然后再决定是改 DMA 实现，还是先修掉 `alias export + c2` 的重复语义

0.0.7. `run21` 后来已经越过 runner early / YAML parse，但没有走到旧 `tok=2596`，而是冻结在新的 `tok=1642`；当前更像是“默认打开的 DMA 热日志把 guest 文件日志路径打爆”：
   - 继续只用私网：
     - 实例：
       `i-0214e12e733f2a7b0`
     - 私网：
       `192.168.1.70`
   - live 已确认：
     - `bertmini-batch8.runner.stage`
       到了
       `after-bin-spawn`
     - `bertmini-batch8.log`
       已出现：
       - `artifacts mapping parse done`
       - `artifacts mapping parse end`
     - token 从 `379`
       增长到
       `1642`
       后停止；多次采样都保持：
       - `LATEST_TOKEN=1642`
       - `HAS_2596=0`
   - `tok=1642` 附近最后稳定可见：
     - `after-rr-postcheck`
     - `after-rr-state-install`
     - 最后一行停在：
       `after-doneflag-clear tok=1642 value`
       的半截
     - guest 主日志大小稳定在
       `3044196`
       字节，不再增长；但 `heartbeat.csv` 持续增长
   - 静态定位结果：
     - `host-init-fileonly-sync-pairdummy.sh`
       默认仍把
       `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=1`
       打开
     - `prt_dma.c`
       中 `dma-submit-inner` / `dma-wait-inner`
       的 chunk 级 sparse probe
       之前没有经过 deep-log gate，
       在 file-only 模式下会对 guest 文件日志做高频追加
   - 针对这个新 blocker，已经改完但还没重新 fresh build/install/run：
     - `pipeline-runtime/src/prt_dma.c`
       把这组 sparse probe 收回到：
       `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=1`
       +
       `prt_log_gate_allow_deep_logs()`
       +
       小传输窗口
       同时满足时才打开
     - `rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
       把默认
       `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE`
       从 `1`
       改回 `0`
   - 接手后的正确下一步：
     1. 终止当前 `run21`
     2. 重新走 fresh
        `marshal clean -> build -> install`
     3. 用私网做 remote image freshness check
     4. 重新走 FireSim：
        `launchrunfarm -> infrasetup -> runworkload`
     5. 先用 coarse/file-only 日志验证是否穿过 `tok=1642`
     6. 如果后面还需要细粒度 DMA probe，再显式打开 deep-log window，不要再让 pairdummy 默认 stage0 全量热探针常开

0.0.6. shared-scope DMA 根修已经真正进 fresh image，FireMarshal/FireSim freshness 已重新闭环，`run21` 正在新的私网实例上验证：
   - 这轮修复的是上一轮残留的**中间态**：
     - `pipeline-runtime/src/prt_dma.c`
       里
       `dma_copy_host_to_spm_pages_linux()`
       /
       `dma_copy_spm_pages_to_host_linux()`
       之前仍然对每个 host-page chunk 走一次
       `dma_submit_wait_annotated()`
       ，等价于每 chunk 都重新 acquire/release RR scope
     - 现在已经改成：
       - 整批 `dma_batch_scope_acquire()`
       - 每 chunk `dma_submit_wait_annotated_scoped()`
       - 批末 `dma_batch_scope_release()`
   - 新增硬约束已经落到 wrapper：
     - `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
       现在会先
       `cd sims/firesim`
       +
       `source sourceme-manager.sh --skip-ssh-setup`
       ，再继续 FireMarshal
   - 本轮 fresh `marshal clean -> build -> install` 已完成：
     - clean:
       `/home/ubuntu/chipyard/tmp/firemarshal-tmux/pairdummy-prt-clean27.pane.log`
     - build:
       `/home/ubuntu/chipyard/tmp/firemarshal-tmux/pairdummy-prt-build27.pane.log`
     - install:
       `/home/ubuntu/chipyard/tmp/firemarshal-tmux/pairdummy-prt-install27.pane.log`
   - 这轮 fresh hash：
     - image 内 runtime binary sha256 =
       `37af07b801ea8000ca65d54302d6b14a905c5aaf585b0d94cddf2b4a955a2712`
     - 本地/远端运行 image sha256 =
       `37acb93c154e137cd10f08955b53da121577f9ff612d385c11c8810829a3db4f`
   - FireSim 这轮也已完整重新走：
     - launchrunfarm:
       `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch21.pane.log`
     - infrasetup:
       `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-infrasetup21.pane.log`
     - runworkload:
       `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run21.pane.log`
   - 当前实例：
     - `i-0214e12e733f2a7b0`
     - **只允许用私网 SSH**：
       `192.168.1.70`
   - remote image freshness check：
     PASS
     - 脚本：
       `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`
   - 接手时要严格注意：
     - `run21` 当前已经确认不是 stale image / stale remote image
     - 但在这次 prompt 更新时，
       guest 还处在 Linux 启动到应用入口之间，
       还没开始稳定产出
       `/root/pipeline-runtime-debug/bertmini-batch8.log`
     - 所以**暂时还不能宣称已经越过或没有越过旧的**
       `tok=2596 / rr-acquire-inner phase=before-csr-write`
       blocker
   - 接手后的第一步：
     1. 只用私网 `192.168.1.70` 继续看
        `/home/ubuntu/sim_slot_0/uartlog`
        是否越过
        `running /etc/init.d/S99run`
     2. 用 `debugfs`
        直接读运行中的 image：
        `/home/ubuntu/sim_slot_0/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy0-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
        ，检查
        `/root/pipeline-runtime-debug/bertmini-batch8.log`
        是否开始落盘
     3. 一旦应用日志出现，优先对照旧 blocker
        `tok=2596 / before-csr-write`
        判断 shared-scope 根修是否真正把前沿继续推后

0.0.5. 当前最新 live 已切到 `segment=0` 细日志 image，对 `mgr=5/6/7` 的 pointwise fast path 已拿到新证据，但还没推进到旧的 `subbatch=6`：
   - 当前 live：
     - `runworkload`：
       `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run17.pane.log`
     - 实例：
       `i-099d87c4249348d0f`
     - **只允许用私网 SSH**：
       `192.168.1.78`
   - 这轮在起跑前再次做过完整 freshness 闭环：
     - local image sha256 =
       `cb22b2dfa779acc58267b9e5125095b73c3834328f4c70981dfa006fd4eeb2ef`
     - remote image sha256 =
       `cb22b2dfa779acc58267b9e5125095b73c3834328f4c70981dfa006fd4eeb2ef`
     - image 内 runtime binary sha256 =
       `7372ed59627cd9e5e725c2805fa3d91473997e6c154189eefbacf241e953af2e`
     - image 内 `firemarshal.env`
       已确认：
       - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='1'`
       - `DEEP_LOG_ENABLE='1'`
       - `DEEP_LOG_SEGMENT='0'`
       - `PIPELINE_RUNTIME_LOG_PROFILE='fine_segment'`
   - 这轮再次确认旧 runner 卡点已经不是问题：
     - `skip-guest-env inherited path=/firemarshal.env`
     - `after-bin-spawn method=ours2 pid=191`
   - runtime 当前也已重新越过：
     - `yaml pipeline parse/validate`
     - `artifacts mapping load begin`
     - `stage=0 tensor=2` export DMA 主线
   - 当前最重要的新证据：
     - deep log 已明确看到较早 subbatch 上的
       `mgr=5`
       /
       `mgr=6`
       /
       `mgr=7`
       都完成：
       `acquire-end -> dispatch-select -> pointwise-return -> fence-end`
     - 本轮新增细 marker 已实际落到日志：
       - `conv-sync-strided acquire-snapshot`
       - `dispatch-select`
       - `pointwise-precall-snapshot`
       - pointwise inner call enter/return
   - 但必须严格注意：
     - 当前 sparse 前沿只明确到
       `segment=0 sink-progress=4/8`
     - 所以**还不能宣称旧的 `stage=0 / subbatch=6 / tile=5/8 / mgr=5` 卡点已经越过**
     - 当前只能说：
       **`mgr=5/6/7` 这段 pointwise fast path 在较早 subbatch 上已经不再一碰就挂；当前 live 还在继续推进 toward `subbatch=6`。**

0.0.4. `run16` 的最新稳定 freeze 已经进一步收敛，不再是旧 runner / old acquire / old fence 前沿：
   - 同一轮 `run16`
     延时复查后，
     `heartbeat.csv`
     继续增长到
     `1502`
   - 但 guest 文件日志大小保持不变：
     - `bertmini-batch8.log` =
       `2239973`
     - `bertmini-batch8.deep.log` =
       `20847`
   - 最后稳定 sparse tail：
     - `oc-split-pointwise stage=0 tile=5/8 mgr=5 ... begin`
     - `conv-sync-strided stage=0 mgr=5 begin ...`
   - 最后稳定 deep tail：
     - `conv-sync stage=0 mgr=5 acquire-begin`
     - `css-aq-b`
     - `css-aq-e`
     - `conv-sync acquire-end stage=0 mgr=5 scope_valid=1 cfg=1 opcode=3`
   - 对照
     `src/prt_gemmini_adapter.c`
     可知这条日志之后马上进入：
     `prt_log_rr_binding_snapshot_or_marker("conv-sync-strided acquire-snapshot", ...)`
   - 但必须注意一个容易误判的事实：
     pairdummy workload 的
     `host-init-fileonly-sync-pairdummy.sh`
     默认就是
     `PIPELINE_RUNTIME_ONLY_MARKER=1`
   - `2026-04-11--17-07-52` 的 fresh build log 也已确认：
     `-DPRT_ENABLE_ONLY_MARKER=1`
   - 所以当前 workload 下，
     `prt_log_rr_binding_snapshot_or_marker()`
     实际已经是 marker-only，
     **不会去读 RR debug CSR**
   - 因而不要再把当前 blocker 写成
     “RR debug CSR read freeze”。
   - 当前更准确的新判断是：
     **freeze 落在 `acquire-end` 之后、`dispatch-select` 之前的一小段热路径里，但责任点仍待继续切分。**
   - 当前已经补上的新细 marker：
     - `css-as-b/e`
     - `css-tt-b/e`
     - `css-pd-b/e`
   - 下一轮 fresh run 要直接看这些 marker，
     判断 freeze 落在：
     - snapshot marker 本身
     - tiled-type / dilation / scale 预处理
     - pointwise predicate
     - 或 `dispatch-select` 之后

0.0.3. 最新 live 已经不是 runner `/firemarshal.env` sourcing 卡点了，而是重新回到了 runtime 主线：
   - 最新一轮是 `run16`
   - 实例：
     `i-050c4cfeaa2e0faeb`
   - **只允许用私网 SSH**：
     `192.168.1.57`
   - `runworkload`：
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run16.pane.log`
   - 本轮再次做过 freshness 闭环：
     - local image freshness：PASS
     - remote image freshness：PASS
     - local/remote image sha256 =
       `af2d6560d961d914f2e865f743bb39fe5cb8cef0d386c0ba8df7ace137510050`
     - image 内 runtime binary sha256 =
       `93891d7b27b03d17c1d73d79e4d01045f7f63893e205e6c1b1c61eabb40b561c`
   - 这轮已经确认旧 runner 卡点被修掉：
     - `runner-early.stage`
       已出现
       `skip-guest-env inherited path=/firemarshal.env`
     - 后续还继续出现：
       - `after-arg-parse batch=8`
       - `after-bin-check ...`
       - `before-runner-enter`
     - `runner.stage`
       已出现：
       - `after-runner-enter`
       - `after-resolve-manager-layout ...`
       - `before-prepare-hugetlb`
       - `after-prepare-hugetlb`
       - `before-bin method=ours2`
       - `after-bin-spawn method=ours2 pid=193`
   - 这说明：
     **`PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE=1` 已真正生效；不要再把当前 blocker 写成 runner 里重复 source `/firemarshal.env`。**
   - 当前 live 新前沿已重新进入 runtime binary：
     - `bertmini-batch8.log`
       已越过：
       - `yaml pipeline load/parse/validate`
       - `init validate-artifacts`
       - `artifacts mapping load begin`
     - 当前最新 sparse log 已推进到：
       - `stage=0 tensor=2`
       - export DMA submit
       - `tok=481`
       - `mgr=0`
       - 且
         `rr-acquire-inner ... after-csr-read ... acquired=1`
         持续正常出现
   - 接手时如果 `run16` 还活着，先继续看 image 文件日志，
     确认它是继续推进到更老的已知前沿，
     还是在这轮 fresh binary 上出现新的 runtime freeze；
     不要再回头重查 shell 启动链。
   - 补充一个比上面还新的同轮 live 结论：
     - `run16` 已继续推进到
       `worker stage=0 subbatch=6 begin`
     - 对 `subbatch=6`，
       已明确看到：
       - `mgr=0 fence-end rc=0`
       - `mgr=1 fence-end rc=0`
       - `mgr=2 fence-end rc=0`
       - `mgr=3 fence-end rc=0`
       - `mgr=4 fence-end rc=0`
       - 当前已继续到
         `mgr=5 begin`
     - `deep.log`
       还确认了
       `mgr=1`
       的细路径：
       - `rr-fence-scope begin/end`
       - `scope-drain begin/post-gemmini-flush/post-rr-fence/end`
       - `conv-sync ... fence-end rc=0`
     - 所以旧的
       **`run13` 卡在 `stage0/subbatch6 mgr=1 fence-begin`**
       已被 `run16` live 明确否掉；
       接手时不要再把它当当前 blocker。

0.0.2. 最新 live 前沿已经不是旧的 `tok=2145` 半行 freeze 了，而是新的 `stage0/subbatch6` blocker：
   - 最新一轮是 `run13`
   - 实例：
     `i-0f83806fd6efc8707`
   - **只允许用私网 SSH**：
     `192.168.1.189`
   - 这轮 fresh image / binary 已确认是新的：
     - image sha256 =
       `b14f12aa96fac534b7d74fe64c3ba3851de25815db7424341e9a0cc0dff5e413`
     - runtime binary sha256 =
       `23ce7ae9d6b595c61508b3716c64a2999ad897bb719fb58ca47b1379fc0f599f`
   - 这轮已经明确**越过旧的日志写 freeze**：
     - `bertmini-batch8.log`
       已推进到
       `tok=1990`
     - 所以不要再把当前前沿写成
       `tok=2145` / `rr-acquir` 半行
   - 当前最新有效前沿是：
     - `worker stage=0 subbatch=0..5 done`
       全部已出现
     - `worker stage=0 subbatch=6 begin ...`
       已出现
     - `mgr=0`
       已走到
       `conv-sync-strided ... fence-end rc=0`
     - `mgr=1`
       当前最后稳定停在：
       `conv-sync-strided stage=0 mgr=1 fence-begin`
     - 后面没有
       `fence-end rc=0`
   - 当前最准确 blocker：
     **live 冻结位于 `stage=0 / subbatch=6 / tile=1 / mgr=1` 的 pointwise fallback conv 同步收尾区间，最后稳定日志在 `conv-sync-strided ... fence-begin`。**
   - 这里要记住一个关键静态事实：
     - 当前
       `gemmini.h`
       里
       `gemmini_fence()`
       只是普通 CPU `fence`
     - 所以真正更可疑的是它前面的
       `prt_rr_fence_scope()` / `rr_fence(cfg)`
       ，或者更早 Gemmini 请求本身已进入不可回收状态
   - 这个问题是**数据/状态相关**，不是“mgr=1 永远不通”：
     - 同一轮 `run13` 里，
       `subbatch=0..5`
       的 `mgr=1`
       都已经走到
       `fence-end rc=0`
     - 只有最新的
       `subbatch=6`
       卡住
   - 本地 capture 已固化到：
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/20260411-stage0-subbatch6/`
   - 当前这轮实例已通过正规流程回收：
     `firesim terminaterunfarm --forceterminate`
     已执行，
     实例已进入
     `shutting-down`
   - 下一步最优先动作不是盲改功能，而是做一轮**定点深日志** fresh 复现：
     - `PIPELINE_RUNTIME_LOG_PROFILE=fine_segment`
     - `DEEP_LOG_SEGMENT=0`
     - `DEEP_LOG_SUBBATCH=6`
     - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=1`
     - `PIPELINE_RUNTIME_PROGRESS_RAW=1`
     - `PIPELINE_RUNTIME_GEMMINI_PHASE=1`
   - 目标是先区分：
     1. 卡在 `rr_fence(cfg)`
     2. 还是 pointwise fallback 内某条 Gemmini 指令已经把 manager 弄死
   - 另外，2026-04-11 这轮又发现并已修掉一个脚本层漏项：
     - `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
       原先没有转发
       `PIPELINE_RUNTIME_LOG_PROFILE`
     - 所以如果只在 host 侧 export
       `PIPELINE_RUNTIME_LOG_PROFILE=fine_segment`
       ，image 内生成的
       `/firemarshal.env`
       仍可能错误保留
       `PIPELINE_RUNTIME_LOG_PROFILE='manual'`
     - 这会导致 guest wrapper 不自动打开
       `DEEP_LOG_ENABLE`
       ，从而让 `DEEP_LOG_SEGMENT=0` / `DEEP_LOG_SUBBATCH=6`
       看似已设置、但 runtime 实际没有拿到 `--deep-log-enable 1`
     - 接手时一定要先检查 image 内
       `/firemarshal.env`
       至少包含：
       - `PIPELINE_RUNTIME_LOG_PROFILE='fine_segment'`
       - `DEEP_LOG_ENABLE='1'`

0.0.1. `run12` 又拿到了比 `run11` 更靠后的 live 证据：
   - 继续只用私网
     `192.168.1.250`
   - 当前 image 内
     `bertmini-batch8.log`
     已明确越过旧的
     `after-rr-marker -> rr_acquire`
     卡点
   - 对
     `tok=2134..2144`
     已连续看到：
     - `after-rr-marker`
     - `rr-acquire-inner phase=before-csr-write`
     - `rr-acquire-inner phase=after-csr-write`
     - `rr-acquire-inner phase=after-csr-read ... acquired=1`
     - `rr-acquire-inner phase=before-set-opc`
     - `rr-acquire-inner phase=after-set-opc`
   - 但随后又出现新的 freeze：
     - `heartbeat.csv` 继续增长到至少 `908`
     - `bertmini-batch8.status` 仍为 `state=running`
     - `bertmini-batch8.log` 稳定停在 `2326744` bytes
     - 最后完整日志在：
       `tok=2145`
       的
       `rr-acquire-inner phase=after-csr-write`
     - 再下一条只剩半行：
       `[prt-progress] rr-acquir`
   - 因而当前最准确前沿是：
     **run12 已越过旧 blocker，但在 `tok=2145` 附近再次冻结；最后完整证据位于 `after-csr-write` 之后，下一条本应是 `after-csr-read` / 更后的 acquire marker，只留下半行前缀。**
   - 当前仍待区分两种解释：
     1. 卡在
        `rr_read_csr()`
        前后
     2. `rr_read_csr()` 已返回，但打印下一条
        `PRT_PROGRESS_LOG`
        时卡住

0.0. 这之后又继续推进到新的 `run12` live：
   - instance:
     `i-087cedd854b7d9e37`
   - **只允许用私网 SSH**：
     `192.168.1.250`
   - `infrasetup`:
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-infra13.pane.log`
   - `runworkload`:
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run12.pane.log`
   - 远端 freshness 已通过：
     - local/remote image sha256 =
       `58c5a6b6915699c88439898534b41b14da45c6d0102573010ab30bb421f4b46b`
     - runtime binary sha256 =
       `927282ee43ff82b74252dbbe86c07eb8e47edacb729879469f62171a21c49c36`
   - 这轮 guest 已推进到：
     - `running /etc/init.d/S99run`
     - image 内 `/root/pipeline-runtime-debug/bertmini-batch8.log` 已落下
   - 但截至当前观察点，
     `bertmini-batch8.log`
     还停在：
     - `artifacts file pread chunk-begin ... off=2097152 chunk=1048576`
   - 所以这轮还**没有**重新走到旧的
     `after-rr-marker -> rr_acquire`
     卡点。
   - 如果接手时还在这附近，先继续按私网看 image 内文件日志，不要误以为已经复现到旧 blocker。

0. 先补充一个比下面 `run10` 描述还新的本轮结论：
   - 在这之后，已经继续推进到新的 fresh 流程：
     - 旧 `run11` 实例
       `i-0da294d40c7a51a66`
       已进入 `shutting-down`
     - 新 FireMarshal：
       - build:
         `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-build6.pane.log`
       - install:
         `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-install3.pane.log`
       - 两轮 freshness 均通过
       - 当前 runtime binary sha256 =
         `927282ee43ff82b74252dbbe86c07eb8e47edacb729879469f62171a21c49c36`
   - 注意区分两个完全不同层面的事情：
     1. `pairbert-b8-d12s128-launch8`
        失败是因为**调用时漏传了**
        `-c runtime-config`
        ，导致 FireSim 回退到默认 runtime config，
        去查
        `firesim_rocket_singlecore_no_nic_l2_lbp`
        这个当前 hwdb 没有的默认条目。
        这不是 runtime blocker。
     2. 修正后新的
        `pairbert-b8-d12s128-launch8b`
        已经在跑，
        当前卡在 AWS `f2.6xlarge` 容量不足的重试阶段，
        日志里有：
        `Tried all subnets, but there was insufficient capacity to launch your instances`
        这同样不是 runtime blocker。
   - 接手时如果看到 `launch8b` 还在跑，先判断有没有拿到新的实例；
     没拿到就继续等容量或按当时情况重试，不要误把这一层问题写成 runtime 新结论。

0.2. 先补充一个比下面 `run10` 描述还新的本轮结论：
   - 最新 fresh run 是 `run11`
   - 实例：
     `i-0da294d40c7a51a66`
   - **只允许用私网 SSH**：
     `192.168.1.52`
   - `build5/install2` 以及远端 freshness 都已通过
   - 这轮 live 已明确越过：
     - `artifacts mapping load`
     - `artifacts mapping parse`
     - `init validate-artifacts`
   - 当前 image 文件日志最后稳定停在：
     `[prt-progress] dma-submit-inner stage=0 tensor=2 phase=after-rr-marker tok=625 mgr=0`
   - `heartbeat.csv` 持续增长到 `723s`
   - `worker stage=0 subbatch=1 compute-done` 已出现，但 `worker stage=0 subbatch=1 done` 未出现
   - 因而当前 blocker 应当以这条为准：
     **freeze 位于 `stage0/subbatch1` export processing 内，卡在 `dma_blocking_submit()` 的 `after-rr-marker` 之后、`rr-acquire-end` 之前，也就是 `prt_rr_acquire_scope()` / `rr_acquire_cfg()` 内部。**

0.1. 本轮还补充了一个非常重要的构建结论，后续不要再误判：
   - 之前看到
     `grep -a "rr-acquire-inner ..."`
     在 local binary 里没有命中，
     **不能直接推出“binary 没重编”。**
   - 原因有两个：
     1. 如果直接在
        `build/rerocc-linux-tests`
        里跑没有带 `-f` 的 `make rerocc_pipeline_runtime-linux`，
        GNU make 可能只是把现有文件当成已存在目标，根本不重建。
     2. `rr-acquire-inner ...` 属于 `PRT_PROGRESS_LOG(...)`。
        如果手工本地重编时用了默认
        `PIPELINE_RUNTIME_PROGRESS=0`，
        编译器会把这些字符串直接裁掉；
        如果又没有删整个
        `.pipeline_runtime_objs/`，
        还会得到混合旧对象文件的 binary。
   - 当前已经验证可行的本地等价重建方式是：
     - 删掉：
       `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
       和
       `build/rerocc-linux-tests/.pipeline_runtime_objs/`
     - 再按 pairdummy workload 默认宏重建：
       - `PIPELINE_RUNTIME_PROGRESS=1`
       - `PIPELINE_RUNTIME_PROGRESS_RAW=0`
       - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
       - `PIPELINE_RUNTIME_ONLY_MARKER=1`
       - `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
   - `2026-04-11 15:37 UTC`，
     本地等价重建后的 binary 已确认包含：
     - `rr-acquire-inner phase=before-csr-write`
     - `rr-acquire-inner phase=after-csr-write`
     - `rr-acquire-inner phase=after-csr-read`
     - `rr-acquire-inner phase=before-set-opc`
     - `rr-acquire-inner phase=after-set-opc`
   - 所以后续可以放心进入下一轮 fresh FireMarshal / FireSim，
     不再被“marker 没进 binary”这个问题阻塞。

1. `run10` 的当前 live 前沿已经继续收敛，而且比下面旧描述更重要：
   - `uartlog` 已明确推进到：
     `running /etc/init.d/S99run`
   - 用**私网**
     `192.168.1.228`
     对 live image 做 `debugfs` 检查后，已确认 image 内已经落下：
     - `/root/pipeline-runtime-debug/bertmini-batch8.log`
     - `/root/pipeline-runtime-debug/bertmini-batch8.runner-early.stage`
     - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
     - `/root/pipeline-runtime-debug/bertmini-batch8.status`
   - 当前 log 已明确推进到：
     - `yaml model load end`
     - `init load-pipeline-yaml end`
     - `init validate-artifacts begin`
     - `artifacts mapping cache disabled ... reason=env`
     - `artifacts mapping load begin ...sbus128.yaml`
   - 但截至
     `2026-04-11 15:03 UTC`
     为止，仍**没有**出现：
     - `artifacts mapping load end`
     - `artifacts mapping parse begin/progress/end`
     - `init validate-artifacts end`
     - 更后面的 `submit-begin / rr-acquire-begin`
   - 同时 host 侧
     `.img mtime`
     停在：
     `2026-04-11 15:00:15 UTC`
     不再变化，
     但
     `heartbeat.csv`
     仍继续增长到：
     `664s`
   - 当前前沿 blocker 因而应改写为：
     **live 停点位于 `prt_validate_gemmini_artifacts() -> parse_mapping_file() -> load_file()` 的更早 load/read 区间。**

2. 但是，这里不能简单得出
   “pairdummy+sbus128 的 layer mapping load 本身必挂”。
   本地已有同日旧 capture：
   `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-nochkpt-noaudit-exportsubmithang-20260411/bertmini-batch8.log`
   已证明同一条
   `gemmini_layer_mapping....sbus128.yaml`
   曾成功走过：
   - `artifacts mapping load end`
   - `artifacts mapping parse end`
   - 并继续推进到更后的 export-submit hang
   但那份旧 capture 的 `status` 里是
   `guest_deep_log_enable=1`，
   当前 `run10` 则是
   `guest_deep_log_enable=0`。
   所以当前只能说：
   - 这次新卡点和 `no deep log` 路径存在相关性
   - 还不能说已经定位到 layer mapping 语义错误

3. fresh pairdummy image 已重新 `marshal build` + `marshal install`，
   且 build/install 两轮本地 freshness 都通过：
   - build tmux：
     `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-build3.pane.log`
   - install tmux：
     `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-install1.pane.log`

4. 新 run farm 已重启到：
   - 实例：
     `i-07def72243ac94e24`
   - 私网：
     `192.168.1.228`
   - live 查询和 SSH 仍然**只能**用这个私网地址

5. 这轮 `infrasetup` 要以第二次干净重跑为准：
   - `pairbert-b8-d12s128-infra10`
     在 `instance_liveness` 处退出，
     但这轮中间做过额外观测，不要把它当成新的稳定 blocker
   - 随后已用同一套 Fabric 配置对
     `192.168.1.228`
     直接执行
     `uname -a`
     /
     `echo $0`
     验证 SSH/Fabric 本体可用
   - `pairbert-b8-d12s128-infra11`
     已成功完成，exit code `0`

6. 远端 freshness 已再次闭环通过：
   - 脚本：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`
   - private ip：
     `192.168.1.228`
   - local image sha256 =
     `185f452f477d5f86953c6c8b02fbda4f763dad2f93337caf53d741fd3d953a2f`
   - remote image sha256 =
     `185f452f477d5f86953c6c8b02fbda4f763dad2f93337caf53d741fd3d953a2f`
   - image 内 env 已确认当前目标配置：
     - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
     - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`

7. 新一轮 run 已启动：
   - tmux session：
     `pairbert-b8-d12s128-run10`
   - result dir：
     `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-11--14-50-51-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
   - run log：
     `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-11--14-50-51-runworkload-J2SAVCJFQ8GB2FFA.log`

8. 接手后的下一步不要再重复做泛泛观察，直接按这个顺序：
   - 继续只用私网 SSH
     `192.168.1.228`
     看 live host
   - 若 `run10` 还在跑，优先再次确认：
     - `.img mtime` 是否仍冻结
     - `heartbeat.csv` 是否仍增长
     - image 内 `bertmini-batch8.log` 是否仍停在
       `artifacts mapping load begin`
   - 如果上述三点继续成立，
     下一轮代码修改应优先补
     `prt_gemmini_artifacts.c:load_file()`
     的细 marker，
     至少区分：
     - `open`
     - `lseek`
     - `malloc`
     - `pread begin/end`
   - 然后必须重新：
     `marshal build -> marshal install -> freshness check -> infrasetup -> runworkload`
   - 不要跳过 freshness，也不要改用公网 IP

当前最重要的状态更新到 2026-04-08：

补充一个更高优先级的 2026-04-11 新状态，接手时优先看这个，而不是下面 2026-04-08 的旧描述：

1. `checkpoint/audit` 文件写关闭后的受控复现已经真正跑过：
   - 只跑 `METHODS=ours2`
   - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0`
   - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0`
   - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=1`
   - `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=1`
   - `EXPORT_DMA_TIMEOUT_MS=10000`
   - 远端 image freshness 已通过私网 SSH 闭环校验
   - run host 私网：
     `192.168.1.173`
   - 相关现场 capture：
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-nochkpt-noaudit-exportsubmithang-20260411/`

2. 这轮 live run 已经**越过旧 freeze 点**：
   - sparse log 明确推进到：
     - `worker stage=0 subbatch=0..6 done`
     - `worker stage=0 subbatch=7 begin`
     - `segment=0 sink-progress=7/8 elapsed_ms=2008 fatal=0 stop=0`
   - 所以“关闭 checkpoint/audit 后仍卡在旧 fixed-weight DMA 前面”这个说法已经不成立。

3. 但这轮又暴露出一个更靠后的新 hang：
   - `heartbeat.csv` 持续增长，guest/仿真没死
   - `bertmini-batch8.status` 仍为 `state=running`
   - 但 sparse/deep log 在观察窗口内都完全不再增长：
     - sparse log: `977089` bytes
     - deep log: `2336657` bytes
   - deep log 最后完整推进到：
     - `stage=0`
     - `subbatch=7`
     - `tensor=2`
     - `export-target ... dst=0x3faac9f400 ... begin`
     - `tok=3017..3029` 均成功到 `wait-end rc=0 hw_done=0`
   - **最后一条新日志** 是：
     - `dma-submit stage=0 tensor=2 phase=submit-begin src=0x40502400 dst=0x1047c7800 bytes=1024 ...`
   - 后续本该出现的：
     - `rr-acquire-begin`
     - `submit-end tok=3030`
     - `wait-begin tok=3030`
     都没有再出现

4. 这意味着当前 blocker 需要改写成：
   **hang 位于 `dma_submit_wait_annotated() -> prt_dma_submit() -> dma_blocking_submit()` 的更早入口区间，落在 `submit-begin` 之后、`rr-acquire-begin` 之前。**

5. 当前最强的新怀疑不是硬件 DMA 本体，而是：
   - `PRT_MARKER_LOG` 在当前 build 下走 guest deep log 文件直写
   - freeze 恰好发生在下一条 deep marker `rr-acquire-begin` 之前
   - 因而需要优先验证：
     **deep log 文件追加本身是否成为新的阻塞源**

6. 因而下一轮不要直接沿用当前这套 heavy deep log。
   下一步建议是：
   - 关闭 `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE`
   - 保留 file-only sparse log
   - 在 `prt_dma.c:dma_blocking_submit()` 里补少量 `PRT_PROGRESS_LOG/ERR_LOG` 稀疏 marker，
     只包住：
     - `prt_trace_on_dma_submit(rt)`
     - `prt_trace_log_event(...)`
     - `dma_debug_capture_req(tok, req)`
     - `rr-acquire-begin` marker 前后
   - 目标是区分：
     - deep log 追加阻塞
     - `prt_dma_submit()` 内部真实逻辑阻塞

7. 当前这轮 run farm 已按正规流程终止：
   - terminate session:
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-term1.pane.log`
   - 实例 `i-006f62c2159f5907d` 已进入 `shutting-down`

当前最重要的新状态：

1. 旧 frozen mainline 的历史结论仍成立：
   - `2026-04-02` 那轮主线 workload 能完整跑完，
     最终停在 tensor 48 的 golden mismatch。
   - 这条历史结论不能被改写成 “DMA hang”。

2. 旧 coupled-DMA baseline 的历史结论也仍成立：
   - 在
     `DUMMY_GEMMINI_MODE=1 + skip-model-bin-load=1`
     时，旧硬件也能复现
     `init synthesize-model-bin begin reason=skip-model-bin-load`
     这条 synthetic-model 路径 hang。
   - 所以“只有新硬件挂”这个说法一直是错的。

3. 但当前**最新、优先级最高**的结论，已经继续前推，而且比
   `segment 0 not_ready (-7)`
   更早：
   - 基于
     `sbus128 pairdummy`
     的 fresh 复跑已经带上了 synthetic-model 的
     `prefault + targeted mlock(blob)` 修复，
     guest 仍然使用
     `PIPELINE_RUNTIME_MLOCKALL_MODE=1`
     （`MCL_CURRENT` only）。
   - 第一轮 decisive run 的 live 证据表明：
     runtime 子进程**没有走到 synthetic-model 分配**，
     一度稳定停在：
     - `[prt-progress] yaml model file read ... bytes=18518`
   - 在继续加入
     `prt_yaml_loader.c`
     parser marker 后，
     第二轮又把停点继续前推到了：
     - `[prt-progress] yaml model load begin ...`
   - 在一次 `65s` 的 live 观察窗口内，
     `bertmini-batch8.log`
     大小固定为：
     `4599`
   - 后面一直没有看到：
     - `yaml file open begin`
     - `yaml model file read`
     - `yaml model parse begin`
     - `yaml model parse line=...`
     - `yaml model load end`
     - `init load-model-yaml end`
     - `init synthesize-model-bin begin`
     - `synthetic-model alloc before-prefault`
     - `synthetic-model alloc after-prefault`
     - `synthetic-model alloc before-mlock`
     - `synthetic-model alloc after-mlock`
   - 所以当前应该先把 blocker 改写为：
     **`prt_load_model_yaml()` 里的 `load_file()` 本体存在卡点。**
   - 这两轮 live 证据分别已保存到：
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-prefaultmlock-yamlhang-20260408/`
     `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-yamlmarker-loadfilehang-20260408/`

4. 在这条新证据之上，旧的 `mlockall` / synthetic-model 结论仍然有效，但优先级已经下降：
   - 在
     `sbus128 pairdummy`
     上，加入临时调试开关
     `PIPELINE_RUNTIME_MLOCKALL_MODE=1`
     （也就是 `MCL_CURRENT` only）之后，
     runtime **不再卡在 `mlockall`**。
   - 它已经明确走过：
     - `[prt-early] process memory lock mode=current-only flags=0x1`
     - `[prt-early] before mlockall`
     - `[prt-early] after mlockall rc=0`
     - `[prt-early] after mlockall mode=current-only flags=0x1`
   - 它也已经明确走过 synthetic-model 分配：
     - `synthetic-model alloc before-compute`
     - `synthetic-model alloc after-compute size=17055744 ...`
     - `synthetic-model alloc before-mmap size=17055744`
     - `synthetic-model alloc after-mmap ptr=... size=17055744`
     - `synthetic-model alloc kind=mmap-lazy ...`
     - `init synthesize-model-bin end ...`
   - 当前这条调试线的新停点已经更新为：
     - `worker stage=0 exit stop=0 fatal=-7 requested=0`
     - `segment[0] failed: not_ready (-7)`
     - `runtime_run failed: not_ready (-7)`
     - `BERTMINI_PIPELINE_RUNTIME_FAIL`

5. 基于上一轮日志和静态代码收敛，仍然保留一个**重要但已次级**的判断：
   - 当前这个
     `segment 0 not_ready (-7)`
     很可能发生在
     `build_stage_conv_desc()`
     早期的
     `stage_prepare_exec_views()`
     里，
     在 worker 打出 `wrkrdy` / `gemm-run` marker 之前就已经返回了。
   - 更具体地说，
     stage-0 fixed tensor 会先走
     `prt_dma_copy_dram_to_spm_pages()`
     把 host tensor 搬进 SPM；
     Linux/RISC-V 下它会立即调用
     `prt_host_virt_to_phys()`。
   - 而
     `skip-model-bin-load`
     这条 synthetic-model 路径原来用的是匿名 lazy `mmap`；
     所以如果这些页还没被 materialize，
     就会直接拿到
     `PRT_ERR_NOT_READY (-7)`。
   - 因而当前更准确的结论不是
     “`MCL_FUTURE` 这个 flag 一定不可删”，
     而是：
     **任何 future 映射里会参与 DMA 的页，都必须在第一次 DMA 前变成 present。**
   - 当前树里已经有一个新的定点修复，不要丢：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
     里的 synthetic-model `mmap`
     现在会先按页 prefault，再对 blob 做定向 `mlock(...)`，
     目的就是去掉
     `stage 0 fixed-tensor DMA`
     对
     `MCL_FUTURE`
     的首次 materialize 依赖，并且在 `before-prefault -> after-prefault -> before-mlock -> after-mlock`
     之间留下了更细的卡点 marker。
   - 这个修复当前只做过交叉编译验证，
     **还没有 FireSim 复跑验证**。

因此当前更准确的 blocker 描述是：

- 第一优先级：
  `prt_load_model_yaml()` 在
  `yaml model file read`
  之后、
  `yaml model load end`
  之前的解析路径。
- 第二优先级：
  如果 YAML 路径被排除，再回到
  `current-only` 模式下暴露出来的
  `segment 0 not_ready (-7)`。

当前树里已经有的调试改动，不要丢：

- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/main.c`
  - 新增 `PRT_MLOCKALL_MODE`
  - 取值：
    - `0`: 原行为 `MCL_CURRENT | MCL_FUTURE`
    - `1`: `MCL_CURRENT`
    - `2`: skip `mlockall`
    - `3`: `MCL_FUTURE`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
  - synthetic-model 匿名 `mmap` 现在会按页 prefault
  - 目的是让 stage-0 fixed tensor 的第一次 DMA 不再依赖 `MCL_FUTURE` 去 materialize future 页
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_yaml_loader.c`
  - 已新增：
    - `yaml model parse begin`
    - 前 12 行和每 64 行的解析进度日志
    - `layer-push` / `layer-index` marker
    - `yaml model parse error line=... key=...`
    - `yaml model parse end`
    - `yaml file open begin/end/fail`
    - `yaml file seek-end begin/end/fail`
    - `yaml file ftell begin/end/fail`
    - `yaml file seek-set begin/end/fail`
    - `yaml file alloc begin/end/fail`
    - `yaml file read begin/end/fail`
    - `yaml file close end`
  - 同时对 `strtoul/strtoull` 加了“必须前进”的防自旋保护
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/Makefile`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/Makefile`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/workload/host-init.sh`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
  - 当前默认：
    `PIPELINE_RUNTIME_MLOCKALL_MODE=1`
- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
  - status 里会写：
    `mlockall_mode=...`
- `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
  - 已支持透传：
    `PIPELINE_RUNTIME_MLOCKALL_MODE`

这轮 fresh pairdummy artifact：

- image：
  `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
  - mtime：
    `2026-04-08 09:00:36 +0000`
  - sha256：
    `23e06390d5cdb8ba28cc8b7c151d45b8c6dac85294ca999f3c13df7cdf8b0ed1`
- bin：
  `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-bin`
  - mtime：
    `2026-04-08 09:00:34 +0000`
  - sha256：
    `8093b3a3787436cd249d3f2c333308bf6d62a54e6f84554a5927d13c8cbd157f`
- FireMarshal build log：
  `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-build-2026-04-08--08-59-39-PBQ59SA8Z1EFQFUV.log`
- FireMarshal install log：
  `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-install-2026-04-08--09-01-08-1RR06BNT5RXIP5CK.log`

这轮最新 decisive run：

- runtime：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
- hwdb：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
- build recipes：
  `/home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml`
- run 结果目录：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-08--12-26-43-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
- live capture：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-prefaultmlock-yamlhang-20260408/`
- 关键事实：
  - guest Linux 明确到达：
    `running /etc/init.d/S99run`
  - guest status 一直停在：
    - `state=running`
    - `mlockall_mode=1`
    - `skip_model_bin_load=1`
    - `skip_input_load=1`
    - `skip_golden_check=1`
  - runner stage 稳定停在：
    - `before-bin method=ours2`
  - wrapper stage 稳定停在：
    - `before-child-wait`
  - guest 粗日志最后一行稳定停在：
    - `[prt-progress] yaml model file read ... bytes=18518`
  - 这说明 runtime 子进程至少还没完成 `load_model_yaml()`
  - 这轮 run 已人工终止并完成：
    `terminaterunfarm --forceterminate`
- run host：
  - instance：
    `i-002abfde0b580a968`
  - private ip：
    `192.168.1.199`
  - 这台实例当前应已不再 `running`；
    上一轮结束时 AWS 状态为：
    `shutting-down`

必须遵守的硬约束：

- 全程中文。
- FireSim 只走正规 manager 流：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- FireSim manager 命令只通过：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- FireMarshal 只通过：
  `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
- 每次 image/rootfs/bin/AGFI 变化后，下一轮前必须重新 `infrasetup`
- 每次 run 被中断/失败/人工停止后，下一轮前也必须重新 `infrasetup`
- 每次执行前都要核对镜像是不是新的，这是硬约束
- Linux 启动日志可以走 UART
- bin/runtime 日志不要走 UART，要走 guest 文件
- 如果要 SSH 到 run host 做 live 查询，必须使用私有地址；
  不要使用公网地址
- run 结束、失败或人工中断后，必须立刻关掉 run farm，并确认实例不再 `running`
- 不要随便清理别的进程

下一轮建议的优先顺序，不要跳：

1. 不要先回去重查 `mlockall` / synthetic-model / `segment 0 not_ready`；
   现在第一优先级是重新 fresh 复跑，
   看新的 `prt_yaml_loader.c` marker 能把停点精确到哪一行/哪一类字段。
2. 重点看 guest 文件日志里是否出现：
   - `yaml model parse begin`
   - `yaml model parse line=...`
   - `yaml model layer-push ...`
   - `yaml model parse error line=... key=...`
   - `yaml model parse end`
   - `yaml model load end`
3. 如果新日志确认 YAML 已经走通，
   再回到
   `segment 0 not_ready (-7)`
   那条线继续缩小。
4. 如果之后要再确认 `MCL_FUTURE` 假设，
   再做一次明确 A/B：
   - `PIPELINE_RUNTIME_MLOCKALL_MODE=0`
   - `PIPELINE_RUNTIME_MLOCKALL_MODE=3`
   但那应该排在 YAML 停点和 `not_ready` 追踪之后。
5. 任何新 run 结束后，第一时间 `terminaterunfarm --forceterminate`

不要做的事：

- 不要把当前问题重新叙述成“只有新硬件挂”
- 不要把当前问题重新叙述成“Linux 没起来”
- 不要把 manager `runworkload exit code=0` 误当成 guest pass
- 不要把 bin/runtime 大量日志重新打回 UART
- 不要忘记在跑完后关闭 run farm

## 2026-04-11 16:13 UTC 最新续跑状态

这段状态比上面旧的 YAML 停点记录更新，下一轮请以这里为准：

- 已从 live guest image 直接抽取到当前 decisive 证据：
  - 实例：
    `i-087cedd854b7d9e37`
  - private ip：
    `192.168.1.250`
  - `bertmini-batch8.status` 显示：
    - `state=running`
    - `guest_log_enable=1`
    - `guest_deep_log_enable=0`
    - `periodic_sync_enable=1`
    - `mlockall_mode=2`
- `bertmini-batch8.log` 最后稳定停在：
  - `tok=2145`
  - `rr-acquire-inner phase=after-csr-write ...`
  - 然后只剩半截：
    `rr-acquir`
- 这说明：
  - 旧 blocker
    `after-rr-marker` 之前
    已经被越过
  - 当前 freeze 不是 deep log 路径，因为这轮 deep log 根本没开
  - 更合理根因是 sparse guest 文件日志发生 partial write 后，
    旧实现继续补写剩余字节，结果把 runtime 卡在热路径里

已完成修补：

- 文件：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_progress.h`
- 函数：
  `prt_write_fd_all_impl()`
- 改动：
  - 不再循环等待整条日志写完
  - 只要 `write()` 已经接受任意字节就立即返回
  - 仅对 `EINTR/EAGAIN/EWOULDBLOCK` 做最多 4 次轻量重试

已完成本地重编验证：

- binary：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- sha256：
  `23ce7ae9d6b595c61508b3716c64a2999ad897bb719fb58ca47b1379fc0f599f`
- 编译时间：
  `2026-04-11 16:11:14 +0000`

live run 回收状态：

- 已成功执行：
  `terminaterunfarm --forceterminate`
- AWS 上该实例状态已进入：
  `shutting-down`

下一轮不要跳步：

1. 先重新走 FireMarshal `build` / `install`
2. 重新做本地 freshness check
3. 重新 `launchrunfarm`
4. 重新 `infrasetup`
5. 用私网 IP 做 remote freshness check
6. 再 `runworkload`
7. 第一优先级观察：
   `tok=2145` 后是否还能再出现半截 sparse log
8. 如果新 run 仍卡，再继续围绕 `rr_read_csr()` 本体缩小
```

## 2026-04-11 17:28 UTC 更新

上面那段 `rr-acquire` 旧卡点已经不是当前优先级。下一轮请先以这里为准。

最新 decisive 证据来自 `run15`：

- runtime config：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
- hwdb：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
- workload json：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
- run host：
  - instance：
    `i-0fec203829a13527f`
  - private ip：
    `192.168.1.7`
- 这轮已严格完成：
  - FireMarshal build/install
  - local freshness check：PASS
  - remote freshness check：PASS
  - remote image sha256 与 local image sha256 一致

live guest image 里最关键的文件证据：

- `bertmini-batch8.wrapper.stage` 停在：
  - `wrapper-enter`
  - `after-log-preamble`
  - `before-child-spawn`
  - `after-child-spawn pid=131`
- `bertmini-batch8.status` 为：
  - `state=running`
  - `log_profile=fine_segment`
  - `dummy_gemmini_mode=1`
  - `skip_model_bin_load=1`
  - `skip_input_load=1`
  - `skip_golden_check=1`
  - `mlockall_mode=2`
- `bertmini-batch8.runner-early.stage` 只到：
  - `script-entry`
  - `before-guest-env path=/firemarshal.env`
- `bertmini-batch8.runner-proc.stage` 只到：
  - `label=script-entry`
  - `cmdline=/bin/sh /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh --batch 8`
  - `wchan=do_wait`
- `bertmini-batch8.log` 只有 firemarshal wrapper 自身前导行，没有任何 `[bertmini]` 或 `[prt-early]`

当前结论：

- wrapper 已经成功启动 runner shell
- runner shell 没有走到 runtime binary
- 当前 blocker 在 runner 更早阶段，表现上落在二次 `source /firemarshal.env` 附近，或者至少在它之后的极早位置
- 这已经比上一轮 “before main / no `[prt-early]`” 更早更精确

已完成的最新修补：

- 文件：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - 新增：
    `PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE`
  - 当环境已由 wrapper 继承时，runner 跳过二次 `source /firemarshal.env`
  - `RUNNER_STAGE_SYNC_ENABLE` 默认从 `1` 改为 `0`
- 文件：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
  - wrapper spawn child 时显式传：
    - `PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE=1`
    - `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=0`

当前下一步不要跳：

1. 先用这两个修补重新 `marshal build`
2. 再 `marshal install`
3. 重新 `launchrunfarm`
4. 重新 `infrasetup`
5. 用私网 IP 做 remote freshness check
6. 再 `runworkload`
7. 第一优先级看：
   - `runner-early.stage` 是否出现
     `skip-guest-env inherited`
   - 是否继续出现 `after-arg-parse`
   - 是否终于出现 `before-bin`
   - 是否出现新的 binary-child `runner-proc` 快照
8. 如果这轮修补后仍卡在 runner 更早阶段，再继续细化 wrapper/runner，而不是直接回去查 runtime 语义

## 2026-04-12 04:05 UTC run17 最新续调结论

当前 live run 仍然是：

- session：
  `pairbert-b8-d12s128-run17`
- instance：
  `i-099d87c4249348d0f`
- private ip：
  `192.168.1.78`

这轮已经再次确认：

- runner 早期链路已通：
  - `runner-early.stage` 有
    `skip-guest-env inherited path=/firemarshal.env`
  - `runner.stage` 有
    `after-bin-spawn method=ours2 pid=191`
- 所以不要再回到“卡在 runner/pre-main”那个旧结论

当前 live image 的真实 frontier（通过私网 IP SSH 到 run host，再对
`/home/ubuntu/sim_slot_0/*pairdummy.img` 做 `debugfs cat` 观察）是：

- `stage=0 subbatch=5`
- 已经完整越过：
  - `mgr=5 fence-end`
  - `mgr=6 fence-end`
  - `mgr=7 fence-end`
- 已经进入：
  `worker stage=0 subbatch=5 export-sync-enter`
- 当前冻结在：
  `tensor=2` 导出到 `target=address2` 的 export DMA 路径
- 最后一个确定出现的 marker 是：
  `dma-submit stage=0 tensor=2 phase=rr-acquire-end rc=0 cfg=0 rr_mgr=0 rr_opc=2`
  对应 token：
  `tok=2281`
- 下一条本应出现但没有出现的是：
  `phase=doneflag-begin`

因此当前 blocker 要写成：

- 这段旧写法已经被后续 live image 文件日志推翻，不能再沿用：
  `stage=0 subbatch=5 export-sync / tensor=2 / target=address2 / tok=2281`
- 后续纠正后的真实语义是：
  `segment=0 / stage=0 / tensor=0 / C1 entry DMA / tok=2281`
- 更窄窗口要按新稀疏日志重写成：
  `stage=0 / tensor=0 / tok=2281 / rr-acquire-end -> <after-rr-postcheck 之后的新稀疏日志>`

不要再写成这些旧误判：

- `RR debug CSR read freeze`
- `runner shell pre-main hang`
- `subbatch=6 blocker already crossed`

当前 run 是否“还在推进”的结论也要精确写：

- `heartbeat.csv` 从 `1031` 增长到 `1087`
- 但 `bertmini-batch8.status` / `bertmini-batch8.log` / `bertmini-batch8.deep.log`
  在 25 秒窗口内字节数完全不变
- 这表示仿真时钟还在跑，但 pipeline runtime 日志前沿已经冻结

这轮顺手定位并修了 host monitor 两个缺陷：

- 文件：
  `/home/ubuntu/chipyard/scripts/firesim-prt-host-watchdog.sh`
- 修补：
  - 默认 `arm_on_guest_status_nonzero` 改为 `1`
  - 默认 `remote_img_glob` 放宽为：
    `/home/ubuntu/sim_slot_0/*rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8*.img`
- 原因：
  - 旧默认 glob 抓不到当前
    `...batch8-fileonly-sync-pairdummy.img`
  - file-only 模式又不依赖 UART arm marker，旧默认不会进入 progress 追踪

这轮还加了新的 runtime 细 marker：

- 文件：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
- 新增：
  - `phase=rr-acquire-postcheck`
  - `phase=rr-state-install-end`
  - `phase=doneflag-clear`

下一个 session / 下一轮 fresh rerun 的第一优先级：

1. 用新 binary 和新 host watchdog 重新 fresh 跑
2. 第一时间确认 `tok=2281` 附近是否出现：
   - `rr-acquire-postcheck`
   - `rr-state-install-end`
   - `doneflag-clear`
   - `doneflag-begin`
3. 如果仍然停在这些 marker 之前，就继续围绕这几个语句做更细分割
4. 如果已经越过 `doneflag-begin`，再继续缩小到 `dma_debug_capture_done_flag()` 或其后续编程路径

## 2026-04-12 04:30 UTC 新的代码状态与下一轮执行约束

这轮已经不是单纯分析，代码已经实际改动：

- `pipeline-runtime/src/prt_dma.c`
- `pipeline-runtime/include/prt_types.h`
- `pipeline-runtime/include/prt_runtime.h`

已完成的修正：

- `blocking_fence` DMA wait 不再通过轮询 `doneflag` 判断完成
- 当前 wait 主链改为：
  - `hw_dma_fence()`
  - 之后 `rr_fence_scope()`
- completion flag 不再使用 `&tok->hw_done_flag` 这种 token 临时地址做每次 submit 时的 `virt_to_phys`
- 改成 runtime 级稳定 completion pool：
  - 启动时一次性分配
  - prefault
  - `mlock`
  - 一次性 `virt_to_phys`
  - submit 时只取 slot
- stage-local bounce page 也补成：
  - prefault
  - `mlock`

这条要记成新的硬约束：

- 任何后续编译动作之前，必须先：
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh --skip-ssh-setup`

这条已经现场验证过；manager 环境会把交叉编译器放进 `PATH`：

- `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc`

本地 binary 已经按当前 `pairdummy fileonly-sync` 宏组合强制重编完成：

- `PIPELINE_RUNTIME_PROGRESS=1`
- `PIPELINE_RUNTIME_PROGRESS_RAW=0`
- `PIPELINE_RUNTIME_PROGRESS_HOT=1`
- `PIPELINE_RUNTIME_ONLY_MARKER=1`
- `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
- binary：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- SHA256：
  `8bfa0d4bc739e41484cb9ad3b74f1324ef32717d28401d7fe844ae005dd5500e`

已确认新 binary 含有这些关键字符串：

- `dma-completion-pool`
- `dma-doneflag-slot begin/end`
- `checkpoint=wait-fence-done`
- `phase=rr-acquire-postcheck`
- `phase=rr-state-install-end`
- `phase=doneflag-clear`
- `phase=doneflag-begin`
- `phase=doneflag-end`
- `dma-backend using blocking_fence completion via hw_dma_fence + rr_fence_scope`

注意：

- 这轮还没有 fresh build/install image
- 也还没有做 remote image freshness check
- 也还没有 fresh `infrasetup` / `runworkload`

下一轮必须严格按下面顺序继续：

1. 在 `sims/firesim` 下 `source sourceme-manager.sh --skip-ssh-setup`
2. 用新 binary 重新 `marshal build`
3. 重新 `marshal install`
4. 用私网 IP 做 remote image freshness check
5. 重新 `firesim infrasetup`
6. fresh `runworkload`
7. 第一优先级看旧 frontier `tok=2281` 是否越过，并确认是否出现：
   - `rr-acquire-postcheck`
   - `rr-state-install-end`
   - `doneflag-clear`
   - `doneflag-begin`
   - `dma-doneflag-slot end`
   - `wait-fence-done`

## 2026-04-12 04:43 UTC 本轮最新 live 状态

本轮已经继续完成到 fresh live run：

- `infrasetup`：成功
- remote image freshness check：成功
- fresh `runworkload`：已启动

关键对象：

- instance id：
  `i-03e660582d21388b0`
- private ip：
  `192.168.1.55`
- live tmux session：
  `pairbert-b8-d12s128-run19`
- pane log：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run19.pane.log`
- host monitor log：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run19.monitor.log`
- manager run log：
  `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-12--04-39-37-runworkload-AL2R74LS81FS0T9B.log`

remote image freshness 已确认：

- image SHA256：
  `b56562fbaa6faa22cb659c7c6bb7ad4bbfe1596fd5100e5f0e86806d3da2f845`
- guest image env：
  - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
  - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
  - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
  - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
  - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`

注意：

- 有一个失败的无效启动：
  - session：
    `pairbert-b8-d12s128-run18`
  - 原因：
    我把 host 启动环境裁得过头，丢了 conda，导致 wrapper 在
    `source sourceme-manager.sh` 前直接报：
    `::ERROR:: you must have conda in your environment first`
  - 这不是 guest / runtime / hardware 问题

截至当前时刻的 live 结论：

- 不能说已经卡回旧 DMA blocker
- 也不能说已经越过旧 DMA blocker
- 当前 guest 还在 Linux 启动期，尚未进入 firemarshal 用户态
- 证据：
  - `heartbeat.csv` 已增长到：
    `3088332452, 178`
  - `uartlog` 目前仍是 Linux 早期启动输出
  - image 内这些 guest 文件仍为空：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper-proc.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner-early.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner-proc.stage`

下一轮 / 下一个观察点要做的事：

1. 继续盯 `pairbert-b8-d12s128-run19`
2. 继续只用私网 IP `192.168.1.55` 做远端检查
3. 第一时间等 guest 文件日志出现内容，而不是只看 UART
4. 一旦 `runner` / `status` 文件出现，再判断是否越过旧 frontier `tok=2281`
5. 只有在真正进入 runtime 文件日志之后，才去判断这些 marker 是否出现：
   - `rr-acquire-postcheck`
   - `rr-state-install-end`
   - `doneflag-clear`
   - `doneflag-begin`
   - `dma-doneflag-slot end`
   - `wait-fence-done`

## 2026-04-12 04:49 UTC 最新推进点

当前 `pairbert-b8-d12s128-run19` 已经不再停在 Linux / firemarshal 早期阶段。

已经确认越过：

- Linux 启动期
- firemarshal wrapper / runner 启动期
- pipeline yaml 加载与校验
- layer mapping 大文件读取阶段

关键证据：

- host watchdog 已记录：
  - `guest-status arm observed at 2026-04-12T04:46:09Z hb='6814151757, 378'`
  - `guest_sparse` 继续增长：
    - `938`
    - `199091`
    - `253952`
    - `797010`
- guest 文件里已经看到 completion pool 修正真正生效：
  - `dma-completion-pool before-prefault`
  - `dma-completion-pool after-prefault`
  - `dma-completion-pool before-mlock`
  - `dma-completion-pool after-mlock rc=0 errno=0`
  - `dma-completion-pool ready slots=1024 bytes=4096 ...`
- 已完成：
  - `init load-pipeline-yaml end`
  - `init validate-artifacts begin`
  - `artifacts mapping load end ... bytes=12029923 elapsed_ms=530`
- runner 已到：
  - `after-runner-enter`
  - `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
  - `before-prepare-hugetlb`
  - `after-prepare-hugetlb`
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=192`

当前最新 sparse frontier：

- 已经进入 DMA submit 主体
- 已看到连续：
  - `dma-submit-inner stage=0 tensor=2 ...`
  - `rr-acquire-inner phase=after-csr-read ... acquired=1`
  - `rr-acquire-inner phase=before-set-opc`
  - `rr-acquire-inner phase=after-set-opc`
- 当前已观测 token 大约推进到：
  - `tok=527`

所以当前精确结论是：

- 不再卡在更早的 yaml / artifact load 阶段
- 也还没有到旧 live blocker `tok=2281`
- 下一轮继续盯 `pairbert-b8-d12s128-run19`，重点是把 frontier 一直追到 `tok=2281` 附近，再判断是否出现：
  - `rr-acquire-postcheck`
  - `rr-state-install-end`
  - `doneflag-clear`
  - `doneflag-begin`
  - `dma-doneflag-slot end`
  - `wait-fence-done`

## 2026-04-12 04:56 UTC 交接补充：run19 已冻结，且新增一条必须遵守的编译约束

先记住两条硬约束：

1. 所有 SSH / remote inspection / remote freshness check 都只能使用私网 IP，不允许使用公网 IP
2. 任何后续编译动作之前，必须先：
   - `cd /home/ubuntu/chipyard/sims/firesim`
   - `source sourceme-manager.sh --skip-ssh-setup`

当前 live run 的真实状态：

- session：
  `pairbert-b8-d12s128-run19`
- instance id：
  `i-03e660582d21388b0`
- private ip：
  `192.168.1.55`
- host watchdog 已经连续记录到：
  - `hb='17943834272, 948' idle=325s`
- `guest_sparse` 在到达
  `2465092`
  后不再增长
- FireSim manager 仍认为：
  - 实例还在运行
  - simulation 还在运行

因此这轮 run19 的结论已经收敛为：

- 不是 Linux early boot 卡死
- 不是 yaml / artifact load begin 卡死
- completion pool 的 prefault + `mlock` 修正确实已经在 guest 中生效
- 当前真正 freeze frontier 仍是：
  `tok=2281`

`tok=2281` 的最后可见上下文：

- `stage=0`
- `tensor=0`
- `kind=3`
- 最后几行是：
  - `dma-submit-inner ... phase=after-trace-count tok=2281 ...`
  - `dma-submit-inner ... phase=after-trace-event tok=2281 kind=3`
  - `dma-submit-inner ... phase=after-capture-req tok=2281 done_va=0x0`
  - `dma-submit-inner ... phase=before-rr-marker tok=2281 mgr=0`
  - `dma-submit-inner ... phase=after-rr-marker tok=2281 mgr=0`
  - `rr-acquire-inner phase=before-csr-write ...`
  - `rr-acquire-inner phase=after-csr-write ...`
  - `rr-acquire-inner phase=after-csr-read ... acquired=1`
  - `rr-acquire-inner phase=before-set-opc ...`
  - `rr-acquire-inner phase=after-set-opc ...`

静态代码事实：

- 在
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  中，`after-set-opc` 之后本应紧接着经过：
  - `rr-acquire-postcheck`
  - `rr-state-install-end`
  - `doneflag-clear`
  - `doneflag-begin`
  - `doneflag-end`
  - `program-*`
  - `wait-fence-done`

另一个必须记住的观测面缺陷：

- 当前 build 开了：
  - `PIPELINE_RUNTIME_ONLY_MARKER=1`
  - `PIPELINE_RUNTIME_PROGRESS_HOT=1`

## 2026-04-12 05:28 UTC 交接补充：run20 已明确越过旧 tok=2281 blocker

如果接手时已经不是 run19，而是 run20，请先用这条更新覆盖更旧的判断：

- 当前 live：
  - session：
    `pairbert-b8-d12s128-run20`
  - instance id：
    `i-01d949923bf32b492`
  - private ip：
    `192.168.1.239`
- 这轮在启动前已再次完成：
  - `infrasetup`
  - remote image freshness check
  - 然后才启动 `runworkload`
- 当前最重要的新结论：
  - live image 文件日志已经推进到
    `stage=0 / tensor=0 / tok=2596`
  - 所以旧 run19 的
    `tok=2281`
    blocker 已经被**明确越过**
- 更关键的是，旧 blocker 后面的新稀疏日志已经在 live 中真实出现：
  - `after-rr-postcheck`
  - `after-rr-state-install`
  - `after-doneflag-clear`
  - `before-doneflag-acquire`
  - `after-doneflag-acquire`
  - `before-program-fence`
  - `after-program-fence`
  - `after-program-dst`
  - `after-program-src`
  - `dma-wait-inner phase=before-fence`
  - `dma-wait-inner phase=after-fence`
  - `dma-wait-inner phase=after-release`
- 已确认的 live 片段是：
  - `tok=2593`
    到
    `tok=2596`
    的 `stage=0 tensor=0` 小页 DMA
    都完整走通了
- 因而对旧问题的判断要改成：
  - DMA completion 主路径修正已经在 `sbus128` live hardware 上生效
  - 旧 `after-set-opc -> doneflag/program/wait` 区间不再冻结
  - 这不是“碰巧绕过”，而是根因路径已经真实打通
- 接手后的动作：
  1. 继续只用私网 IP `192.168.1.239`
  2. 优先看 guest 文件日志，不要回退到 UART 作为主观测面
  3. 把当前 frontier 往更后追，判断 run20 是自然跑通，还是在更靠后的新位置出现新的稳定 freeze
- 当前 guest env 仍是：
  - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
- 于是 `PRT_MARKER_LOG` / `PRT_PROGRESS_HOT_LOG` 可能被错误路由到 deep log 路径，
  在 deep log 关闭时直接消失
- 所以当前 sparse log 只能稳定看到 `PRT_PROGRESS_LOG`

下一轮建议顺序：

1. 先静态缩点 `tok=2281` 这条 `stage=0 tensor=0 kind=3` 到底对应什么 DMA 语义
2. 修正或绕过 marker / hot log 路由缺陷
3. 如果要重跑，必须按 FireSim 正规链路：
   - `terminaterunfarm`
   - 确认实例真正终止
   - 如 image / workload / binary / env 有变更，再重新 `infrasetup`
   - 只用私网 IP 做 remote freshness check

## 2026-04-12 07:35 UTC 交接补充：run23 当前不是旧 DMA blocker，而是 `ours2 / segment=1 / tensor=3` 的 `ALL_RINGBUFFER` 生产侧前沿

如果接手时还是 `run23`，先用这条补充覆盖更早“run20 新前沿未定”的判断。

- 当前 live：
  - run：
    `pairbert-b8-d12s128-run23`
  - instance id：
    `i-040bca6fca765b51b`
  - private ip：
    `192.168.1.175`
  - 只允许用这个私网地址继续 SSH / remote inspect
- 当前 guest status/runner 已确认：
  - `state=running`
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=192`
  - 所以实际在跑的是：
    `pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
- 这份 `ours2` mapping 的 `segment=1` 与当前 live 完全对上：
  - `segment=1` 有 6 个 stage
  - `tensor=3`
    - `stage0 exportTensorTypeList: [ALL_RINGBUFFER]`
    - `stage2 entryTensorTypeList: [ALL_RINGBUFFER, DRAM]`
    - ring config：
      `ring_buffer_count: {3: 2}`
      `ring_buffer_size_per: {3: 64}`
      `ring_buffer_use_count: {3: 1}`
  - `tensor=8`
    也是后半段的另一条 `ALL_RINGBUFFER`
- 当前 live 文件里最后可见的新前沿是：
  - `worker stage=2 subbatch=0 waiting phase=entry-c7-ring-ready kind=c7-entry-allring tensor=3 ... ring=0/0/2`
  - 这表示 `stage2` 正在等 `stage0` 的 `tensor=3` producer publish
- 一个关键静态结论必须记住：
  - `C8` 首拍不是 bug，不是“首个 subbatch 被丢”
  - 正确流程是：
    1. `stage_wait_exports_ready()` 先对 `C8` 做 pre-prime
    2. compute 直接写 ring slot
    3. compute 后 `prt_process_c8()` 才 `ring_fill_locked()`
  - 所以当前 `c7 wait` 不能直接归咎于 ring 首拍语义
- 当前更可疑的真正前沿是 producer 侧：
  - `stage0 / tensor=3` 是内部 `ALL_RINGBUFFER`
  - 但 runtime 仍会在真正 `C8 publish` 之前先跑：
    `sync_stage_export_aliases()`
  - 这条路径会把 ring slot 页再次通过
    `copy_tensor_pages_to_model_aliases()`
    materialize 回 model alias
  - 也就是说：
    当前 `stage2` 在等 ring ready，
    但 `stage0` 可能还串行卡在“内部 transport tensor 的 alias sync”上
- 当前观测面限制：
  - `heartbeat.csv` 一直涨，只能说明仿真仍在跑，不能证明 guest 软件一定前进
  - `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=0`
    所以 `runner.stage/runner-proc.stage` 不是实时刷盘
  - 当前 status 已确认：
    - `periodic_sync_enable=1`
    - `periodic_sync_seconds=1`
    - `stdio_capture_mode=log`
    - `checkpoint_log_enable=1`
    - `mlockall_mode=2`
  - 即：
    wrapper 在定期 `sync`，但 runtime 自己的文件输出仍可能因为进程内部 flush 不及时而滞后

接手后的优先顺序：

1. 继续只用私网 `192.168.1.175` 监控 `run23`
2. 判断它是否自然越过 `tensor=3` 的 `c7-entry-allring` 等待
3. 如果稳定不前，不要先改 `prt_ring_wait_ready()`；先缩点：
   - `sync_stage_export_aliases(stage0, tensor=3)`
   - `copy_tensor_pages_to_model_aliases(tensor=3)`
   - `C8 publish` 真正发生前是否被这条 alias sync 串行阻塞
4. 如果要重跑：
   - 保留 `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
   - 保留私网 SSH / remote freshness 闭环
   - 先增强观测：
     - `runner stage sync`
     - runtime 文件 flush / checkpoint 可见性
   - 然后再考虑细日志重跑

## 2026-04-12 07:28 UTC 交接补充：旧 `run23` 已停止，接手后不要再继续观察旧实例，直接进入 fresh image / rerun

如果接手时看到更早的描述还在说“继续观察 run23”，先用这条覆盖。

- `run23` 的最终静态结论保留：
  - live 前沿对应
    `ours2 / segment=1 / tensor=3`
    的内部
    `ALL_RINGBUFFER`
  - 更可疑的不是
    `c7-entry-allring`
    wait 自身，而是 producer 在真正 publish 前仍然先跑了：
    - `sync_stage_export_aliases()`
    - `copy_tensor_pages_to_model_aliases()`
  - 对“当前 segment 内仍有 entry consumer 的内部 `ALL_RINGBUFFER` export tensor”，这条 alias materialization 不应发生
- 基于这个结论，已经在
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
  落下新的修正：
  - 当
    `export_buf->kind == PRT_BUF_C8_EXPORT_ALL_RING`
    且
    `has_entry_consumer_for_tensor(rt, tensor_id)`
    时，
    `sync_stage_export_aliases()`
    直接跳过 alias sync
  - 新日志：
    `export-sync skip stage=%u tensor=%u reason=internal-all-ring-consumer`
- 另外还改了 guest 观测默认值：
  - 在
    `host-init-fileonly-sync-pairdummy.sh`
    中，
    `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE`
    默认从
    `0`
    改成了
    `1`
  - 目的是让
    `runner.stage`
    /
    `runner-proc.stage`
    在 live 时更及时刷盘
- 编译硬约束继续按最严格版本执行：
  - **任何编译动作之前**都必须先：
    - `cd /home/ubuntu/chipyard/sims/firesim`
    - `source sourceme-manager.sh --skip-ssh-setup`
  - 然后才允许回 repo 根目录继续：
    - `source env.sh`
    - workload/binary/image 相关编译或重建动作
- 本地 runtime binary 已按上面约束重新编译成功：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- 旧 `run23` 实例已经不要再用了：
  - instance id：
    `i-040bca6fca765b51b`
  - private ip：
    `192.168.1.175`
  - 规范终止会话：
    `pairbert-b8-d12s128-term23b`
  - `terminaterunfarm`
    已返回
    `Instances terminated`
  - AWS 状态已进入
    `shutting-down`

接手后的正确顺序已经变成：

1. 等旧实例彻底退出
2. 用
   `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
   对
   `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
   重新执行：
   - `marshal build`
   - `marshal install`
3. 确认本地 image freshness check 通过
4. 继续只用私网做 remote image freshness check
5. 用
   `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
   重新执行：
   - `launchrunfarm`
   - `infrasetup`
   - `runworkload`
6. 下一轮重点验证：
   - `ours2 / segment=1 / tensor=3`
     是否越过旧
     `c7-entry-allring`
     frontier
   - `export-sync skip ... internal-all-ring-consumer`
     是否出现
   - `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=1`
     后
     `runner.stage`
     的可见性是否明显改善

## 2026-04-12 07:49 UTC 交接补充：run24 已越过旧 ring wait blocker，但新的稳定冻结点在 `segment=0 action-generate end` 之后

如果接手时看到更早描述还停在 `run23` 或“正在观察 layer-mapping parse”，先用这条覆盖。

- 当前 fresh rerun 已完成：
  - 旧 `run23` 正规终止
  - `marshal clean -> build -> install`
    已完成
  - local image freshness：PASS
  - 新实例：
    `i-0adc648f1ec21fcfb`
  - private ip：
    `192.168.1.138`
  - `launchrunfarm`：PASS
  - `infrasetup`：PASS
  - remote image freshness：PASS
    - local / remote sha256：
      `f5b18224ff2f3d9d4f4c80cfabc96e7f6a4723ac051fac244602c1d498ad8ef4`
  - `runworkload` session：
    `pairbert-b8-d12s128-run24`
- 这轮已经明确越过：
  - Linux boot
  - wrapper / runner early
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=206`
  - pipeline yaml 完整 parse / validate
  - layer-mapping 大文件 open / pread / parse progress
  - synthetic model alloc / prefault / mlock
- 当前最新稳定前沿：
  - `bertmini-batch8.log`
    最后停在：
    - `init ready segments=13 pipeline_subbatch_size=1 batch=8`
    - `segment=0 init begin stages=1 seg_subbatch_size=1 is_last=0`
    - `segment=0 action-generate begin`
    - `segment=0 action-generate end action=1`
  - 同时：
    - `heartbeat.csv`
      继续从
      `665`
      增长到
      `705`
    - `uartlog` 字节数稳定在
      `15688`
    - `bertmini-batch8.log`
      的
      `size=305051`
      /
      `mtime=guest 6s`
      保持不变
- 当前结论：
  - 新冻结点已经可先收敛为：
    **`segment=0 action-generate end action=1` 之后**
  - 从源码看，
    `prt_action_generate()`
    本身只是轻量分配；
    更可疑的是它后面的
    `prt_action_alloc_acc()`
    /
    `prt_action_alloc_spm()`
    路径
- 为了缩点这条新前沿，已经加了新的 instrumentation：
  - `prt_runtime.c`
    新增：
    - `segment=%u action-alloc-acc begin action=%u`
    - `segment=%u action-alloc-acc end action=%u unique_g=%u unique_d=%u`
  - `prt_schedule_action.c`
    新增：
    - `alloc-acc enter`
    - `alloc-acc stage-assign-ready`

接手后的正确顺序：

1. 终止 `run24`
2. 按硬约束重新执行：
   - `marshal build`
   - `marshal install`
3. 再做 local / remote image freshness check
4. 重新走：
   - `launchrunfarm`
   - `infrasetup`
   - `runworkload`
5. 重点验证新日志到底最远推进到：
   - `action-alloc-acc begin`
   - `alloc-acc enter`
   - `alloc-acc stage-assign-ready`
   - `alloc-acc done`
   - `action-alloc-spm begin`

## 2026-04-12 08:47 UTC 交接补充：run25 已越过 `run24` 的 `action-generate` frontier，但新的稳定冻结点在 `rr-acquire-inner ... after-set-opc`

如果接手时看到更早描述还停在 `run24 / action-generate end`，先用这条覆盖。

- 当前 run：
  - session：
    `pairbert-b8-d12s128-run25`
  - instance：
    `i-07cd6a9a82513be23`
  - private ip：
    `192.168.1.65`
  - remote image freshness：PASS
    - local / remote image sha256：
      `7cd382cac89f79f3591a9b1a37d348dd77fc086f0c25a8563d65559917d48eee`
- host watchdog 关键时间线：
  - `guest-status arm observed at 2026-04-12T08:33:54Z hb='6937872059, 384'`
  - `guest_sparse` 先后增长到：
    - `938`
    - `1001`
    - `1899`
    - `93765`
    - `197618`
    - `213454`
    - `309459`
  - 之后
    `heartbeat`
    继续从
    `560`
    增长到
    `801`
    ，但
    `guest_sparse`
    连续约
    `244s`
    保持
    `309459`
    不变
- 这轮已经明确越过：
  - Linux boot / wrapper / runner
  - `after-prepare-hugetlb`
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=208`
  - pipeline yaml parse / validate
  - layer-mapping load / validate
  - synthetic model alloc / prefault / mlock
  - `segment=0 action-generate end action=1`
  - `segment=0 action-alloc-acc begin/end`
  - `segment=0 action-alloc-spm begin/end`
  - `segment=0 build-topology`
  - `segment=0 prepare-stage-spm`
  - `segment=0 bind-topology`
  - `segment=0 flush-spm-xlate`
  - `segment=0 worker-create`
- 当前最新稳定 guest 文件前沿：
  - `bertmini-batch8.log`
    末尾停在：
    - `rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-csr-write stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-csr-read stage=0 manager=0 opcode=2 cfg=0 ... acquired=1`
    - `rr-acquire-inner phase=before-set-opc stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-set-opc stage=0 manager=0 opcode=2 cfg=0`
- 当前静态结论：
  - 新冻结点已从
    `action-generate end`
    缩到：
    **`prt_rr_acquire_scope()` 返回边界附近**
  - 更具体地说，是：
    - `after-set-opc`
      之后
    - `after-rr-postcheck`
      之前
  - 在
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    中，这两者之间按源码只剩非常短的返回/检查路径，所以需要把 return 边界打透
- 本轮已补的新 instrumentation：
  - `prt_rerocc.c`
    - `rr-acquire-inner phase=before-scope-valid`
    - `rr-acquire-inner phase=after-scope-valid`
    - `rr-acquire-inner phase=before-return`
  - `prt_dma.c`
    - `dma-submit-inner ... phase=after-rr-acquire-call`

接手后的正确顺序：

1. 终止 `run25`
2. 按硬约束重新执行：
   - `marshal build`
   - `marshal install`
3. 再做 local / remote freshness check
4. 重新走：
   - `launchrunfarm`
   - `infrasetup`
   - `runworkload`
5. 重点验证新日志最远推进到：
   - `after-scope-valid`
   - `before-return`
   - `after-rr-acquire-call`
   - `after-rr-postcheck`

## 2026-04-12 09:08 UTC 交接补充：run26 已越过 `after-set-opc`，freeze 进一步收敛到 `before-return -> after-rr-acquire-call`

如果接手时看到上一条还停在 “`after-set-opc` 之后”，先用这条覆盖。

- 当前 run：
  - session：
    `pairbert-b8-d12s128-run26`
  - instance：
    `i-033962c438384a555`
  - private ip：
    `192.168.1.63`
  - `infrasetup26`：
    exit code `0`
  - remote image freshness：PASS
    - runtime binary sha256：
      `a4839607a339c538e4417d68ea9b0a74bdd8cdc08450ef1a14d791584f573124`
- host watchdog 关键时间线：
  - `guest-status arm observed at 2026-04-12T09:03:13Z hb='6936546069, 384'`
  - `guest_sparse` 增长到：
    - `938`
    - `1504`
    - `2293`
    - `98304`
    - `197618`
    - `213453`
    - `309730`
  - 随后
    `heartbeat`
    从
    `560`
    继续涨到
    `624`
    ，但
    `guest_sparse`
    保持
    `309730`
    不变
- 这轮已经明确越过：
  - `run25` 的旧前沿
  - `rr-acquire-inner phase=after-set-opc`
  - `rr-acquire-inner phase=before-scope-valid`
  - `rr-acquire-inner phase=after-scope-valid`
  - `rr-acquire-inner phase=before-return`
- 当前最新稳定 guest 文件前沿：
  - `bertmini-batch8.log`
    末尾停在：
    - `rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-csr-write stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-csr-read stage=0 manager=0 opcode=2 cfg=0 ... acquired=1`
    - `rr-acquire-inner phase=before-set-opc stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-set-opc stage=0 manager=0 opcode=2 cfg=0`
    - `rr-acquire-inner phase=before-scope-valid stage=0 manager=0 opcode=2 cfg=0`
    - `rr-acquire-inner phase=after-scope-valid stage=0 manager=0 opcode=2 cfg=0 valid=1`
    - `rr-acquire-inner phase=before-return stage=0 manager=0 opcode=2 cfg=0`
  - 但仍看不到：
    - `dma-submit-inner ... phase=after-rr-acquire-call`
    - `dma-submit-inner ... phase=after-rr-postcheck`
- 当前静态结论：
  - freeze 区间已进一步缩窄为：
    **`before-return` 之后、`after-rr-acquire-call` 之前**
  - 这意味着：
    - `scope->valid = 1`
      本身已经不是 blocker
    - `prt_rr_acquire_scope_cfg()` / `prt_rr_acquire_scope()` 的真实返回边界是新的责任点
  - 结合源码，优先怀疑：
    - 函数返回边界 / 调用约定
    - 或 `rr_set_opc()` / CSR 包装的副作用

接手后的正确顺序：

1. 先保留 `run26` 现状，继续把 live 证据抓完整
2. 静态检查：
   - `prt_rerocc.c`
     中 `prt_rr_acquire_scope_cfg()` / `prt_rr_acquire_scope()`
   - `rerocc_control.h`
     中 `rr_set_opc()` / `rr_write_csr()` / `rr_swap_csr()`
   - 已编译 runtime binary 的反汇编，确认函数尾和调用点
3. 若要继续加 instrumentation，优先加在：
   - `prt_rr_acquire_scope()` 包装函数入口/返回前
   - `prt_dma.c` 的 `prt_rr_acquire_scope(...)` 调用前后 raw marker
   - 必要时对返回值赋给 `rrc` 前后分别加 raw marker

## 2026-04-12 10:18 UTC 交接补充：run28 之后已经修掉 completion slot 泄漏，当前 run29 卡在 AWS 容量，不在软件执行期

- `run28` 的最强动态证据：
  - checkpoint 文件已明确越过：
    - `rr-acquire-end`
    - `program-post-src`
    - `wait-fence-done`
  - 新稳定 freeze 停在：
    - `doneflag-begin`
      之后
    - `doneflag-end`
      之前
- 对应的静态实锤缺陷位于：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  的
  `dma_blocking_submit_and_wait()`
  ：
  - 旧实现没有对栈上 `tok` 调
    `prt_dma_token_cleanup(&tok)`
  - 因而 completion flag slot 不释放
  - 这与 `run28` 后段在
    `dma_completion_flag_acquire()` /
    `dma_debug_capture_done_flag()`
    邻域冻结高度一致
- 这条 bug 已在当前工作区修复：
  - `dma_blocking_wait(...)`
    返回后立即执行：
    `prt_dma_token_cleanup(&tok)`
  - 返回码保持不变
- 本轮 fresh rebuild/install 已完成：
  - build session：
    `pairdummy-prt-build36a`
  - install session：
    `pairdummy-prt-install35`
  - local freshness：PASS
  - runtime binary sha256：
    `4bd2d2c0e04c736052e97917f8ebe6ece5b1b28046cd340c056f5934cf048161`
  - firemarshal env sha256：
    `2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
- 旧 live run 已处理：
  - `run28` instance：
    `i-067e360e239e22759`
  - private ip：
    `192.168.1.229`
  - 已执行：
    `terminaterunfarm --forceterminate`
  - AWS 状态进入：
    `shutting-down`
- 当前 live 状态：
  - `launch29` session：
    `pairbert-b8-d12s128-launch29`
  - FireSim manager 正在自动重试
    `f2.6xlarge`
    容量
  - pane log 里最新是：
    `insufficient capacity to launch your instances`
  - 所以当前不是软件 freeze，也还没进入 guest 执行

接手后的正确顺序：

1. 先看
   `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch29.pane.log`
   是否已经成功拿到新实例
2. 一旦 launch 成功，立刻做：
   - `infrasetup29`
   - 用**私网 IP**执行 remote freshness：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`
3. 然后带着 host monitor 环境变量重新起：
   - `run29`
4. 重点验证：
   - 是否越过 `run28` 的
     `doneflag-begin -> doneflag-end`
     冻结区间
   - 是否继续越过
     `segment=0 sink-progress=6/8`
5. 如果修后仍卡在 doneflag 邻域，再只对：
   - `dma_completion_flag_acquire()`
   - `dma_debug_capture_done_flag()`
   前后加更细 marker

额外约束提醒：

- 全程中文。
- SSH / 远端 inspect / freshness 只能用**私网 IP**。
- FireSim 命令必须通过：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- FireMarshal 命令必须通过：
  `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
- 任何编译动作前必须先：
  1. `cd /home/ubuntu/chipyard/sims/firesim`
  2. `source sourceme-manager.sh --skip-ssh-setup`
  3. 回 repo 根 `source env.sh`
- guest 文件日志仍是主观测面，不要把 UART 当主观测面。
- stale image 闭环必须持续做：
  - local freshness
  - remote freshness
- `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
  必须保留。
- 任何 `build/install/launch/infrasetup/runworkload/terminaterunfarm`
  都优先传**绝对路径**，因为 tmux wrapper 的工作目录不是 repo root。

## 2026-04-12 10:44 UTC 交接补充：run29 已穿过旧 doneflag blocker，新前沿是更后的 `rr-acquire-begin -> rr-acquire-end`

- `run29` 已确认穿过旧 blocker：
  - `guest_sparse` 不再卡在 `402911`
  - 已增长到：
    `404707`
  - `checkpoint.log` 已增长到：
    `1975927`
  - 尾部连续出现大量：
    - `doneflag-end`
    - `program-post-src`
    - `wait-fence-done`
- 因而：
  - `dma_blocking_submit_and_wait()` 的 token cleanup 修复是有效的
  - 旧 completion slot 泄漏 blocker 已被真正修掉
- `run29` 的新稳定 freeze：
  - heartbeat 继续从
    `11183439481, 609`
    增长到
    `20602259128, 1084`
  - 但 guest 文件稳定不再增长：
    - sparse log：
      `404707`
    - checkpoint log：
      `1975927`
  - checkpoint 最后一条稳定停在：
    `dma stage=0 checkpoint=rr-acquire-begin tensor=1000001 src=0x10475c800 dst=0x40205800 bytes=1024 dst_acc=0`
  - 后面没有：
    `rr-acquire-end`
- 当前静态理解：
  - 新 frontier 在：
    `rr-acquire-begin`
    之后，
    `rr-acquire-end`
    之前
  - 但还不能直接断言一定死在真实
    `prt_rr_acquire_scope()`
    调用里
  - 需要先区分：
    1. 本次是 `reuse scope`
       还是 `real acquire`
    2. 若是 `real acquire`，卡在：
       - `before-csr-write`
       - `after-csr-write`
       - `after-csr-read`
       - retry loop
       - `rr_set_opc()`
       - return 边界
- 为此，当前工作区已新增下一轮 checkpoint instrumentation：
  - `prt_dma.c`
    - `rr-scope-state`
    - `rr-acquire-call-begin`
    - `rr-acquire-call-end`
    - `rr-acquire-reuse`
  - `prt_rerocc.c`
    - `before-csr-write`
    - `after-csr-write`
    - `after-csr-read`
    - `retry`
    - `before-set-opc`
    - `after-set-opc`
    - `before-scope-valid`
    - `after-scope-valid`
    - `before-return`
- 这些改动已经 fresh rebuild/install：
  - build session：
    `pairdummy-prt-build37`
  - install session：
    `pairdummy-prt-install36`
  - local freshness：PASS
  - 新 runtime binary sha256：
    `19fe6ed9a9d65262da86e534ff8ef25eaa313c6bc9b6bd1ef626ba2eab370bde`
- `run29` 已终止：
  - instance：
    `i-0cf51fec4fa523dca`
  - private ip：
    `192.168.1.162`
- 当前 live 状态：
  - `launch30` session：
    `pairbert-b8-d12s128-launch30`
  - 已成功拿到新实例：
    - instance：
      `i-00c8e36972f4291ee`
    - private ip：
      `192.168.1.24`
  - `infrasetup30`：PASS
  - remote freshness：PASS
    - remote runtime binary sha256：
      `19fe6ed9a9d65262da86e534ff8ef25eaa313c6bc9b6bd1ef626ba2eab370bde`
  - `run30` session：
    `pairbert-b8-d12s128-run30`

## 2026-04-12 11:02 UTC 交接补充：run30 已启动，但当前仍在 guest 早期 boot，尚未到上轮 runtime freeze 前沿

- 当前 live 观测：
  - host watchdog 正在盯：
    `192.168.1.24`
  - heartbeat 已增长到：
    `2851323873, 164`
  - 远端文件日志目前还没有内容：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
  - UART 仅作为早期 boot 辅助，已看到：
    - OpenSBI
    - Linux kernel early boot
- 这还不能判定异常：
  - `run29` 中 guest-status arm 首次出现是在 heartbeat：
    `7086493407, 392`
  - 所以当前 `run30` 仍早于上轮进入文件日志阶段的时间点

接手后的正确顺序：

1. 继续监控：
   - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run30.monitor.log`
   - `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run30.pane.log`
   - 私网实例 `192.168.1.24` 上的 guest 文件日志
2. 等 guest 文件日志开始出现内容后，先判断是否再次进入：
   - `rr-acquire-begin`
     之后
   - `rr-acquire-end`
     之前
3. 一旦进入新的 checkpoint frontier，重点看末尾到底停在：
   - `rr-scope-state`
   - `rr-acquire-call-begin`
   - `rr-acquire-reuse`
   - `before-csr-write`
   - `after-csr-write`
   - `after-csr-read`
   - `retry`
   - `before-set-opc`
   - `after-set-opc`
   - `before-return`
4. 然后再决定是修：
   - RR scope 生命周期
   - cfg 复用/并发冲突
   - 还是 CSR / `rr_set_opc()` / 返回边界问题

## 2026-04-12 11:16 UTC 交接补充：run30 已复现 freeze，但 broad stage0 checkpoint 扰动了前沿；当前已把 checkpoint 收窄到 `tensor_id >= 1000000`

- `run30` 结果：
  - host watchdog 看到
    `guest_sparse`
    增长为：
    `1504 -> 197618 -> 214485 -> 311620 -> 402910`
  - 到 heartbeat
    `11181643686, 609`
    后停止增长
  - heartbeat 继续涨到：
    `13745373055, 739`
  - 所以 `run30` 仍确认存在新的更后 freeze
- 这轮新增证据：
  - 从远端 image 里的
    `bertmini-batch8.checkpoint.log`
    抓到：
    - `rr-scope-state tensor=0 have_scope=1 scope_valid=1 scope_cfg=0 rr_mgr=0 rr_opc=2`
  - 最后一行只剩半行：
    - `rr-acquire-reuse ten`
  - 最保守结论：
    - 当前观测点不是卡在真实
      `prt_rr_acquire_scope()`
      调用内部
    - 至少这次捕获到的点已经在
      `have_scope=1`
      reuse 分支里
- 但不要把 `run30` 的 `tensor=0` reuse 点当成最终根因：
  - `run29` 原始 frontier 是
    `tensor=1000001`
  - `run30` 对所有 stage0 DMA 都开 checkpoint，log 量明显增大，已经扰动行为
  - 因而当前更合理策略是把 checkpoint 收窄回旧 frontier 附近
- 已做修正：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  - 把：
    - `checkpoint_submit`
    - `checkpoint_wait`
  - 都从
    `stage0 全量`
    收窄为：
    `tok->stage_idx == 0U && tok->tensor_id >= 1000000U`
- 新 artifact 已 fresh build/install：
  - build session：
    `pairdummy-prt-build38`
  - install session：
    `pairdummy-prt-install37`
  - local freshness：PASS
  - runtime binary sha256：
    `4ce1b924f13547db7b5d3fb22de19c3f23e2e2c38507a913cbe468993394eca5`
- `run30` 已终止：
  - instance：
    `i-00c8e36972f4291ee`
  - private ip：
    `192.168.1.24`
- 当前 live 状态：
  - `launch31` session：
    `pairbert-b8-d12s128-launch31`
  - 当前因为 AWS：
    `insufficient capacity to launch your instances`
    在 retry

接手后的正确顺序：

1. 先看
   `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch31.pane.log`
   是否成功拿到新实例
2. launch 成功后立刻做：
   - `infrasetup31`
   - 用**私网 IP**做 remote freshness，确认远端 binary sha256 是
     `4ce1b924f13547db7b5d3fb22de19c3f23e2e2c38507a913cbe468993394eca5`
3. 再带 host monitor 环境变量起：
   - `run31`
4. `run31` 的判断重点：
   - 看 freeze 是否回到原始
     `tensor=1000001`
     fixed-load frontier
   - 如果回到原始 frontier，再根据收窄后的 checkpoint 判断：
     - `rr-scope-state`
     - `rr-acquire-call-begin`
     - `rr-acquire-reuse`
     - `rr-acquire-call-end`
     - `rr-acquire-end`

## 2026-04-12 12:xx UTC 交接补充：run31 已确认真实 frontier 回到 `tensor=1000001`，现在卡在 `doneflag-begin` 之后；已继续把观测收窄到 completion-slot acquire/release

- 当前 live run：
  - session：
    `pairbert-b8-d12s128-run31`
  - instance：
    `i-015953798c8a09223`
  - private ip：
    `192.168.2.51`
- host watchdog：
  - `guest_sparse`
    增长到：
    `1504 -> 197619 -> 214744 -> 311621`
  - 在 heartbeat
    `585`
    后不再增长
  - heartbeat 继续涨到至少：
    `818`
- 远端 image 内
  `bertmini-batch8.checkpoint.log`
  尾部稳定为：
  - `rr-acquire-begin tensor=1000001 ...`
  - `rr-scope-state ... have_scope=1 scope_valid=1 scope_cfg=0 rr_mgr=0 rr_opc=2`
  - `rr-acquire-reuse tensor=1000001 ...`
  - `rr-acquire-end tensor=1000001 rc=0 ...`
  - `doneflag-begin tensor=1000001`
- 也就是：
  - 真实 frontier 已不再是 `run30` 的 `tensor=0`
  - 现在稳定卡在：
    `dma_debug_capture_done_flag()`
    / `dma_completion_flag_acquire()`
    一段
- 新静态结论：
  - `PRT_PROGRESS_HOT_LOG`
    实际写到
    `deep.log`
  - 当前 workload：
    `guest_deep_log_enable=0`
  - 所以 `doneflag-begin` 后的 hot log 不会自己成为 blocker
- 已做新代码修改：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  - 新增：
    `dma_should_checkpoint_doneflag_tok()`
  - 对
    `stage0 && tensor_id >= 1000000`
    的 completion-slot 路径补最小 checkpoint：
    - `doneflag-acquire-enter`
    - `doneflag-acquire-pool`
    - `doneflag-acquire-lock`
    - `doneflag-acquire-scan`
    - `doneflag-acquire-exit`
    - `doneflag-release-enter`
    - `doneflag-release-lock`
    - `doneflag-release-exit`
  - 另外对 completion mutex 增加：
    - 先 `pthread_mutex_trylock()`
    - 若 busy，再 `pthread_mutex_timedlock()` 1 秒
  - 目的：
    - 直接区分：
      - 锁立刻可拿
      - 锁长时间 busy
      - 槽位扫描失败
      - 还是 acquire 返回后才出问题

接手后的正确顺序更新为：

1. 先终止 `run31`
   - 用 FireSim manager wrapper
   - 必须继续使用私网 IP 观测
2. fresh build / install 前，严格执行：
   - `cd /home/ubuntu/chipyard/sims/firesim`
   - `source sourceme-manager.sh --skip-ssh-setup`
   - 回 repo 根：
     `source /home/ubuntu/chipyard/env.sh`
3. 重新 build / install 后，做：
   - local freshness
   - remote freshness
4. 重新 `launchrunfarm -> infrasetup -> runworkload`
5. 新一轮重点看 checkpoint 是否进入：
   - `doneflag-acquire-enter`
   - `doneflag-acquire-pool`
   - `doneflag-acquire-lock`
   - `doneflag-acquire-scan`
   - `doneflag-acquire-exit`
6. 如果卡在 `doneflag-acquire-lock`：
   - 说明 completion mutex 被别处长时间持有，继续向 release 路径和上游状态破坏排查
7. 如果穿过 `doneflag-acquire-exit` 仍挂：
   - 再把 frontier 往 `program-begin` / CSR 编程后推进

## 2026-04-12 12:xx UTC 交接补充：run32 证明上一版 doneflag instrumentation 仍然扰动 live 行为，frontier 被拉回更早的 `rr-acquire-inner before-csr-write`

- `run32` 实例：
  - instance：
    `i-09e6930b736eba3f3`
  - private ip：
    `192.168.2.28`
- remote freshness：PASS
  - remote image sha256：
    `b4a803bac4724ce33a57e1628acf4631685c88c1cd649c22a7fb17cc0017c8f1`
  - local runtime binary sha256：
    `65b3d47e5d69f4153306015d785dfbcd4ffa1f8e4fa6b52e7697ae6fdbe248d4`
- host watchdog：
  - `guest-status arm`
    仍与 `run31` 时间接近：
    `hb='7087832023, 392'`
  - `guest_sparse`
    继续增长到：
    `1504 -> 198109 -> 215001 -> 309036`
  - 然后 heartbeat 继续涨到至少：
    `769`
    但 `guest_sparse` 固定不动
- image 内 guest 文件：
  - `bertmini-batch8.log` =
    `309036`
    字节
  - `bertmini-batch8.checkpoint.log` =
    `0`
    字节
- 当前稳定 tail：
  - `rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 csr=0x810 wdata=0x100`
- 结论：
  - 当前 run32 **没有**重新进入 `run31` 的
    `tensor=1000001`
    doneflag checkpoint 路径
  - 更合理解释是：
    上一版
    `pthread_mutex_timedlock + release checkpoint`
    的额外代码体量再次扰动了 live 行为
- 已继续缩减代码改动：
  - 移除 release 路径新增 checkpoint
  - 移除 `pthread_mutex_timedlock()`
  - 只保留 acquire 路径最小 checkpoint：
    - `doneflag-acquire-enter`
    - `doneflag-acquire-pool`
    - `doneflag-acquire-lock`
    - `doneflag-acquire-scan`
    - `doneflag-acquire-exit`

后续正确顺序更新为：

1. 终止 `run32`
2. fresh build / install
3. 重新做 remote freshness
4. 重跑 FireSim
5. 首先判断 frontier 是否重新回到 `run31` 的：
   - `tensor=1000001`
   - `doneflag-begin -> doneflag-end`
6. 只有重新回到该 frontier 后，才继续依据新的 acquire-only checkpoint 判断锁 / 槽位问题

## 2026-04-12 最新追加：当前 live frontier 已经推进到 `segment=0 action-alloc-spm begin`，不是 yaml load，也不是之前的 worker-create

1. 这轮关键 live 是 `run36`：
   - instance：
     `i-0dc22546725ac2986`
   - private ip：
     `192.168.1.140`
   - 远端观测必须继续只用私网 IP，不要用公网 IP
   - guest 主日志继续以 image 文件系统里的
     `/root/pipeline-runtime-debug/*`
     为主，不要把 `uartlog` 当主观测面

2. `run36` 已经真正越过了早先的 `artifacts mapping load` 路径：
   - `bertmini-batch8.log` 已看到：
     - `artifacts validate end`
     - `synthetic-model alloc before/after prefault`
     - `synthetic-model alloc before/after mlock`
     - `init ready`
     - `segment=0 action-generate`
     - `segment=0 action-alloc-acc`
   - 最后稳定停在：
     `segment=0 action-alloc-spm begin action=1`
   - `checkpoint.log` 仍是：
     `0`
     bytes

3. 这非常重要，因为当前 blocker 已经变化了：
   - 现在不再是 `mapping load`
   - 也还没走到之前加过探针的 `worker-create`
   - 当前真正前沿是：
     `prt_action_alloc_spm()`

4. 已做过静态缩点，排除了“segment 太大只是慢”这个假设：
   - 当前实际使用的编排文件：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
   - `segment 0` 只有：
     - `4` 个 `buffer binding`
     - `193` 个总页数
     - 类型是 `PIPE, PIPE, WEIGHT, WEIGHT`
   - 所以当前更像是卡在：
     - `alloc_action_alias_window`
     - `prt_spm_xlate_ctx_alloc`
     - `pt_pool_add_chunk / alloc_contig_pt_storage / probe_phys_contig_range`
     - 或第一个 binding 的 slot page alloc

5. 为此已经补了新的细粒度 progress probe，并且必须继续用它们做 fresh-image 闭环验证：
   - 文件：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c`
   - 新探针：
     - `alloc-spm enter`
     - `alloc-spm page-count`
     - `alloc-spm alias-window begin/end`
     - `alloc-spm xlate-ctx begin/end`
     - `alloc-spm binding-begin`
     - `alloc-spm ring-slot / weight / pipe-slot begin/end`
     - `alloc-spm binding-end`
   - 文件：
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
   - 新探针：
     - `spm-xlate-ctx alloc begin / slice end / end`
     - `spm-pt pool chunk-add begin / alloc-contig end`
     - `spm-pt alloc-contig begin`
     - `spm-pt hugetlb anon begin / mmap ok`
     - `spm-pt hugetlbfs begin / mmap ok`
     - `spm-pt anon begin / mmap ok`
     - `spm-pt probe begin / first-page begin / first-page end / fail / noncontig / end`

6. `run36` 已经停掉：
   - 正确命令：
     `firesim terminaterunfarm --forceterminate -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml -a /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml`
   - 当时实例状态已进入：
     `shutting-down`

7. 当前正在进行的新 fresh-image 闭环：
   - `marshal clean`：
     `pairdummy-prt-clean6`
     已完成
   - `marshal build`：
     `pairdummy-prt-build44`
     已启动
   - 这轮 build 要继续显式带上这些环境变量，避免“源码改了但 image 里不是新 binary”：
     - `DEEP_LOG_ENABLE=0`
     - `DUMMY_GEMMINI_MODE=1`
     - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0`
     - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=1`
     - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0`
     - `PIPELINE_RUNTIME_LOG_PROFILE=manual`
     - `PIPELINE_RUNTIME_ONLY_MARKER=0`
     - `PIPELINE_RUNTIME_PROGRESS=1`
     - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
     - `PIPELINE_RUNTIME_PROGRESS_RAW=1`
     - `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
     - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=log`

8. 后续必须继续遵守的硬约束：
   - SSH / 远端检查一律使用私网 IP
   - 编译前、FireMarshal 前：
     先 `cd /home/ubuntu/chipyard/sims/firesim && source sourceme-manager.sh --skip-ssh-setup`
     再 `cd /home/ubuntu/chipyard && source env.sh`
   - FireMarshal 只走：
     `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
   - FireSim workload 只走：
     `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
   - stale-image 闭环必须完整执行：
     `marshal clean -> marshal build -> marshal install -> local freshness -> launchrunfarm -> infrasetup -> remote freshness -> runworkload`
   - 正式日志以 guest 文件系统为主，不要重新退回 `uartlog`

9. 上面那轮 fresh-image 闭环后续已经继续推进完成了：
   - `pairdummy-prt-build44`：完成
   - `pairdummy-prt-install42`：完成
   - 新本地 runtime binary sha256：
     `d1ce235aafb2e2302af2f07cb6d2f24f0eb51ad904b69238a66668e3b828fc47`
   - 新 local / remote image sha256：
     `46600ead676100c343f9444b1f6f8a2a45bb1b82220e4528ec3d0706c2935390`

10. 新 FireSim live：
    - launch：
      `pairbert-b8-d12s128-launch37`
    - infrasetup：
      `pairbert-b8-d12s128-infrasetup37`
    - run：
      `pairbert-b8-d12s128-run37`
    - instance：
      `i-06bd49efe6f4a7950`
    - private ip：
      `192.168.1.129`

11. `192.168.1.129` 上 remote freshness 已再次 PASS：
    - image sha：
      `46600ead676100c343f9444b1f6f8a2a45bb1b82220e4528ec3d0706c2935390`
    - runtime binary sha in image：
      `d1ce235aafb2e2302af2f07cb6d2f24f0eb51ad904b69238a66668e3b828fc47`
    - guest env 仍确认：
      - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='1'`
      - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`

12. `run37` 当前还没回到 runtime 主线：
    - heartbeat 正常增长
    - `uartlog` 已到 Linux boot
    - 但 image 内还没有：
      - `/root/pipeline-runtime-debug/bertmini-batch8.status`
      - `/root/pipeline-runtime-debug/bertmini-batch8.log`
      - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
    - 因而目前仍处在 guest boot / 用户态前期
    - 不能把当前静默期误判成新的 runtime 卡点
    - 下一步应继续监控，直到这些 guest 文件出现，再用新的 alloc-spm probe 定位真实前沿
## 2026-04-12 15:10 UTC incremental update

- 新结论：
  - 用户确认的约束成立：`spm` 翻译页表若要求连续物理地址，则不能接受“普通匿名页 + 连续性探测”作为正式后备语义。
  - 当前 `run38` 的实际现场也证明，这一轮并没有真的走到普通匿名页 fallback；真实卡点仍是：
    - `spm-pt alloc-contig hugetlb-check need_pages=1 total_pages=1 free_pages=1 can_try=1`
    - `spm-pt hugetlb anon begin ...`
    - 之后不再出现 `mmap ok / failed`
- 已落实的新修复：
  - 文件：
    - `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
    - `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
    - `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - 逻辑：
    - `runtime_pt_chunk_bytes()` 在 `spm_xlate_enable=1` 时固定按单 hugepage chunk 处理
    - 多页物理连续请求强制 `require_hugetlb`
    - 优先走 `hugetlbfs`；拒绝多页普通匿名页 fallback
    - bertmini runner 显式补 `--spm-pt-require-hugetlb 1`
- fresh-image 闭环状态：
  - `clean9`: PASS
  - `build47`: PASS
  - `install44`: PASS
  - local freshness: PASS
    - runner-script sha256=`fdabde31d87372dfd82810ee79e0f9e8751e3af19d7b71400662ef1d491b25fa`
    - runtime-binary sha256=`849e8e60f766acc00076b6b7e6e824a5fae3e32ce43d4f67dfa4192d7c9bf62d`
    - firemarshal-env sha256=`2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
- FireSim 状态：
  - `run38` 已终止，旧实例 `i-089aa6c1a0ec02334` 处于 `shutting-down`
  - `launch39` 已启动，但当前暂时遇到 AWS `f2.6xlarge` 容量不足，manager 正在持续重试
