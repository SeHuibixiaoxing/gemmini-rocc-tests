# Current Status

更新时间：`2026-04-13 04:42 UTC`

## 2026-04-13 04:42 UTC 已建立独立调试记录目录；当前最新 run 的真实前沿已推进到 `subbatch=3 before-build-stage-task`，host watchdog 存在“checkpoint-only progress 误杀”风险

- 新增独立调试记录目录：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/`
- 约束已经落地：
  - 后续每一轮调试必须写入新的时间戳文件
  - 不再复用同一个记录文件
- 当前首份记录：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/debug_records/20260413T044223Z.md`
- 对最新 timeout capture
  `pairdummy-sbus128-runworkload-20260413-040643 / 192.168.1.123 / 20260413T042704Z`
  的重新静态核对，新增两个关键结论：
  1. 这轮不只是到
     `worker stage=0 subbatch=2 done`
     ，而且 checkpoint 已继续推进到：
     `worker stage=0 checkpoint=before-build-stage-task subbatch=3`
  2. 但仍未出现：
     `worker stage=0 checkpoint=after-build-stage-task subbatch=3`
- 因而当前最新主怀疑路径已经进一步收敛为：
  - `build_stage_task_desc`
  - `build_stage_conv_desc`
  - `stage_prepare_exec_views`
  这条 build / fixed-tensor / spm-xlate 路径
- 从当前 `ours2` 编排文件
  `pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
  可直接读出：
  - `segment=0 stage=0`
    的 fixed tensor 为：
    `1000000`
    和
    `1000001`
  - `tensorUseLazyFetch: [0, 0, 0, 0]`
  - 因而每个 subbatch 都会重新装载固定张量：
    - `1000000 -> 1` 页
    - `1000001 -> 64` 页
- 另一个关键系统性问题：
  - 当前
    `/home/ubuntu/chipyard/scripts/firesim-prt-host-watchdog.sh`
    的 progress 判据并不包含
    `guest checkpoint log`
    的 size 增长
  - 所以如果 guest 主要只在 checkpoint 文件里前进，
    watchdog 会把它误判成 idle timeout
  - 这使得
    `04:27 UTC`
    这轮“被冻结并被 watchdog 杀掉”
    还不能直接等价成“runtime 真死锁”
- 下一步已明确：
  1. 把 checkpoint 文件增长纳入 watchdog progress 判据
  2. 在
     `stage_prepare_exec_views / stage-fixed-load`
     附近补低扰动 sparse 观测
  3. 重新走
     `image-closure -> launch -> infrasetup -> run`
     做 fresh 验证

## 2026-04-13 04:11 UTC 已完成“收窄 checkpoint -> fresh image -> fresh host -> fresh run”；当前 live run 仍在 boot / 早期用户态前沿

- 本轮源码收敛改动：
  - `prt_dma.c`
    已撤掉
    `stage0 tensor=2`
    的高频 checkpoint：
    - `dma_should_checkpoint_doneflag_tok()`
      不再覆盖
      `tensor=2`
    - `dma_should_checkpoint_submit_wait_tok()`
      不再覆盖
      `tensor=2`
  - 改为保留低扰动 sparse：
    - `dma-export-host ... phase=page-submitwait-begin`
    - `dma-export-host ... phase=page-submitwait-end`
    - 当前按
      `stage0 tensor=2`
      的页级 milestone
      打点：
      `page=0`
      / 每
      `8`
      页 / 最后一页
- 新 image-closure 已完整通过：
  - `marshal build`
    PASS
  - `marshal install`
    PASS
  - build/install 后的本地 freshness
    均 PASS
  - 新 runtime-binary sha256：
    `3cd0a9ed498bc6f9ffc0efe3de0800507b09eca07013ba876c5a0597e318a180`
  - 新 image sha256：
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
  - host watchdog log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260413-040643.host-watchdog.log`
- 新 host 已确认通过：
  - `infrasetup`
  - `+check-fingerprint`
    输出
    `FireSim fingerprint: 0x46697265`
  - 私网
    `remote-freshness`
    PASS
- 截至 `04:11 UTC`
  的当前 live 前沿：
  - `heartbeat.csv`
    已涨到：
    `4606152876, 261`
  - `uartlog`
    仍主要是 Linux kernel boot 输出
  - guest image 内仍未出现：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
  - 因而当前还不能对新的 runtime blocker 下结论；
    这轮还停留在 boot / 早期用户态前沿，不是 runtime 主体冻结
- 下一步：
  1. 继续监控这轮 live run
  2. 一旦
     `/root/pipeline-runtime-debug`
     目录出现，
     立即核对新的页级 sparse
     是否生效
  3. 重点观察：
     - 是否再次越过旧
       `subbatch=2`
       blocker
     - 收窄 checkpoint 后，
       是否还会重现
       `program-begin -> program-post-fence`
       冻结

## 2026-04-13 03:53 UTC 当前 live run 已明确越过旧 `subbatch=2 compute-done -> c2 export` blocker；新的稳定冻结点前移到 `DMA program-begin -> program-post-fence` 之间

- 当前 live run：
  - instance：
    `i-05425478faec8f72c`
  - private ip：
    `192.168.1.19`
  - session：
    `pairdummy-sbus128-runworkload-20260413-033712`
  - manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-13--03-37-13-runworkload-0KJCK5M5W4NEY2H1.log`
  - host watchdog log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260413-033712.host-watchdog.log`
- 这轮最关键的新结论：
  - 旧的冻结点
    `stage0 subbatch=2 compute-done -> c2 export`
    已经不成立
  - guest 文件日志已经稳定推进到：
    - `worker stage=0 subbatch=6 done`
    - `worker stage=0 subbatch=7 begin`
    - 8 个
      `oc-split-pointwise`
      tile
      全部完成
    - 随后再次进入
      `tensor=2`
      的 export DMA
  - `checkpoint.log`
    也显示：
    `tensor=2`
    的 export DMA 多次成功走到
    `wait-fence-done ... hw_done=1`
- 当前新的稳定冻结点：
  - `heartbeat.csv`
    继续从
    `12466795137, 675`
    涨到
    `14062688059, 756`
  - 但：
    - `bertmini-batch8.log`
      固定停在
      `369073`
      字节
    - `bertmini-batch8.checkpoint.log`
      固定停在
      `2751866`
      字节
  - 精确尾部前沿为：
    - sparse log
      停在
      `stage0 subbatch=7`
      的 export 前准备阶段
    - checkpoint log
      稳定停在：
      `dma stage=0 checkpoint=program-begin tensor=2 src=0x40603000 dst=0x103918c00 bytes=1024 done_pa=0x102277000`
    - 后面始终没有出现：
      `checkpoint=program-post-fence`
- 因而当前最合理的工作假设变成：
  - 旧的 export-wait blocker
    已经被越过
  - 新的冻结点不再是
    `wait-fence-done`
    那类“提交后等待 DMA 完成”的位置
  - 这次卡住发生在
    `program-begin`
    之后、`program-post-fence`
    之前；
    从静态代码看，这一小段里只剩：
    - 极少量 tracing/checkpoint 打点
    - `hw_dma_submit_fence()`（CPU `fence rw, rw`）
  - 所以当前更像是：
    **高频 checkpoint / file-log 调试路径诱发的新人工冻结点**
    ，而不是原先那个 export DMA 功能性 blocker 原地没动
- 下一步执行策略：
  1. 把当前“高频 checkpoint 覆盖整个 `stage0 tensor=2` export”继续收窄
  2. 保留足够识别
     `subbatch=7`
     / `program-begin`
     / `program-post-fence`
     的低扰动 sparse
  3. 结束当前 run，重新走
     `image-closure -> launch -> infrasetup -> run`
     验证冻结点是否继续后移或直接消失

## 2026-04-13 03:29 UTC 当前这轮 live run 已被 host watchdog 自动终止；冻结点稳定收敛为 `stage0 subbatch=2 compute-done -> c2 export`

- 已结束的 run：
  - instance：
    `i-02b3f31f5aecbd73b`
  - private ip：
    `192.168.1.185`
  - session：
    `pairdummy-sbus128-runworkload-20260413-025728`
  - host watchdog log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus128-runworkload-20260413-025728.host-watchdog.log`
  - timeout capture：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-runworkload-20260413-025728-192.168.1.185-host-watchdog-20260413T031746Z.*`
- 这轮终止不是人工猜测，而是 host watchdog 的明确结论：
  - `heartbeat.csv`
    一直涨到：
    `22987944430, 1203`
  - 但
    `guest_sparse`
    自
    `282695`
    字节起连续
    `624s`
    不再变化
  - watchdog 随后触发：
    `host idle timeout reached after 624s`
    并自动执行：
    `terminaterunfarm --forceterminate`
- timeout capture 里的决定性前沿：
  - `status.txt`
    仍是
    `state=running`
    ，说明 guest 当时没有正常退出
  - `guest-sparse-log.txt`
    尾部稳定停在：
    - `worker stage=0 subbatch=2 compute-done`
    - 随后是一串
      `rr-acquire-inner ...`
      / `rr-acquire-wrap phase=after-call`
  - 仍然没有看到：
    - `worker stage=0 subbatch=2 done`
    - 更晚的 export retire / 下一 substage 完成标记
- 因而这轮可以收敛出新的硬结论：
  - 旧 shell / runner foreground `sync`
    blocker
    已经越过
  - 当前 pairdummy/sbus128 主线的真实冻结点，
    重新稳定落回：
    **`compute-done` 之后、`c2 export` 内部**
- 本轮已经落下的静态整改：
  - `prt_dma.c`
    已把高扰动 checkpoint 收回到更窄版本：
    - `doneflag-acquire-*`
      继续保留在
      `stage0`
      固定 tensor，
      并额外覆盖
      `tensor=2`
    - `program/wait`
      类 checkpoint
      不再对所有固定 tensor 打，
      改成只盯
      `stage0 tensor=2`
  - `prt_scheduler.c`
    新增窄门控 sparse：
    - `c2-export ... phase=dispatch`
    - `c2-export ... phase=copy-begin`
    - `c2-export ... phase=copy-end`
    - `c2-export ... phase=retire`
  - `prt_dma.c`
    新增首个 export chunk 的窄门控 sparse：
    - `dma-export-host ... phase=first-chunk-submitwait-begin`
    - `dma-export-host ... phase=first-chunk-submitwait-end`
  - `scripts/firesim-prt-host-watchdog.sh`
    默认观测面已补齐：
    - `wrapper.stage`
    - `wrapper-proc.stage`
    - `runner.stage`
    - `runner-early.stage`
    - `runner-proc.stage`
    - timeout 时额外抓
      `checkpoint.log`
- 当前还没完成的一点：
  - 我尝试先做本地
    `make rerocc_pipeline_runtime-linux`
    编译级检查，
    但当前 host 环境缺少
    `riscv64-unknown-linux-gnu-gcc`
    / `riscv64-linux-gnu-gcc`
    ，所以这一步无法在本地直接完成
  - 下一步验证要回到固定 workflow：
    `image-closure -> launch -> infrasetup -> run`

## 2026-04-13 03:06 UTC 已明确越过旧 blocker；当前 live run 进入 stage0 实际计算

- 同一 live run：
  - instance：
    `i-02b3f31f5aecbd73b`
  - private ip：
    `192.168.1.185`
  - session：
    `pairdummy-sbus128-runworkload-20260413-025728`
- 当前最关键的新结论：
  - 这轮已经**明确越过**旧前沿
    `after-resolve-manager-layout`
  - `wrapper.stage`
    已到：
    `before-child-wait pid=132`
  - `runner-early.stage`
    已到：
    `before-runner-enter`
  - `runner.stage`
    已到：
    - `after-runner-enter batch=8`
    - `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
    - `before-prepare-hugetlb`
    - `after-prepare-hugetlb`
    - `before-bin method=ours2`
    - `after-bin-spawn method=ours2 pid=196`
- 因而可以确认：
  - 上一轮修掉的
    `runner stage foreground sync`
    确实是有效修复，
    这轮没有再卡死在该点
  - 新 host 上的 fixed profile + fresh image + fresh `-bin`
    组合已经把执行前沿推进到了更后面的 runtime 主体
- 当前 runtime binary 进展：
  - `runner-proc.stage`
    显示父 shell
    `wchan=do_wait`
    是因为它在等待子进程
    `rerocc_pipeline_runtime-linux`
    ，这次不是 shell 自己异常卡住
  - 子进程
    `pid=196`
    已被采到：
    `State: R (running)`
  - sparse log 已进入真实执行，不再停在 yaml / hugetlb 初段：
    - `layer_mapping` 大文件已经完成读取并在继续后续执行
    - 已出现
      `stage=0`
      的
      `conv-sync-strided`
      / `pointwise-matmul-fallback`
      / `fence-end rc=0`
      / `worker stage=0 subbatch=2 compute-done`
  - `checkpoint.log`
    也在快速增长，
    已出现：
    - `wait-fence-done ... hw_done=1`
    - `doneflag-acquire-*`
    - `after-build-stage-task subbatch=2 rc=0 op=1 tile_count=8 mgr0=0`
- 当前不是新的静态 blocker：
  - `heartbeat.csv`
    继续涨到：
    `10718708876, 585`
  - `bertmini-batch8.log`
    已涨到约
    `282695`
    字节
  - `bertmini-batch8.checkpoint.log`
    已涨到约
    `367898`
    字节
  - 这说明 guest 正在持续前进
- 下一步：
  1. 继续监控当前 live run，
     优先观察是否直接跑通
  2. 如果后续再停住，
     再基于当时的 segment/stage/checkpoint 前沿做定点修复，
     不回退去重复排查已经通过的
     `runner stage sync`
     / `hugetlb`
     早期路径

## 2026-04-13 03:00 UTC 新 fresh host `192.168.1.185` 已重新起跑；当前确认 guest 仍在推进，但还没到 wrapper 落盘

- 新实例：
  - instance：
    `i-02b3f31f5aecbd73b`
  - private ip：
    `192.168.1.185`
- 已完成固定顺序：
  - `image-closure`
  - `launchrunfarm`
  - `infrasetup`
  - `remote-freshness`
  - 手工
    `./FireSim-f2 +slotid=0 +check-fingerprint`
- 当前本地构件哈希仍是：
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
- 新 run：
  - session：
    `pairdummy-sbus128-runworkload-20260413-025728`
  - manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-13--02-57-29-runworkload-VRH49UVJTXBNLB5Y.log`
  - result dir：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-13--02-57-29-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync`
- 当前最重要的新事实：
  - `remote-freshness`
    已完整 PASS
  - 手工
    `+check-fingerprint`
    已输出
    `FireSim fingerprint: 0x46697265`
  - `heartbeat.csv`
    连续推进，最近已到：
    `2428237454, 140`
  - `uartlog`
    也在继续增长，
    大小已从约
    `6128`
    字节涨到
    `7480`
    字节
  - 这说明当前不是“1 秒假完成”或 host driver 侧再次空转退出
- 截至
  `2026-04-13 03:00 UTC`
  ，guest 侧前沿仍在 Linux boot 早期：
  - `uartlog`
    还没有进入
    `/firemarshal.sh`
    或 runner 文本
  - 对当前 run host 上的 workload image：
    `/home/ubuntu/sim_slot_0/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy0-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
    运行
    `debugfs -R "ls -l /root"`
    可以看到
    `/root/rerocc-linux-tests*`
    等 overlay 已在镜像中
  - 但
    `debugfs -R "ls -l /root/pipeline-runtime-debug"`
    仍返回
    `File not found by ext2_lookup`
- 所以当前结论只能收敛到：
  - 这轮修掉
    `runner stage foreground sync`
    之后，
    新 run 还没有再现“1 秒假完成”或“前台 sync 立即把 shell 拖住”
  - 但也**还没有**推进到 wrapper/runner 文件日志重新出现的程度
  - 目前更像：
    guest 正在较慢地通过 Linux boot，
    还没到用户态 workload 入口
- 下一步：
  1. 继续监控
     `heartbeat.csv`
     / `uartlog`
     / image 内
     `pipeline-runtime-debug`
     是否出现
  2. 一旦出现
     `wrapper.stage`
     / `runner.stage`
     ，立刻确认是否越过旧前沿
     `after-resolve-manager-layout`
  3. 如果长时间 heartbeat 继续涨、但始终不进入
     `/firemarshal.sh`
     ，再回到 boot/init 路径做静态排查，而不是过早下结论为 runtime 主逻辑卡死

## 2026-04-13 02:46 UTC 当前 live run 真正卡在 runner stage sync 的前台 sync，不是 guest 还没进 wrapper

- 新的 fresh host：
  - instance：
    `i-01e0ba8475adefe51`
  - private ip：
    `192.168.1.89`
  已完成：
  - `launchrunfarm`
  - `infrasetup`
  - `remote-freshness`
  - 手工 host 预检：
    - `/home/ubuntu/sim_slot_0/FireSim-f2`
      非零
    - `./FireSim-f2 +slotid=0 +check-fingerprint`
      通过
- 这轮 run：
  - session：
    `pairdummy-sbus128-runworkload-20260413-023601`
  - 不是之前那种“1 秒假完成”；
    `heartbeat.csv`
    持续推进，manager 也一直认为 sim 正在运行。
- 一开始的误判是：
  - `uartlog`
    长时间只停在早期 Linux boot 文本
  - 直接
    `debugfs -R "cat ...status"`
    / `cat ...wrapper.stage`
    / `cat ...runner.stage`
    返回空输出
  - 从而看起来像“guest 甚至还没到 `/firemarshal.sh`”
- 这轮后来用更稳的办法重新检查后，结论被纠正：
  - 先对 image 内目录做
    `debugfs -R "ls -l /root/pipeline-runtime-debug"`
  - 结果看到以下文件其实都已经存在且非零：
    - `bertmini-batch8.status`
    - `bertmini-batch8.wrapper.stage`
    - `bertmini-batch8.wrapper-proc.stage`
    - `bertmini-batch8.runner-early.stage`
    - `bertmini-batch8.runner.stage`
    - `bertmini-batch8.runner-proc.stage`
    - `bertmini-batch8.log`
  - 同时 host watchdog 也在
    `2026-04-13T02:42:48Z`
    通过
    `guest_status_size > 0`
    成功 arm
- 因而这轮的真实前沿不是“还没到 wrapper”，而是：
  - `status`：
    `state=running`
  - `wrapper.stage`
    已到
    `before-child-wait pid=132`
  - `runner-early.stage`
    已到
    `before-runner-enter`
  - `runner.stage`
    已到
    `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
  - 主 sparse log
    只到：
    `[firemarshal] ...`
    配置头 + `[bertmini] runner-enter batch=8 methods=ours2`
  - `checkpoint.log`
    仍为空
- 当前最关键的新诊断是：
  - `runner-proc.stage`
    显示
    `/bin/sh /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh --batch 8`
    的 shell 处于
    `State: S (sleeping)`
    且
    `wchan=do_wait`
  - 在
    `runner.stage`
    已成功写出
    `after-resolve-manager-layout`
    之后，
    下一条本该出现的是
    `runner-config ...`
    ，但没有出现
  - 这说明 shell 不是卡在 stage 行写入之前，
    而是大概率卡在
    `write_runner_stage()`
    写完 stage 文件之后立刻调用的前台
    `sync`
    上
  - 也就是：
    **当前前移卡点是我们新加的 runner-stage 逐点强制刷盘，把 guest 自己拖住了**
- 为排除 stale remote `-bin`
  这一支，这轮还额外核对了：
  - 本地
    `...pairdummy-bin`
    sha256：
    `5ad5bed580cb31b952d3bcdd0c6ba76653c589f88d485b5cc6f14424fe701645`
  - host 上
    `/home/ubuntu/sim_slot_0/...pairdummy-bin`
    sha256
    相同
  - 所以这轮不是
    `remote image 新但 +prog0 -bin 旧`
    的错配
- 当前已做的源码修复：
  - 文件：
    `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - 文件：
    `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - 把
    `runner_stage_sync()`
    从前台阻塞
    `sync`
    改成非阻塞后台 flush，
    避免 shell 停在等待 `sync` 子进程退出
  - 同时固定 profile：
    `pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
    和
    `rerocc-linux-tests-coupleddma/workload/overlay/firemarshal.env`
    里的
    `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE`
    从 `1`
    改为 `0`
    ，改成只依赖 wrapper 的周期性 `sync`
- 当前 live run 已通过
  `pairdummy_sbus128_workflow.sh terminate`
  回收；
  当前正在重做：
  `pairdummy_sbus128_workflow.sh image-closure`
- 下一步固定为：
  1. 等新的 `image-closure` PASS
  2. 按固定流程重新：
     `launch -> infrasetup -> current-private-ip -> remote-freshness -> run`
  3. 新 run 监控时，
     不要再只做
     `debugfs cat <单文件>`
     判定“文件不存在/还没到该阶段”
     ；
     先做
     `debugfs -R "ls -l /root/pipeline-runtime-debug"`
     看 inode size，
     再决定读哪个文件
  4. 如果新 run 能越过
     `after-resolve-manager-layout`
     并出现
     `before-prepare-hugetlb`
     或后续 sparse/checkpoint，
     就可以确认这次前移 blocker 的根因确实是
     `runner stage sync`
     的前台刷盘
  5. 那之后再继续回到
     `hugetlb / alloc-contig / runtime`
     主线

## 2026-04-13 01:48 UTC 复用 host 控制面失效，不应继续当成 runtime 新卡点

- 这一轮为了继续使用旧实例
  `i-0f338e9910fbae43d`
  （private ip:
  `192.168.1.163`）
  ，先完成了以下固定动作：
  - `image-closure`: PASS
  - 修复
    `pairdummy_sbus128_workflow.sh`
    中
    `current-private-ip / remote-freshness / run`
    仍写死
    `fsimcluster=firesim`
    的 bug；
    现在改为从 runtime yaml 解析
    `run_farm_tag`
  - 重新用私网地址做 remote freshness
- 这一轮又确认了一条重要事实：
  - 对“已经跑过一次 workload 的旧 host”，
    直接比较整张 remote `.img`
    与本地 pristine `.img`
    的 sha，
    会因为 guest 已写入日志而失败
  - 这不是 stale runtime，
    但也不能忽略；
    正确补救是先把本地 pristine
    `.img`
    和 `-bin`
    重新覆盖到
    `/home/ubuntu/sim_slot_0/`
    ，并清理
    `uartlog/heartbeat/memory_stats`
    等外部残留文件，
    然后再重做 remote freshness
- 本轮在
  `192.168.1.163`
  上完成“镜像回洁”后，
  remote freshness 已重新严格 PASS：
  - runtime sha256:
    `da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
  - local/remote image sha256:
    `86515fc1dadb8278db468c495126c2e85f6c406c0bcadd8ba08e8857471c9311`
- 但随后新的 run
  `pairdummy-sbus128-runworkload-20260413-013846`
  没有真正进入 guest：
  - `uartlog`
    只剩
    `script`
    的开始/结束两行
  - `heartbeat.csv`
    没有生成
  - FireSim manager 在约 1 秒内把 job 判成 completed
  - 结果目录里也没有
    `pipeline-runtime-debug`
    文件
- 这轮最关键的新发现不是 runtime 语义，而是 host 侧 driver bundle 异常：
  - `/home/ubuntu/sim_slot_0/FireSim-f2`
    和多份 `.so`
    在 run 前实际是
    `0` 字节
  - 这样
    `sudo ./FireSim-f2`
    会像“空脚本”一样立刻退出 `0`
    ，从而造成：
    - manager preflight 假通过
    - runworkload 假完成
  - 远端
    `driver-bundle.tar.gz`
    本身内容是正常的；
    `tar -tvzf`
    能看到正确文件大小
  - 手工在
    `/home/ubuntu/sim_slot_0`
    里重新
    `tar -xvzf driver-bundle.tar.gz`
    之后，
    `FireSim-f2`
    与各 `.so`
    已恢复为正常非零大小
- 手工修复 driver bundle 之后，
  真正的 preflight 才暴露出更底层的问题：
  - `timeout ... sudo ./FireSim-f2 +slotid=0 +check-fingerprint`
    稳定报：
    `AFI in Slot is not in READY state !`
  - `sudo fpga-load-local-image -S 0 -I agfi-0dc8dcfa4c7735f40 -A`
    返回 `0`
    ，但没有任何 stdout/stderr，
    也没有把 slot 拉到 READY
  - `sudo fpga-describe-local-image -S 0 -R -H`
    在这台 Ubuntu F2 host 上仍是
    `rc=0`
    但空输出
  - reboot 这台实例之后，
    上述现象仍然没有改善
- 因而当前应该把这台旧实例归类为：
  **复用 host 的 FPGA 管理面失效 / 不可信**
  ，而不是 pipeline-runtime 新卡点。
- 当前明确结论：
  1. `alloc-contig/hugetlb`
     的 checkpoint 调试版 runtime 仍然是当前本地主线版本
  2. 这轮没有跑到新的 guest runtime 前沿
  3. 当前 blocker 不在 guest runtime，
     而在这台复用 F2 host 的 FireSim/FPGA 控制面
- 当前已执行的收尾动作：
  - 坏实例
    `i-0f338e9910fbae43d`
    已通过
    `pairdummy_sbus128_workflow.sh terminate`
    进入
    `shutting-down`
  - 新的 fresh launch
    已重新启动：
    `pairdummy-sbus128-launchrunfarm-20260413-015011`
  - 截至本次更新时，
    AWS 仍在返回
    `insufficient capacity`
    ，还没有拿到新的
    `f2.6xlarge`
- 下一步固定为：
  1. 不再继续复用
     已回收中的
     `i-0f338e9910fbae43d`
  2. 先通过
     `terminaterunfarm --forceterminate`
     或显式
     `aws ec2 terminate-instances`
     彻底回收它
  3. 等当前新的
     `launchrunfarm`
     真正拿到 fresh host
  4. 回到 fresh workflow：
     `launch -> infrasetup -> remote-freshness -> run`
  5. 下一台 fresh host 上，
     在真正 `runworkload`
     之前，必须额外检查：
     - `/home/ubuntu/sim_slot_0/FireSim-f2`
       不是 `0` 字节
     - 手工
       `timeout ... ./FireSim-f2 +slotid=0 +check-fingerprint`
       能通过
     然后才继续盯 guest `checkpoint/log`

## 2026-04-13 01:09 UTC checkpoint 旁路诊断版 image-closure 已完成

- 已停掉旧 live run：
  - session：
    `pairdummy-sbus128-runworkload-20260413-005007`
  - instance：
    `i-04bac52cc7df377ac`
  - private ip：
    `192.168.1.106`
  - 已通过
    `terminaterunfarm --forceterminate`
    进入 `shutting-down`。
- 对这轮 live run 的重新核对，已经确认两个重要事实：
  - 不能用 run host 的 `/dev/root`
    去做 `debugfs`；
    正确目标必须是
    `/home/ubuntu/sim_slot_0/*.img`
    里的 guest image。
  - 也不能把
    `/home/ubuntu/sim_slot_0/*-bin`
    当成 guest image 内 runtime freshness 的最终依据；
    真正要核对的是 image 内
    `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`。
- 基于正确 image 抽取后，这轮旧 live run 的 decisive 结论是：
  - guest image 内 runtime binary 的 sha256
    确实是
    `f6516aa02acadfe3736d83d1524340e0c88916054db233f32e7523204ed613d4`
  - 并且 image 内 binary 已包含：
    - `spm-pt alloc-contig hugetlb-check`
    - `spm-pt hugetlbfs open begin`
    - `spm-pt hugetlbfs ftruncate begin`
    - `spm-pt hugetlbfs mmap begin`
    - `spm-pt probe progress`
  - 所以那一轮**不是 stale image / stale runtime**。
- 这轮旧 live run 的真实前沿重新收敛为：
  - `segment=0 action-alloc-spm begin`
  - `spm-xlate-ctx alloc begin`
  - `spm-pt pool chunk-add begin next_idx=0 chunk_bytes=2097152`
  - `spm-pt alloc-contig begin req_bytes=2097152 probe_page=4096 require_hugetlb=1`
  - 然后 sparse log 不再出现后续 `policy / hugetlb-check / hugetlbfs open`
    等新字符串。
- 同时又观察到一个新的现场特征：
  - `bertmini-batch8.log`
    的 inode `Size=229376`
    ，正好是 `56 * 4096`
  - 文件末尾有大量 `NUL`
    尾部
  - sparse log `mtime`
    停在 guest 时间
    `00:00:06`
  - `wrapper-proc.stage`
    只更新到 guest 时间
    `00:00:02`
  - 但 `heartbeat.csv`
    仍继续推进到约
    `13794329012, 739`
- 当前更准确的判断是：
  - 旧 live run 的新 binary
    的确跑到了 `alloc-contig begin`
  - 但主 sparse log 很可能在这一前沿附近发生了 guest 文件追加/刷盘阻塞，
    导致“hugetlb 真前沿”和“主日志最后一行”不再等价
  - 因而下一轮不能只盯
    `bertmini-batch8.log`
    ；必须同时给独立小文件
    `checkpoint`
    旁路加断点。
- 已完成的新代码修改：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
  - 新增 checkpoint 旁路断点：
    - `hugetlb-can-cover enter/exit`
    - `hugetlb meminfo enter/exit`
    - `alloc-contig after-cover`
    - `alloc-contig before/after hugetlbfs`
    - `hugetlbfs open/ftruncate/mmap begin/end`
  - 这些修改的目的不是改变分配语义，
    而是把 `alloc-contig` 前沿从主 sparse log
    旁路到独立 checkpoint 文件，
    用来区分：
    - 真正卡在 hugetlb 路径
    - 还是主 sparse log 自己卡住
- 当前新的本地闭环：
  - 本地重编 runtime binary sha256：
    `da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
  - 新 `image-closure`：
    PASS
  - `image-freshness`
    已明确显示：
    `runtime-binary sha256=da3e5425a111bb4b0d2d147af3e0b09fab6cc2867f96381da3774d8c780941d6`
  - 当前本地 image sha256：
    `5697b65055f461028c70bc80708211309813035e4af902acd4b9b2e8cc312bda`
- 下一步固定为：
  1. `launch`
  2. `infrasetup`
  3. `remote-freshness`
  4. `run`
  5. 新 run 首先同时观察：
     - `bertmini-batch8.log`
     - `bertmini-batch8.checkpoint.log`
     - `bertmini-batch8.wrapper-proc.stage`
  6. 如果 sparse log 仍停在 `alloc-contig begin`
     ，但 checkpoint log 继续推进，
     则可以把 blocker 进一步归类为：
     “主 sparse log 文件写/刷盘路径卡住”
     ，而不是 hugetlb 逻辑本身无进展。

## 2026-04-12 17:28 UTC hugetlbfs 新前沿与修复

- 旧 live run：
  - session：
    `pairdummy-sbus128-runworkload-20260412-171219`
  - instance：
    `i-00eefd089273cd848`
  - private ip：
    `192.168.1.217`
  - 当前已通过
    `terminaterunfarm --forceterminate`
    停掉。
- 这一轮最重要的新确认：
  - `synthetic-model alloc before-prefault`
    之后并没有真正死卡。
  - 新镜像内已经明确看到：
    - `prefault-progress` 从 `1 MiB` 推到 `16 MiB`
    - `synthetic-model alloc after-prefault`
    - `synthetic-model alloc before-mlock`
    - `synthetic-model alloc after-mlock rc=0 errno=0`
  - 所以当前 pairdummy/sbus128 主线下，
    synthetic blob 的 `prefault + mlock`
    已不再是最新 blocker。
- 当前新的真实前沿已经前移到：
  - `segment=0 action-alloc-spm begin`
  - `spm-xlate-ctx alloc begin`
  - `spm-pt alloc-contig begin`
  - `spm-pt alloc-contig hugetlb-check need_pages=1 total_pages=1 free_pages=1 can_try=1`
  - `spm-pt hugetlbfs begin req_bytes=2097152 alloc_bytes=2097152 probe_page=4096 path_dir=/dev/hugepages`
- 这轮静态复盘后的结论：
  - 现有 `prt_page_table.c`
    在 hugetlb page-table 分配路径里有两层“日志盲区”：
    - `mmap(... MAP_POPULATE ...)`
      把 hugepage 建立/预填充藏进 kernel 内部
    - `probe_phys_contig_range()`
      开头整段
      `memset(base, 0, bytes)`
      又把整块触页藏进用户态黑箱
  - 这两层都不是语义必须项，因为后面的逐页触碰/物理地址探测本来就会建立映射并验证物理连续性。
- 已完成的代码修复：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
  - 修改：
    - 去掉 hugetlb anon / hugetlbfs 路径里的 `MAP_POPULATE`
    - 去掉 `probe_phys_contig_range()` 开头的整段 `memset`
    - 补充 `hugetlbfs open/ftruncate/mmap` begin/end 日志
    - 补充 `spm-pt probe progress`
      的中间进度日志
- 当前这版新 binary / image 的闭环 sha：
  - runtime sha256：
    `f6516aa02acadfe3736d83d1524340e0c88916054db233f32e7523204ed613d4`
  - image sha256：
    `6a620252ef42b8a45673ded3a9279366fbc8dc8d096d4b93a9ada9065663d1d5`
- 这不是“为了绕过当前 case 随便跳过一步”。
  当前修复的理由是：
  - physical contiguity 仍由 hugetlb 映射 + 后续 pagemap probe 验证保证
  - 页建立动作仍会发生，只是从“黑箱式隐式预填充”改成“显式逐页触碰，可观测、可定位”
- 下一步固定为：
  1. `terminaterunfarm --forceterminate`
     停掉当前 live run
  2. 用固定 workflow 重做
     `image-closure -> launch -> infrasetup -> remote-freshness -> run`
  3. 新 run 重点观察以下新点位：
     - `spm-pt hugetlbfs open begin/end`
     - `spm-pt hugetlbfs ftruncate begin/end`
     - `spm-pt hugetlbfs mmap begin`
     - `spm-pt hugetlbfs mmap ok`
     - `spm-pt probe begin/progress/end`

## 2026-04-12 17:42 UTC 新一轮 fresh run 已重新拉起

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
- 当前闭环 sha：
  - runtime sha256：
    `f6516aa02acadfe3736d83d1524340e0c88916054db233f32e7523204ed613d4`
  - image sha256：
    `6a620252ef42b8a45673ded3a9279366fbc8dc8d096d4b93a9ada9065663d1d5`
- 新 run 当前状态：
  - remote freshness 已确认 run host 上的 image 与本地一致
  - host watchdog 已启动，但尚未 arm 到 guest `status`
  - guest 仍在 Linux boot 阶段，heartbeat 已推进到约
    `4446645349, 253`
  - 当前还未再次进入
    `pipeline-runtime-debug`
    文件阶段，因此还不能判断这轮新的 hugetlbfs 点位是否已经越过

## 2026-04-12 workflow 固化更新

- 当前 `12-pair sbus128` dummy-model 主线已新增固定 profile 和固定 workflow：
  - profile:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
  - workflow:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
  - runbook:
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/docs/pairdummy_sbus128_reusable_workflow_20260412.md`
- 这个固化方案的目标不是修复 runtime 语义本身，而是防止再次犯以下流程错误：
  - `/firemarshal.sh` 路径判断错层
  - pairdummy 配置靠 shell 临时 env 覆写，导致 build / image / run 语义漂移
  - generic fileonly 入口默认变成 dummy synthetic
  - 跳过 local / remote freshness，结果用了旧 image

## 2026-04-12 16:41 UTC 增量状态

- 截至当前核对时，AWS 上没有运行中的 `f2.6xlarge` FireSim 实例。
- 当前只剩一个旧 tmux 会话：
  `pairbert-b8-d12s128-term39`
  ；
  这只是之前 `terminaterunfarm --forceterminate` 的残留 shell，不代表 run farm 仍存活。
- 因此从这一刻开始，现场应按“无 live run、可干净重起”处理。
- 下一步固定为：
  1. 先用
     `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh image-closure`
     重建当前 image 闭环
  2. 再按同一脚本顺序执行
     `launch -> infrasetup -> current-private-ip -> remote-freshness -> run`
  3. 监控时以 guest 文件日志为主，不把 `uartlog` 当主观测面

## 2026-04-12 16:47 UTC 新 fixed-workflow 闭环已重新启动

- 新一轮固定 workflow 已按顺序执行到 `runworkload`：
  - `image-closure`: PASS
  - `launchrunfarm`: PASS
  - `infrasetup`: PASS
  - `remote-freshness`: PASS
  - `runworkload`: 已启动
- 当前本地关键 sha：
  - image sha256：
    `e7aa13adfffbb4e6a34b66a5497a21f37fc73297e8778aa74c2b7d70c2e0fc30`
  - runtime sha256：
    `575414015130d8a0f7af37ad35a31905cec601ea59038065f8f874eb944ab848`
  - `/firemarshal.sh` 与 fileonly wrapper sha256：
    `0d3aa9398bfbf0fa6bec0a269be9669261ba1e9b5c48e3be6528a31c94e6366e`
  - runner script sha256：
    `2e2717d1d4502754c23c1d11f9781b1a1df0ff58abc649b89904f13f4e5b12e3`
  - `firemarshal.env` sha256：
    `26272ae0e24844d9ee4f86047a198bf8a6f8f4141a9af98ddb0f86278ff7033c`
- 新 FireSim live：
  - instance：
    `i-09ce54845adc28eac`
  - private ip：
    `192.168.1.195`
  - AGFI：
    `agfi-0dc8dcfa4c7735f40`
  - `runworkload` session：
    `pairdummy-sbus128-runworkload-20260412-164743`
  - 结果目录：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-12--16-47-44-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
- 当前 live 早期观测：
  - host watchdog 已挂上，监控实例 `192.168.1.195`
  - `heartbeat.csv` 已开始增长
  - guest `pipeline-runtime-debug` 文件还没出现，说明仍在 boot / 用户态早期
  - 此时不要把“guest 文件尚未出现”误判成新的 runtime 卡点

## 2026-04-12 17:01 UTC 当前 live 断点进一步收敛

- 当前 run 现已确认越过以下旧边界：
  - `running /etc/init.d/S99run`
  - `before process memory lock`
  - `after process memory lock`
  - `dma-completion-pool before/after-prefault`
  - `dma-completion-pool before/after-mlock`
  - `artifacts mapping parse end`
  - `init validate-artifacts end`
- 当前 live 最新日志前沿：
  - `init synthesize-model-bin begin reason=skip-model-bin-load`
  - `synthetic-model alloc before-mmap`
  - `synthetic-model alloc after-mmap`
  - `synthetic-model alloc before-prefault ... mode=write-preserve`
- 当前现场特征：
  - heartbeat 继续增长，约已到
    `15026934005, 801`
  - 但 guest sparse log 自
    `synthetic-model alloc before-prefault`
    之后不再增长
  - 因而目前更像是
    `prefault_and_lock_blob()`
    内部耗时过长或停住，
    而不是 Linux boot / wrapper / artifact parse 问题
- 下一步动作：
  - 给
    `prefault_and_lock_blob()`
    的逐页触碰循环补 1MiB 粒度的 progress log
  - 然后重新走固定 workflow：
    `image-closure -> launch -> infrasetup -> remote-freshness -> run`
  - 目标是判断：
    当前是“页触碰极慢但仍在前进”
    还是“真的卡在某一页 fault / 锁页路径”

## 2026-04-12 17:04 UTC 新 prefault-progress 版本已完成 image-closure

- 已修改：
  - `pipeline-runtime/src/prt_runtime.c`
    在
    `prefault_and_lock_blob()`
    里加入每 `1 MiB`
    一条的
    `prefault-progress`
    日志，只增加观测，不改变 prefault/mlock 语义。
- 新 fixed-workflow `image-closure` 已 PASS。
- 新 runtime binary sha256：
  `7bf68548f50636875cd536535edc7430f7f8cba324b58fa6d7d446a4ce53d4a9`
- 当前新的 main blocker 不是 build/install/freshness，而是 AWS `f2.6xlarge` 容量：
  - `launchrunfarm`
    当前在自动重试
  - pane log 已明确出现：
    `Tried all subnets, but there was insufficient capacity to launch your instances`
  - 因而当前不能把“尚未进入新一轮 run”
    误记成 workflow 失败；
    代码和 image 已就绪，只差容量恢复

## 2026-04-12 当前结论更新（入口 freshness / run40）

### 最新补充：这轮又固化了两条新的硬约束，并把 tooling 也补上了，避免以后再犯同类错误

- 新的硬约束 1：
  对当前 pairdummy file-only workload，
  guest 真正执行入口是 image 内
  `/firemarshal.sh`，
  不是
  `/root/firemarshal.sh`
  。以后判断 workload 实际执行路径、做 image freshness、分析 guest 入口时，都必须以
  `/firemarshal.sh`
  为准。
- 新的硬约束 2：
  对当前 pairdummy file-only workload，
  freshness 不能只校验 runner / runtime / env；
  必须同时校验这 5 项：
  - `/firemarshal.sh`
  - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
  - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
  - `/firemarshal.env`
- 新的硬约束 3：
  以后停止 FPGA run 默认就带：
  `terminaterunfarm --forceterminate`
  不要再裸跑会卡在
  `Type yes`
  的交互式版本。
- 新的 tooling 兜底：
  - `scripts/firemarshal-tmux-run.sh`
    现在会把 workload json 参数自动归一化成绝对路径，
    不再把
    `software/firemarshal`
    当前目录错误当成 json 解析基准。
  - `verify_pairdummy_firemarshal_image_freshness.sh`
    已经补成 5 项校验。
  - `host-init.sh`
    已保证把
    `run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
    真正打进 guest image。
- 本轮 fresh-image 闭环新状态：
  - 旧 `run39`
    实例
    `i-0d15f38a246cf50bb`
    已用
    `terminaterunfarm --forceterminate`
    停掉
  - `clean10b`
    `PASS`
  - `build48`
    产出新 image 后，已通过修正后的 local freshness
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
    - 已完成 clear/load AGFI
    - AGFI =
      `agfi-0dc8dcfa4c7735f40`
    - driver readiness preflight 已通过
- 当前下一步：
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
  5. 因而目前 fresh-image 主线已经明确**越过**
     旧 run39 当时看到的
     `before-mlock`
     位置；
     下一步继续看它是否会再次停在
     `synthetic-model`
     路径，还是推进到更后的
     `spm-pt`
     / segment 执行主线

## 2026-04-12 当前结论更新（run39 / strict-hugetlb 版本确认）

### 最新补充：当前 `run39` 跑的已经是“strict hugetlb”修复后的新版本，不是旧 image；而且这轮并没有回退卡死在 `hugetlb` 之前，当前只是 guest boot 刚推进到 `S99run`

- 当前 `run39` 对应实例：
  - instance id:
    `i-0d15f38a246cf50bb`
  - private ip:
    `192.168.1.193`
- 本轮 fresh-image 闭环已确认：
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
- 已确认本轮 image / runtime / runner 都是新的，不是 stale image：
  - runner-script sha256 =
    `fdabde31d87372dfd82810ee79e0f9e8751e3af19d7b71400662ef1d491b25fa`
  - runtime-binary sha256 =
    `849e8e60f766acc00076b6b7e6e824a5fae3e32ce43d4f67dfa4192d7c9bf62d`
  - firemarshal-env sha256 =
    `2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
- 当前源码中，`spm-pt` 物理连续页表分配逻辑已经不是“多页普通匿名页 fallback”：
  - `pipeline-runtime/src/prt_page_table.c`
    中：
    - `runtime_pt_chunk_bytes()`
      在
      `spm_xlate_enable=1`
      时，把页表 chunk 固定为单个 hugepage
    - `alloc_contig_pt_storage()`
      中：
      - 只要请求超过 1 个基础页，就视为
        `need_multi_page_phys_contig=1`
      - 此时强制
        `require_hugetlb=1`
      - 优先尝试
        `hugetlbfs`
      - **拒绝多页普通匿名页 fallback**
      - 只有单页请求，才还可能走匿名页路径
- bertmini runner 也已显式传入：
  - `--spm-pt-require-hugetlb 1`
- 截至当前这一轮最新现场：
  - `heartbeat.csv`
    已推进到
    `6788150238, 377`
  - `uartlog`
    已推进到：
    `running /etc/init.d/S99run`
  - 但 image 内这些 guest 文件还没创建出来：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.checkpoint.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner-proc.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper-proc.stage`
- 因而当前能确定两点：
  - 这轮 `run39`
    **不是** 又回退卡死在
    `hugetlb`
    之前；它已经推进到
    `S99run`
  - 但它**还没有**真正进入我们要看的
    `pipeline-runtime`
    guest 文件日志前沿，所以还不能下结论说已经越过新的
    `spm-pt hugetlbfs`
    分配点

## 2026-04-12 当前结论更新（本轮补充）

### 最新补充：对 `pairmanager_dummy16x16 / g12 d12 / sbus128 / mac256` 这条主线来说，`hugetlb` 前沿是一次真实回退，不只是观测更细；根因是 `spm-pt` 默认依赖匿名 hugepage，这条路径本身不稳定

- 已重新核对“同一套 artifact”的旧证据与当前 fresh run，确认两件事同时成立：
  - 更早那批
    `c2_g2_d2 / mac1024`
    证据确实不是同一套 artifact
  - 但当前用户指出的这条
    `pairmanager_dummy16x16 / g12 d12 / sbus128 / mac256`
    主线，之前也确实已经推进到
    `segment=1 / stage=0`
    之后，并持续排查过更后的
    `export DMA / rr-acquire / doneflag`
    问题
- 直接证据来自：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-nochkpt-noaudit-exportsubmithang-20260411/bertmini-batch8.log`
  - 其中已经明确出现：
    - `init ready segments=13`
    - `segment=0 action-alloc-spm begin action=1`
    - `spm-pt hugetlb anon ok req_bytes=2097152 ...`
    - `spm-pt pool chunk-added idx=0 ...`
    - `segment=0 action-alloc-spm end action=1`
    - `worker stage=0 subbatch=0 begin ...`
  - 这说明：
    **同一套 `segments=13` 的 pairmanager/mac256/sbus128 artifact 之前能稳定越过 `alloc-spm`，并继续推进到后面的 DMA 主线**
- 当前 `run37`
  的真实前沿则已经收敛到：
  - `segment=0 action-alloc-spm begin`
  - `alloc-spm xlate-ctx begin`
  - `spm-pt alloc-contig begin req_bytes=2097152 ...`
  - `spm-pt hugetlb anon begin ...`
  - 然后不再出现
    `spm-pt hugetlb anon mmap ok / spm-pt pool chunk-added`
- 因而，对这条主线来说，这不是单纯“新 probe 把黑盒拆细了”，而是一次**真实回退**：
  - 之前后面的
    `segment=1/stage=0/DMA`
    问题并没有被证明消失
  - 只是当前 fresh binary 在更前面的
    `spm-pt`
    分配处先被挡住了
- 当前更合理的根因判断是：
  - `spm-pt` 默认先赌一次
    `2 MiB MAP_HUGETLB`
  - 但 guest boot 里并没有预留稳定的 hugepage 池
  - 所以这条路本来就不是可依赖语义，而只是“有时碰巧成功”的脆弱实现
  - 一旦 binary layout / probe / 前置分配稍有变化，就可能从之前的
    `hugetlb anon ok`
    退化为当前的
    `hugetlb anon begin` 之后冻结
- 基于这个结论，当前已在
  `pipeline-runtime/src/prt_page_table.c`
  落地一轮修复：
  - 只有在确认 hugepage 池可覆盖当前请求时，才尝试 hugetlb 路径
  - 否则不再默认把
    `spm-pt`
    chunk 无条件拉成
    `2 MiB`
  - 这轮修复的目标不是改变后面
    `segment=1/stage=0/DMA`
    的语义，而是先把前沿重新推回那条更后的真实主线
- 当前已终止旧
  `run37`
  实例
  `i-06bd49efe6f4a7950`
  ，并重新启动 fresh-image 闭环：
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
      仍处于 Linux boot 早期
    - image 内：
      - `bertmini-batch8.status`
      - `bertmini-batch8.log`
      - `bertmini-batch8.checkpoint.log`
      - `bertmini-batch8.runner.stage`
      - `bertmini-batch8.runner-proc.stage`
      - `bertmini-batch8.wrapper.stage`
      - `bertmini-batch8.wrapper-proc.stage`
      仍未出现
    - 因而当前还不能判断这版是否已经重新越过
      `spm-pt hugetlb`
      前沿；下一步仍应继续用私网
      `192.168.1.122`
      监控 guest 文件系统里的这些文件

### 最新补充：`marshal clean -> build -> install -> launchrunfarm -> infrasetup -> remote freshness -> runworkload` 已用新 runtime 修复重新完整闭环；当前 `run35` 正在 fresh image 上继续推进，主观测面仍是 guest 文件系统

- 已完成本轮 stale-image 闭环：
  - `marshal clean`
  - `marshal build`
  - `marshal install`
  - 两步都通过自动 freshness check
- 当前本地 fresh 关键 hash：
  - runtime binary sha256 =
    `6edccb9ee27538dae78a39bb9aa5d3538d14b99cf278e2ccd3b2a639dbe13e35`
  - local image sha256 =
    `56dd00c8c7038df02e485019949eaf5fa620e290077591dd6504dbe488f809bc`
- 本轮新 run farm / host：
  - 实例：
    `i-0f29265083202b0cb`
  - 私网：
    `192.168.1.91`
  - AGFI：
    `agfi-0dc8dcfa4c7735f40`
- 当前远端 freshness 已明确 `PASS`：
  - remote image =
    `/home/ubuntu/sim_slot_0/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy0-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
  - remote sha256 =
    `56dd00c8c7038df02e485019949eaf5fa620e290077591dd6504dbe488f809bc`
  - image 内 env 继续确认：
    - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='1'`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- 这轮继续保留“日志写 guest 文件系统，不把 `uartlog` 当主观测面”：
  - workload outputs 仍然回收：
    `/root/pipeline-runtime-debug/*.log`
  - wrapper
    `run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
    仍然是：
    `exec >> /root/pipeline-runtime-debug/bertmini-batch8.log 2>&1`
    并带周期性 `sync`
- `run35` 当前状态：
  - 已完成：
    - `launch35`
    - `infrasetup35`
    - remote image freshness
    - `run35`
      启动
  - `run35`
    当前仍在运行，且已确认：
    - FireSim sim slot 已启动
    - host watchdog 继续只盯私网
      `192.168.1.91`
    - `uartlog`
      已到 Linux boot / mount rootfs
      `Loaded platform drivers, booting from disk:`
    - `heartbeat.csv`
      已推进到
      `5070443126, 284`
  - 截至这次记录时，image 内 guest 文件：
    - `bertmini-batch8.status`
    - `bertmini-batch8.log`
    - `bertmini-batch8.deep.log`
    - `bertmini-batch8.wrapper.stage`
    - `bertmini-batch8.runner.stage`
    仍为空，说明：
    - **当前 guest 还没进入 runtime wrapper / `S99run` 之后的 pipeline-runtime 主体**
    - 因而这轮还不能判断是否已经越过新的
      `spm-xlate flush / rr-acquire opcode=3`
      前沿
- 如果下一次接手时 `run35` 还在继续：
  1. 继续只用私网
     `192.168.1.91`
     检查：
     - `/home/ubuntu/sim_slot_0/heartbeat.csv`
     - `/home/ubuntu/sim_slot_0/uartlog`
     - 以及 image 内
       `/root/pipeline-runtime-debug/*`
  2. 一旦 image 内
     `bertmini-batch8.status/log`
     非空，就优先以 guest 文件内容判断 runtime 前沿
  3. 新一轮真正需要验证的点仍然是：
     - 是否越过旧的
       `rr-acquire wait stage=4294967295 manager=4 opcode=3 cfg=15`
     - 以及新的
       `exec-bind-flush mgr_count/mgr0..mgr3`
       是否只覆盖当前 stage 实际使用的 manager

### 最新补充：`spm-xlate flush` 运行期并发语义已做第一轮根因修复，当前正在重新走 `marshal clean -> build -> install -> firesim` 闭环

- 已落地代码修复：
  - `pipeline-runtime/src/prt_runtime.c`
    新增按 `stage` 收集实际 Gemmini manager 集的 helper：
    `runtime_collect_stage_gemmini_mgrs()`
  - 运行期
    `stage_prepare_exec_views()`
    的 `exec-bind-flush`
    不再调用“flush 当前 action 全部 managers”的
    `runtime_flush_spm_xlate(rt)`，
    而改为只 flush 当前 `stage` 实际使用的 manager 集：
    `runtime_flush_stage_spm_xlate(rt, stage_id)`
  - `exec-bind-flush`
    marker 现在会额外记录：
    `mgr_count/mgr0..mgr3`
    方便确认它是否还错误碰到其他 stage 的 manager
- 这次修复的直接动机不变：
  - `run33`
    已证明旧的 `doneflag` blocker 真正越过
  - 新 blocker
    转移到更晚位置：
    `rr-acquire wait stage=4294967295 manager=4 opcode=3 cfg=15`
  - 结合源码静态排查，高概率根因仍然是：
    运行期 `exec-bind-flush`
    对整组 managers 做 `spm-xlate flush`
    时，抢到了别的 stage 正在持有的 `opcode=3` scope
- 当前新的 runtime binary sha256：
  - `6edccb9ee27538dae78a39bb9aa5d3538d14b99cf278e2ccd3b2a639dbe13e35`
- 为避免再次把“旧 image / 旧 source layer”误判成新硬件卡点，这轮在 FireMarshal 侧还补了一个配套修复：
  - `rerocc-linux-tests-coupleddma/workload/host-init.sh`
    之前的 binary fingerprint 检查硬编码要求
    `conv-sync-strided acquire-snapshot`
  - 但当前源码在 marker-only RR acquire 路径下，
    clean build 产物不再稳定包含这个旧字面串
  - 现已把检查放宽为接受：
    - `conv-sync-strided acquire-snapshot`
    - 或
      `conv-sync acquire-end`
  - 这属于 **build-side stale fingerprint 修正**，
    不是 guest 运行语义改动
- 当前闭环状态：
  - 旧 `run33`
    已终止，避免旧 run farm 和新 image 混用
  - 已执行：
    `marshal clean`
  - 当前正在重新执行：
    `marshal build`
    以生成包含上述 runtime 修复的新 image

### 最新补充：`run33` 已重新回到 `run31` 的真实 `tensor=1000001` doneflag 前沿，并确认这次不会再卡在 `doneflag`；`acquire-only` 最小 instrumentation 没有继续扰动 live 行为

- 当前这轮 live 只用私网 SSH：
  - 实例：
    `i-015aca2af0fddd113`
  - 私网：
    `192.168.1.54`
- 这轮已经走完：
  - `launch33`
  - `infrasetup33`
  - remote image freshness check
    `PASS`
- 当前 fresh 关键 hash：
  - runtime binary sha256 =
    `615c5c1dd7ab8082b13173a3484086812d9b6cdfa718028b57a433fde280123b`
  - local/remote image sha256 =
    `5fbf27d635d905f1a20541608900b06535b2123bea1654e15e43140e4f59ac57`
- 这轮还再次确认了 file-only 主观测面没有被回退：
  - workload json 产物仍然是：
    `/root/pipeline-runtime-debug/*.log`
  - `host-init-fileonly-sync-pairdummy.sh`
    里：
    `PIPELINE_RUNTIME_UART_LOG_ENABLE=0`
  - image 内远端 freshness 也确认：
    `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
  - 实际 wrapper
    `run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
    会把 stdout/stderr 直接
    `exec >> /root/pipeline-runtime-debug/bertmini-batch8.log 2>&1`
    并周期性 `sync`
- `run33`
  已明确重新走回旧基线：
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
  的旧轨迹基本重合，说明：
  - 这版只保留
    `doneflag-acquire-*`
    的最小 instrumentation
    **没有**再把 frontier 拉回
    `rr-acquire-inner before-csr-write`
    那个更早的假前沿
- 当前这轮已经拿到的决定性 live 证据：
  - sparse log 已重新出现：
    - `artifacts mapping parse done`
    - `artifacts mapping parse end`
    - `rr-acquire-inner phase=before-csr-write`
    - `rr-acquire-inner phase=before-return`
    - `rr-acquire-wrap phase=after-call`
  - checkpoint 文件现在已经大量非零，且明确出现旧真实前沿：
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
  - 而且这不是一次性的：
    - token
      已连续推进到
      `85 -> 96`
    - 每个 token
      都能完整走完
      `doneflag -> program -> wait-fence-done`
- 因而这轮可以明确下结论：
  - **旧 blocker “卡在 `doneflag-begin` 到 `doneflag-end` 之间” 已不再成立**
  - **`acquire-only` 最小 checkpoint 方案是足够轻的，至少不会再扭曲到更早假前沿**
- 当前这轮仍在继续跑，新的调试重点已经后移为：
  1. 继续盯
     `run33`
     后续是否会在更晚位置停住
  2. 优先看是否重新收敛到历史上的更后期 frontier：
     - `worker stage=0 subbatch=4 compute-done`
     - `c2 export`
     - 或新的 `rr/program/wait`
       子阶段
  3. 如果再冻结，再以 guest 文件和 checkpoint 为主，不回退到 UART 主观测面

### 最新补充：`run27` 已确认越过 `S99run` 并进入 runtime 主体；新 wrapper marker 已出现，当前冻结点收敛为 `rr-acquire-wrap after-call` 之后，但这轮因 checkpoint 默认关闭，尚不能继续分辨 caller-return 还是 doneflag/program 子阶段

- 当前这轮 live 只用私网 SSH：
  - 实例：
    `i-04fa58168cd6a59c2`
  - 私网：
    `192.168.1.191`
- 这轮已经走完：
  - `launch27`
  - `infrasetup27`
  - remote image freshness check
    `PASS`
- 当前 fresh 关键 hash：
  - runtime binary sha256 =
    `b9e66a5e6c654250ffda1d30004c0f9c3a910edcbec1e2845910d095773ac6da`
  - local/remote image sha256 =
    `8ed962f2aad93671ab7cdf778d997b6942b75b8a7d24a348a290d6b32ffb083a`
- `run27` 已明确越过：
  - Linux boot
  - `running /etc/init.d/S99run`
  - wrapper 启动和 runner 启动
  - `segment=0`
    的 init / alloc / topology / spm-xlate
  - `rr-acquire-inner phase=before-return`
- 这轮新获得的关键 live 证据：
  - image 内
    `bertmini-batch8.runner.stage`
    已到：
    - `after-bin-spawn method=ours2 pid=207`
  - image 内
    `bertmini-batch8.log`
    已明确出现：
    - `rr-acquire-inner phase=before-return stage=0 manager=0 opcode=2 cfg=0`
    - `rr-acquire-wrap phase=after-call stage=0 manager=0 opcode=2 rc=0 cfg=0 valid=1`
  - 同时：
    - `heartbeat.csv`
      从
      `584`
      增长到
      `673`
    - 但
      `bertmini-batch8.log`
      大小稳定停在
      `309824`
      字节
- 因而这轮已经可以确认：
  - `noinline + wrapper-after-call`
    的新打点已经真实进了 fresh image，并且 guest 实际跑到了这条 log
  - 当前 freeze 位置已经至少推进到：
    **`prt_rr_acquire_scope()` wrapper 内部 log 之后**
- 但这轮也必须准确说明一个限制：
  - 当前 image 里的
    `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE`
    仍然是
    `0`
  - 所以虽然看到了
    `rr-acquire-wrap after-call`
    ，但还**不能**仅凭这轮日志断言：
    - caller 已经执行到 `rr-acquire-end checkpoint`
    - 或已经走到 `doneflag`
    - 或已经走到 `program/wait`
  - 更准确的说法是：
    **freeze 目前收敛在 `wrapper-after-call` 之后，到下一个 caller 侧 checkpoint 之前**
- 这轮 runner / wrapper 文件还补充说明：
  - `bertmini-batch8.status`
    为
    `state=running`
  - `bertmini-batch8.wrapper.stage`
    已到
    `before-child-wait`
  - `bertmini-batch8.runner-proc.stage`
    显示：
    - runner shell 正在等待子进程
    - 子进程
      `pid=207`
      即
      `rerocc_pipeline_runtime-linux`
      仍在 `R (running)`
- 基于这轮证据，下一轮动作已经收敛为：
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
  6. 优先看 checkpoint 文件里最后停在：
     - `rr-acquire-end`
     - `doneflag-end`
     - `program-post-src`
     - `wait-enter`
     - `wait-fence-done`
     的哪一步
  7. 然后再决定是改 caller 路径，还是改 doneflag/program/wait 路径

### 最新补充：`run22` 已稳定冻结在 `compute-done -> c2 export` 之间，下一轮优先打开 checkpoint 文件定位 `submit/program/wait` 子阶段

- 当前 live 继续只用私网 SSH：
  - 实例：
    `i-0f7202156b104bcc2`
  - 私网：
    `192.168.1.226`
- `run22` 已明确越过：
  - boot / runner / YAML parse
  - `run21` 的旧 frontier
    `tok=1642`
  - `worker stage=0 subbatch=4 compute-done`
- 但 `run22` 现在也已明确不是“只是慢”：
  - `heartbeat.csv`
    从
    `817`
    持续增长到
    `914`
  - 而 image 内
    `/root/pipeline-runtime-debug/bertmini-batch8.log`
    大小稳定保持
    `375116`
    字节不变
  - `status`
    仍为
    `state=running`
- 当前冻结点的新收敛结论：
  - `segment=0`
    拓扑日志已显示：
    - `pipebufs=2`
    - `ringbufs=0`
    - `worker stage=0 ready entries=1 exports=1 isolate_pairs=0 shared_pairs=0`
  - 因而
    `compute-done`
    之后不会走
    `c3/c4/c6/c8`
    路径
  - 当前 blocker 应直接收敛为：
    **`stage=0` 唯一 `c2` 导出，也就是 `SPM -> DRAM/host` 的同步导出 DMA**
- 更细的静态/动态对照结论：
  - 这轮 DMA backend 已确认是
    `blocking_fence`
  - `compute-done`
    之前已经有两次同类
    `opcode=2`
    RR acquire 成功出现，说明：
    **通用的 SPM->host alias 导出 helper 不是“完全坏掉”**
  - `compute-done`
    之后最后稳定日志只剩一组新的
    `rr-acquire-inner ... after-set-opc`
    然后再无后续
  - 因而当前最可疑区间不是 RR acquire 本身，而是：
    **`c2` 导出中的 `dma submit/program/wait(fence)` 子阶段**
- 这轮还确认了一个新的语义线索：
  - `segment=0 / stage=0`
    的唯一 export tensor 是
    `tensor=2`
    且类型为
    `DRAM`
  - `sync_stage_export_aliases()`
    在
    `compute-done`
    前已经把同一个 tensor 导出到模型 alias
  - `compute-done`
    后的 `c2`
    还会再把它写回
    `buf->dram_base_addr[0]`
  - 所以当前路径里存在“alias 导出后再次做 c2 主导出”的重复语义，需要后续继续核查其必要性与正确性
- 下一轮调试动作已经收敛为：
  1. 终止当前 `run22`
  2. 保持文件日志为主，不回退到 UART 主观测面
  3. 打开
     `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=1`
     重新 build/install/infrasetup/run
  4. 用已有 stage0 checkpoint 判断最后停在：
     - `rr-acquire-end`
     - `doneflag-end`
     - `program-post-src`
     - `wait-enter`
     - `wait-fence-done`
     中的哪一步
  5. 然后再决定是改实现，还是只补语义去重

### 最新补充：硬约束进一步明确为“任何编译前都必须先 source FireSim manager 环境”，不只是 FireMarshal 包装脚本阶段

- 当前调试硬约束要明确成下面这句，后续不要再写成含糊表述：
  - **任何编译动作之前**，都必须先：
    - `cd /home/ubuntu/chipyard/sims/firesim`
    - `source sourceme-manager.sh --skip-ssh-setup`
  - 然后才允许回到 repo 根目录继续：
    - `source env.sh`
    - `marshal build`
    - 以及其他会触发 workload / binary / image 重新生成的动作
- 这条约束虽然已经通过
  `scripts/firemarshal-tmux-run.sh`
  做了自动兜底，但后续人工执行或新脚本也必须遵守，不能只依赖 wrapper “碰巧替我补上”

### 最新补充：`run21` 已越过 runner early / YAML parse，但冻结在 `tok=1642`，当前优先按“DMA 热日志写文件路径过重”修正

- 这轮 live 继续只用私网 SSH：
  - 实例：
    `i-0214e12e733f2a7b0`
  - 私网：
    `192.168.1.70`
- `run21` 已确认推进到 guest runtime 主体：
  - `bertmini-batch8.runner.stage`
    已到
    `after-bin-spawn`
  - `bertmini-batch8.log`
    已出现：
    - `artifacts mapping parse done`
    - `artifacts mapping parse end`
  - 说明这轮已经**越过**
    wrapper / runner very-early 和 layer-mapping YAML 解析阶段
- 当前新的 live frontier：
  - token 从 `379` 增长到 `1642`
  - 之后多次 20s 级采样保持：
    - `LATEST_TOKEN=1642`
    - `HAS_2596=0`
  - 因而这轮**不是**旧的
    `tok=2596 / rr-acquire-inner phase=before-csr-write`
    blocker
- `tok=1642` 周围最后稳定可见日志：
  - `after-rr-postcheck`
  - `after-rr-state-install`
  - 最后一行停在
    `after-doneflag-clear tok=1642 value`
    的半截
  - 同时 guest 主日志文件大小稳定在
    `3044196`
    字节，不再增长；但 `heartbeat.csv` 继续增长
- 静态排查后的新判断：
  - 当前 pairdummy workload 默认仍把
    `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=1`
    打开
  - `prt_dma.c`
    中 `dma-submit-inner` / `dma-wait-inner`
    这组 chunk 级 sparse probe
    之前只由
    `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE`
    +
    `stage_idx == 0`
    +
    `bytes <= 4096`
    控制，
    **没有**再经过 deep-log window 门控
  - 在 file-only 模式下，这些高频 `PRT_PROGRESS_LOG`
    会直接往 guest 文件日志追加，造成 run21 在大量小 DMA 期间极可能被日志写路径本身拖死
- 针对这个新 blocker，已做的新修复：
  - `pipeline-runtime/src/prt_dma.c`
    现在把 `dma-submit-inner` / `dma-wait-inner`
    这组 sparse probe 收回到：
    - `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=1`
    - `prt_log_gate_allow_deep_logs()`
    - 小传输窗口
    共同满足时才打开
  - `rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
    现在把默认
    `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE`
    从 `1`
    改回 `0`
  - 目的不是“静音掩盖问题”，而是把 chunk 级热日志恢复到原本应有的**细粒度定点调试**语义，避免 file-only 默认运行就带着 stage0 全量 DMA 探针
- 这轮新增结论也再次确认：
  - 后续所有编译动作前，仍必须先：
    - `cd /home/ubuntu/chipyard/sims/firesim`
    - `source sourceme-manager.sh --skip-ssh-setup`
  - 所有 live SSH / freshness check 仍然只允许使用私网地址


### 最新补充：shared-scope DMA 根修已进 fresh image，FireMarshal/FireSim freshness 已重新闭环，`run21` 正在新的私网实例上验证

- 本轮已完成的代码修复：
  - `pipeline-runtime/src/prt_dma.c`
    中 Linux host bounce 两个 helper
    `dma_copy_host_to_spm_pages_linux()`
    /
    `dma_copy_spm_pages_to_host_linux()`
    已从“逐 chunk `dma_submit_wait_annotated()`”
    改成“整批 `dma_batch_scope_acquire()` + 每 chunk `dma_submit_wait_annotated_scoped()` + 批末 `dma_batch_scope_release()`”
  - 这次修的是上一轮遗留的**中间态**：
    shared-scope 主体此前已落下，但这两个 Linux helper 还残留“每个 host-page chunk 都重新 acquire/release RR scope”的结构性问题
- 额外固化的硬约束：
  - `scripts/firemarshal-tmux-run.sh`
    现在已改成：
    **先 `cd sims/firesim`，再 `source sourceme-manager.sh --skip-ssh-setup`，然后才 `source env.sh` 并执行 FireMarshal**
  - 所以后续这条 pairdummy workload 的 `build/install`
    不再只依赖人工记忆“编译前先 source FireSim manager 环境”
- 本轮 fresh `marshal clean -> build -> install` 已完成：
  - clean session:
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/pairdummy-prt-clean27.pane.log`
  - build session:
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/pairdummy-prt-build27.pane.log`
  - install session:
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/pairdummy-prt-install27.pane.log`
  - build/install 后本地 freshness 均通过
- 本轮 fresh 产物关键 hash：
  - image 内 runtime binary sha256 =
    `37af07b801ea8000ca65d54302d6b14a905c5aaf585b0d94cddf2b4a955a2712`
  - 本地/远端实际运行 image sha256 =
    `37acb93c154e137cd10f08955b53da121577f9ff612d385c11c8810829a3db4f`
  - image / remote image 中确认的关键 guest env：
    - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- 本轮 FireSim 正规链路已重新执行：
  - launchrunfarm:
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch21.pane.log`
  - infrasetup:
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-infrasetup21.pane.log`
  - runworkload:
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run21.pane.log`
- 当前 live：
  - 实例：
    `i-0214e12e733f2a7b0`
  - **只允许用私网 SSH**：
    `192.168.1.70`
  - remote image freshness check：
    PASS
    - 脚本：
      `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`
- 当前 live 状态必须严格表述：
  - `run21` 已确认不是 stale image / stale remote image
  - `run21` 当前仍处于 Linux 启动到 workload 入口之间的阶段：
    - `heartbeat.csv` 持续增长
    - `uartlog` 持续向后推进
    - 但在本次更新时，
      guest 还**没有**进入 `pipeline-runtime` 应用主体，
      因而还**不能**宣称已越过或未越过旧的
      `tok=2596 / rr-acquire-inner phase=before-csr-write`
      blocker
  - 下一步应继续只用私网地址
    `192.168.1.70`
    监控：
    1. `uartlog` 是否越过 `running /etc/init.d/S99run`
    2. image 内 `/root/pipeline-runtime-debug/bertmini-batch8.log` 是否开始落盘
    3. 一旦应用日志出现，优先对照旧 blocker
       `tok=2596 / before-csr-write`
       判断 shared-scope 根修是否真正把前沿继续推后

## 2026-04-11 当前结论更新（本轮补充）

### 最新补充：`run17` 已切到 `segment=0` 细日志 image，当前已重新越过 runner / mapping 主线，并确认较早 subbatch 上的 `mgr=5/6/7` pointwise fast path 可过

- 当前 live：
  - `runworkload`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run17.pane.log`
  - 当前实例：
    `i-099d87c4249348d0f`
  - **只允许用私网 SSH**：
    `192.168.1.78`
- 这轮在重新启动前已完成完整 freshness 闭环：
  - local image sha256 =
    `cb22b2dfa779acc58267b9e5125095b73c3834328f4c70981dfa006fd4eeb2ef`
  - remote image sha256 =
    `cb22b2dfa779acc58267b9e5125095b73c3834328f4c70981dfa006fd4eeb2ef`
  - image 内 runtime binary sha256 =
    `7372ed59627cd9e5e725c2805fa3d91473997e6c154189eefbacf241e953af2e`
  - image 内 `firemarshal.env` 已确认：
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='1'`
    - `DEEP_LOG_ENABLE='1'`
    - `DEEP_LOG_SEGMENT='0'`
    - `PIPELINE_RUNTIME_LOG_PROFILE='fine_segment'`
- 这轮再次确认旧 runner 卡点已经不是问题：
  - `runner-early.stage` 已出现：
    - `skip-guest-env inherited path=/firemarshal.env`
    - `after-arg-parse batch=8`
    - `after-bin-check ...`
    - `before-runner-enter`
  - `runner.stage` 已出现：
    - `after-runner-enter`
    - `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
    - `before-prepare-hugetlb`
    - `after-prepare-hugetlb`
    - `before-bin method=ours2`
    - `after-bin-spawn method=ours2 pid=191`
- runtime 主线当前也已重新越过：
  - `yaml pipeline parse/validate`
  - `artifacts mapping load begin`
  - 更后的 `stage=0 tensor=2` export DMA 路径
  - 稀疏日志里 `rr-acquire-inner ... after-csr-read ... acquired=1`
    持续正常出现
- 这一轮最重要的新证据：
  - 在当前 `segment=0` 细日志窗口内，
    已明确看到较早 subbatch 上的：
    - `mgr=5`
      `acquire-end -> dispatch-select -> pointwise-return -> fence-end`
    - `mgr=6`
      `acquire-end -> dispatch-select -> pointwise-return -> fence-end`
    - `mgr=7`
      `acquire-end -> dispatch-select -> pointwise-return -> fence-end`
  - deep log 里也已看到这次新增的细 marker：
    - `conv-sync-strided acquire-snapshot`
    - `dispatch-select`
    - `pointwise-precall-snapshot`
    - pointwise inner call enter/return
- 但这里必须保持表述严格：
  - **当前只能确认 `mgr=5/6/7` 这段 pointwise fast path 在较早 subbatch 上不再一碰就挂。**
  - **还不能宣称旧的 `stage=0 / subbatch=6 / tile=5/8 / mgr=5` 卡点已经越过**，
    因为当前 sparse 前沿只明确看到：
    - `segment=0 sink-progress=4/8`
    - 也就是整体还没推进到 `subbatch=6`
- 因而当前 live 状态应表述为：
  **`run17` 已重新越过 runner 与 mapping 主线，并确认 `mgr=5/6/7` 的 pointwise fast path 在较早 subbatch 上可过；当前还在继续推进 toward `subbatch=6`，旧具体 blocker 是否已完全被越过仍待 live 继续确认。**

### 最新补充：`run16` 的新稳定卡点已经收敛到 `mgr=5 acquire-end` 之后，当前先按 `acquire-end -> dispatch-select` 细分继续打点

- 对 `run16` 再做延时复查后，结论已经稳定：
  - `heartbeat.csv` 继续增长到：
    `1502`
  - 但 guest 文件日志大小不再变化：
    - `bertmini-batch8.log` =
      `2239973`
    - `bertmini-batch8.deep.log` =
      `20847`
  - 最后稳定 sparse tail 停在：
    - `oc-split-pointwise stage=0 tile=5/8 mgr=5 ... begin`
    - `conv-sync-strided stage=0 mgr=5 begin ...`
  - 最后稳定 deep tail 停在：
    - `conv-sync stage=0 mgr=5 acquire-begin`
    - `css-aq-b`
    - `css-aq-e`
    - `conv-sync acquire-end stage=0 mgr=5 scope_valid=1 cfg=1 opcode=3`
- 结合源码路径：
  - `conv_call_for_manager_sync_strided()`
    在打出这条
    `conv-sync acquire-end`
    之后，
    下一步立刻调用：
    `prt_log_rr_binding_snapshot_or_marker("conv-sync-strided acquire-snapshot", ...)`
  - 但需要特别修正一个误判：
    pairdummy workload 的
    `host-init-fileonly-sync-pairdummy.sh`
    默认就是
    `PIPELINE_RUNTIME_ONLY_MARKER=1`
  - `2026-04-11--17-07-52` 的 fresh build log 也明确显示：
    `-DPRT_ENABLE_ONLY_MARKER=1`
  - 所以当前 `run16`
    在这条 workload 下，
    `prt_log_rr_binding_snapshot_or_marker()`
    实际已经是 marker-only，
    **不会去读 RR debug CSR**
- 因而必须撤回“卡在 RR debug CSR 读取”的假设。
- 当前更准确的表述应改成：
  **freeze 位于 `stage=0 / subbatch=6 / tile=5 / mgr=5` 的 `acquire-end` 之后、`dispatch-select` 之前，但还不能把责任归到 RR CSR 读取；当前只知道它落在这段极短热路径里。**
- 为了把这段再切开，当前已在
  `src/prt_gemmini_adapter.c`
  新增更细 raw marker：
  - `css-as-b/e`
    包住 `acquire-snapshot`
  - `css-tt-b/e`
    包住 `tiled_type` / dilation / scale 预处理
  - `css-pd-b/e`
    包住 pointwise predicate 判定
- 下一轮 fresh run 的直接目标：
  用这些 marker 判断 freeze 究竟落在：
  - marker snapshot 本身
  - `tiled_type` / 标量预处理
  - pointwise predicate 判定
  - 还是 `dispatch-select` marker 之后

### 最新补充：`run16` 已确认越过旧的 guest-env runner 卡点，当前 fresh live 已重新进入 runtime 主线

- 本轮 fresh run：
  - 实例：
    `i-050c4cfeaa2e0faeb`
  - **只允许用私网 SSH**：
    `192.168.1.57`
  - `runworkload`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run16.pane.log`
- 本轮在起跑前再次闭环确认 freshness：
  - 本地 image freshness：PASS
  - remote image freshness：PASS
  - local/remote image sha256：
    `af2d6560d961d914f2e865f743bb39fe5cb8cef0d386c0ba8df7ace137510050`
  - image 内 runtime binary sha256：
    `93891d7b27b03d17c1d73d79e4d01045f7f63893e205e6c1b1c61eabb40b561c`
- 这轮已经拿到新的决定性证据：
  - `uartlog` 已推进到：
    `running /etc/init.d/S99run`
  - image 内 wrapper/runner 文件日志已落下，且不再停在旧的
    `before-guest-env path=/firemarshal.env`
  - `runner-early.stage`
    已明确出现：
    - `skip-guest-env inherited path=/firemarshal.env`
    - `after-arg-parse batch=8`
    - `after-bin-check ...`
    - `before-runner-enter`
  - `runner.stage`
    已明确出现：
    - `after-runner-enter`
    - `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
    - `before-prepare-hugetlb`
    - `after-prepare-hugetlb`
    - `before-bin method=ours2`
    - `after-bin-spawn method=ours2 pid=193`
- 因而这轮已经可以确认：
  **`PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE=1` 修复是生效的；旧的 runner `/firemarshal.env` 重复 source 卡点已经被越过，当前 fresh live 不再被 shell 启动链阻塞。**
- 当前 live 新前沿已经重新回到 runtime binary 本体：
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
    - 且对这批 token，
      `rr-acquire-inner ... after-csr-read ... acquired=1`
      仍持续出现
- 所以截至这次更新时：
  - 当前 live 已经不是 shell / wrapper / runner 层问题
  - 也还没有重新卡回“mapping file read begin”这种更早前沿
  - 下一步应继续监控它是否：
    1. 继续自然推进到先前的 `tok=625` / `tok=2145` / `subbatch6 mgr=1 fence-begin` 一类旧前沿
    2. 或者在这轮 fresh binary 上出现新的、更靠前但稳定的 runtime freeze

### 最新补充：`run16` 已明确越过旧 `run13` 的 `stage0/subbatch6 mgr=1 fence-begin` blocker

- 对同一轮 `run16` 的继续 live 观察，已经拿到比上面更靠后的证据：
  - `heartbeat.csv`
    已继续增长到：
    `900s`
  - `bertmini-batch8.log`
    当前已明确出现：
    - `worker stage=0 subbatch=3 done`
    - `worker stage=0 subbatch=4 done`
    - `worker stage=0 subbatch=5 done`
    - `worker stage=0 subbatch=6 begin op=1 acc=0 dma=0 tiles=8`
  - 对 `subbatch=6`，
    当前已明确看到：
    - `mgr=0 fence-end rc=0`
    - `mgr=1 fence-end rc=0`
    - `mgr=2 fence-end rc=0`
    - `mgr=3 fence-end rc=0`
    - `mgr=4 fence-end rc=0`
    - `mgr=5 begin`
      已出现，仍在继续推进
- 更重要的是，
  `bertmini-batch8.deep.log`
  已把 `subbatch=6 / mgr=1`
  的 fence 路径拆开确认：
  - `rr-fence-scope ... begin/end`
    已出现
  - `scope-drain begin/post-gemmini-flush/post-rr-fence/end`
    已出现
  - 最终
    `conv-sync stage=0 mgr=1 fence-end rc=0`
    已出现
- 因而之前写在顶部的旧结论：
  **`run13` 卡在 `stage0/subbatch6 mgr=1 fence-begin`**
  已经被 `run16` 新鲜 live 明确越过，
  不能再把它当作当前最前沿 blocker。
- 截至这一刻，更准确的当前状态应改写为：
  **`run16` 已连续越过旧的 runner 卡点、旧的 `tok=625` RR acquire 卡点，以及旧的 `subbatch6 mgr=1 fence-begin` 卡点；当前 live 仍在 `stage=0 / subbatch=6` 的更后 tile/manager 上继续推进。**

### 最新补充：`run13` 已越过旧 `tok=2145` 半行 freeze，但又在 `stage0/subbatch6 -> conv-sync-strided mgr=1 fence-begin` 附近冻结

- 这轮 fresh run 基于：
  - 实例：
    `i-0f83806fd6efc8707`
  - **只允许用私网 SSH**：
    `192.168.1.189`
  - `runworkload`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run13.pane.log`
- 这轮 fresh image / binary 已确认是新的：
  - image sha256：
    `b14f12aa96fac534b7d74fe64c3ba3851de25815db7424341e9a0cc0dff5e413`
  - runtime binary sha256：
    `23ce7ae9d6b595c61508b3716c64a2999ad897bb719fb58ca47b1379fc0f599f`
- 这轮先明确排除了旧 blocker 还原：
  - `bertmini-batch8.log` 已推进到 `tok=1990`
  - 说明前一轮的
    `tok=2145` 附近“半行 `rr-acquir`”
    所代表的**日志写路径 freeze**
    在这版 binary 上不再是最先出现的前沿卡点
- 当前 image 内最新有效前沿是：
  - `worker stage=0 subbatch=0..5 done`
    已全部出现
  - `worker stage=0 subbatch=6 begin op=1 acc=0 dma=0 tiles=8`
    已出现
  - `mgr=0`
    的 pointwise conv 已完整走到
    `fence-end rc=0`
  - `mgr=1`
    的同一路径当前最后稳定停在：
    `conv-sync-strided stage=0 mgr=1 fence-begin`
  - 后续没有
    `fence-end rc=0`
- 因而当前最准确的新 blocker 应表述为：
  **live 运行已经越过旧的 DMA/logging freeze，当前冻结点位于 `stage=0 / subbatch=6 / tile=1 / mgr=1` 的 pointwise fallback conv 同步收尾区间；最后稳定日志在 `conv-sync-strided ... fence-begin`。**
- 这里还需要补一条很重要的静态结论：
  - 当前这套
    `gemmini.h`
    里
    `gemmini_fence()`
    只是普通 CPU `fence`
  - 所以这条前沿名义上写成
    `fence-begin`
    ，**真正更可疑的阻塞点不是 `gemmini_fence()` 本身，而是它前面的 `prt_rr_fence_scope()` / `rr_fence(cfg)`，或者更早已经有 Gemmini 请求永远不收敛，导致 `rr_fence` 不返回。**
- 这个新 blocker 具有“数据/状态相关”特征，而不是“mgr=1 永远不通”：
  - 在同一轮 `run13` 里，
    `subbatch=0..5`
    的 `mgr=1`
    都已经走到
    `fence-end rc=0`
  - 只有当前最新的
    `subbatch=6`
    停在
    `mgr=1 fence-begin`
  - 所以更像是：
    - 当前 subbatch 的输入/输出/bias/alias 状态触发了新的硬件路径问题
    - 而不是简单的 RR 配置常量错误
- 当前这轮证据已固化到本地：
  - 目录：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/20260411-stage0-subbatch6/`
  - 重点文件：
    - `run13.status`
    - `run13.heartbeat.tail`
    - `run13.stage0.subbatch.summary`
    - `run13.stage0.subbatch6.tail`
    - `run13.tok.tail`
    - `run13.manager.tail`
    - `run13.tmux.tail`
- 当前这轮实例已按正规流程回收：
  - `firesim terminaterunfarm --forceterminate`
    已执行
  - 实例
    `i-0f83806fd6efc8707`
    已进入
    `shutting-down`
- 下一步不应该盲修语义：
  - 先做一轮**定点深日志** fresh 复现，
    目标锁定：
    - `segment=0`
    - `subbatch=6`
  - 建议覆盖：
    - `PIPELINE_RUNTIME_LOG_PROFILE=fine_segment`
    - `DEEP_LOG_SEGMENT=0`
    - `DEEP_LOG_SUBBATCH=6`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=1`
    - `PIPELINE_RUNTIME_PROGRESS_RAW=1`
    - `PIPELINE_RUNTIME_GEMMINI_PHASE=1`
  - 先精确区分：
    1. 卡在 `prt_rr_fence_scope()` / `rr_fence(cfg)`
    2. 还是 pointwise fallback 内某条 Gemmini 指令已把 manager 弄到不可回收状态
- 本轮还额外确认并修了一个**脚本层漏传**：
  - `scripts/firemarshal-tmux-run.sh`
    之前没有转发
    `PIPELINE_RUNTIME_LOG_PROFILE`
  - 所以即使 host 侧显式设置
    `PIPELINE_RUNTIME_LOG_PROFILE=fine_segment`
    ，image 内生成的
    `firemarshal.env`
    仍会回落成
    `manual`
  - 这会让 guest wrapper 不自动打开
    `DEEP_LOG_ENABLE`
    ，从而让“明明指定了 `DEEP_LOG_SEGMENT=0` / `DEEP_LOG_SUBBATCH=6`，但 runtime CLI 没真正带 `--deep-log-enable 1`”这种假象出现
  - 该漏项现已修复；后续 fresh image 必须确认：
    - `PIPELINE_RUNTIME_LOG_PROFILE='fine_segment'`
    - `DEEP_LOG_ENABLE='1'`
    都真实出现在 image 内
    `/firemarshal.env`
    中

### 最新补充：`run12` 已明确越过旧 `rr_acquire` 卡点，但又在更细位置冻结

- 这轮 live 继续只用私网
  `192.168.1.250`
  观察后，已经拿到比 `run11` 更靠后的证据：
  - `bertmini-batch8.log`
    已推进到
    `stage=0 tensor=2`
    的一系列 export DMA token
  - 对于
    `tok=2134..2144`，
    已连续看到：
    - `after-rr-marker`
    - `rr-acquire-inner phase=before-csr-write`
    - `rr-acquire-inner phase=after-csr-write`
    - `rr-acquire-inner phase=after-csr-read ... acquired=1`
    - `rr-acquire-inner phase=before-set-opc`
    - `rr-acquire-inner phase=after-set-opc`
- 这说明旧的稳定 blocker：
  **`after-rr-marker` 之后卡在 `prt_rr_acquire_scope()` / `rr_acquire_cfg()` 内部**
  已经被这轮 live 明确越过，
  至少不再卡在先前 `run11` 的那个位置。
- 但当前又出现新的、更细 freeze：
  - `heartbeat.csv` 继续增长：
    最新已到 `908`
  - `bertmini-batch8.status` 仍是：
    `state=running`
  - 但 `bertmini-batch8.log`
    大小稳定停在：
    `2326744` bytes
  - 连续两次观测窗口内都不再增长
  - 最后完整日志稳定停在：
    - `tok=2145`
    - `rr-acquire-inner phase=after-csr-write ...`
  - 随后只剩一条**半截**：
    - `[prt-progress] rr-acquir`
- 因而当前最准确的新前沿应表述为：
  **run12 已越过旧 `after-rr-marker -> rr_acquire` 卡点，但在 `tok=2145` 附近再次冻结；最后完整证据位于 `after-csr-write` 之后，下一条本应是 `after-csr-read` / 更后的 acquire marker，只留下了半行前缀。**
- 这个现象暂时有两种仍待区分的解释：
  1. 真正卡在
     `rr_read_csr()`
     前后
  2. `rr_read_csr()` 已返回，但在打印下一条
     `PRT_PROGRESS_LOG`
     时卡住，导致 image 内只剩半行
- 下一步监控/排查应围绕这两者继续收敛。

### 最新补充：`run12` 已进入 `S99run` 并开始落文件日志，但当前还没重新走到旧 `rr_acquire` 卡点

- 这轮 fresh run 基于：
  - 新实例：
    `i-087cedd854b7d9e37`
  - **只允许用私网 SSH**：
    `192.168.1.250`
  - `infrasetup`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-infra13.pane.log`
  - `runworkload`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run12.pane.log`
- 这轮远端 freshness 已再次闭环通过：
  - local image sha256 =
    `58c5a6b6915699c88439898534b41b14da45c6d0102573010ab30bb421f4b46b`
  - remote image sha256 =
    `58c5a6b6915699c88439898534b41b14da45c6d0102573010ab30bb421f4b46b`
  - runtime binary sha256 =
    `927282ee43ff82b74252dbbe86c07eb8e47edacb729879469f62171a21c49c36`
- 当前 live 进展：
  - `uartlog` 已推进到：
    `running /etc/init.d/S99run`
  - image 内已出现：
    `/root/pipeline-runtime-debug/bertmini-batch8.log`
  - 当前 `bertmini-batch8.log` 大小约
    `198069` bytes
  - 但截至本次更新，
    文件前沿还停在 artifact 读取阶段：
    - `artifacts file pread chunk-begin ... off=2097152 chunk=1048576`
  - 还**没有**重新出现：
    - `artifacts mapping load end`
    - `artifacts mapping parse end`
    - 更后面的
      `after-rr-marker`
      /
      `rr-acquire-inner`
- 因而这轮当前状态应表述为：
  **新 marker 已进 binary，run12 已进入 guest 用户态并开始落文件日志，但目前还未重新推进到旧 `stage0/subbatch1 export DMA -> rr_acquire` 卡点；当前 live 前沿暂时位于 `gemmini_layer_mapping` 的 `pread chunk` 读取阶段。**

### 最新补充：带 `rr-acquire-inner` 新 marker 的 fresh 流程已经重新启动

- 旧 `run11` 对应实例
  `i-0da294d40c7a51a66`
  已通过正规流程：
  `firesim terminaterunfarm --forceterminate`
  进入 `shutting-down`
- 新一轮 FireMarshal 已重新完成：
  - build：
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-build6.pane.log`
  - install：
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-install3.pane.log`
  - 两轮 freshness 都通过
  - 当前新的 runtime binary sha256 =
    `927282ee43ff82b74252dbbe86c07eb8e47edacb729879469f62171a21c49c36`
- `launchrunfarm` 过程里已确认一个**调用层错误而不是 runtime blocker**：
  - `pairbert-b8-d12s128-launch8`
    失败原因是漏传
    `-c config_runtime_...yaml`
  - 没带 `-c` 时，
    FireSim 会回退到默认 runtime config，
    进而去查
    `firesim_rocket_singlecore_no_nic_l2_lbp`
    这类不在当前 sbus128 hwdb 中的默认条目
  - 该问题已经修正，不要再把它当作新的硬件/软件卡点
- 修正后新的
  `pairbert-b8-d12s128-launch8b`
  已启动，
  当前日志显示是 AWS `f2.6xlarge` 容量不足，
  正在多 subnet 重试：
  - `Tried all subnets, but there was insufficient capacity to launch your instances`
- 因而截至这次更新时：
  - fresh image 已准备好
  - 新 marker 已进入 host binary
  - 下一轮真正的 runtime 复现尚未开始
  - 当前等待的是 run farm 资源，而不是 runtime 自身语义问题

### 最新补充：`run11` 的 live 前沿已经稳定收敛到 `after-rr-marker -> rr_acquire` 区间

- 这轮最新 fresh 复现基于：
  - FireMarshal build：
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-build5.pane.log`
  - FireMarshal install：
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-install2.pane.log`
  - run farm 实例：
    `i-0da294d40c7a51a66`
  - **只允许使用私网 SSH**：
    `192.168.1.52`
  - `infrasetup`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-infra12.pane.log`
  - `runworkload`：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run11.pane.log`
- 这轮本地 freshness 与远端 freshness 都已闭环通过：
  - local sha256 =
    `3284ed38428cf31d7b6216208abf3bb35eac19f0ac1940022d2b7c7e1c323b2e`
  - remote sha256 =
    `3284ed38428cf31d7b6216208abf3bb35eac19f0ac1940022d2b7c7e1c323b2e`
  - image 内 `/firemarshal.env` 已确认：
    - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- 这轮 live 已明确排除“mapping load 更早卡死”：
  - 已出现：
    - `artifacts mapping load end ...`
    - `artifacts mapping parse end ...`
    - `init validate-artifacts end ...`
- 当前 image 内文件日志稳定冻结在：
  - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    大小约 `563043` bytes
  - 最后一条稳定为：
    `[prt-progress] dma-submit-inner stage=0 tensor=2 phase=after-rr-marker tok=625 mgr=0`
- 同时 host 侧：
  - `heartbeat.csv` 继续增长到 `723s`
  - 说明 guest/仿真没死，但日志前沿已冻结
- 结合 `prt_runtime.c` / `prt_dma.c` / `prt_rerocc.c` 静态对照，当前最准确 blocker 是：
  **freeze 位于 `stage=0 / subbatch=1` 的 export processing 区间，并卡在 `dma_blocking_submit()` 内部、`after-rr-marker` 之后、`rr-acquire-end` 之前，也就是 `prt_rr_acquire_scope()` / `rr_acquire_cfg()` 内部。**
- 更细一点说：
  - `worker stage=0 subbatch=1 compute-done` 已出现
  - 但 `worker stage=0 subbatch=1 done` 没出现
  - 因而卡点不在 compute 本体，而在 export DMA 提交路径

### 最新补充：本地“新 marker 是否进 binary”的判定方法已经修正

- 这轮确认了两个独立事实，之前不能混为一谈：
  1. **直接在**
     `build/rerocc-linux-tests`
     **目录里跑**
     `make rerocc_pipeline_runtime-linux`
     **是不可靠入口。**
     如果没有带
     `-f .../rerocc-linux-tests/Makefile`
     以及对应变量，
     GNU make 可能只把现有文件当作已存在目标，根本不会真正重建依赖。
  2. **即使真的重编了单个对象文件，单看 `grep` 也可能误判。**
     因为 `rr-acquire-inner ...` 是 `PRT_PROGRESS_LOG(...)`，
     若本地手工重编时用了默认
     `PIPELINE_RUNTIME_PROGRESS=0`，
     编译器会把这些字符串直接裁掉；
     同时如果没有清掉整个
     `.pipeline_runtime_objs/`，
     最终 binary 还会混入旧的、用别的宏配置编出来的对象文件。
- 当前已经找到与 FireMarshal `host-init.sh` 等价的本地预演入口：
  - 先删除：
    - `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
    - `build/rerocc-linux-tests/.pipeline_runtime_objs/`
  - 再按 pairdummy workload 默认宏重建：
    - `PIPELINE_RUNTIME_PROGRESS=1`
    - `PIPELINE_RUNTIME_PROGRESS_RAW=0`
    - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
    - `PIPELINE_RUNTIME_ONLY_MARKER=1`
    - `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
- 这轮本地等价重建已经成功完成，时间戳为：
  - `prt_dma.o`：
    `2026-04-11 15:37:02 UTC`
  - `prt_rerocc.o`：
    `2026-04-11 15:37:05 UTC`
  - `rerocc_pipeline_runtime-linux`：
    `2026-04-11 15:37:09 UTC`
- 并且 binary 已明确包含新 marker：
  - `rr-acquire-inner phase=before-csr-write`
  - `rr-acquire-inner phase=after-csr-write`
  - `rr-acquire-inner phase=after-csr-read`
  - `rr-acquire-inner phase=before-set-opc`
  - `rr-acquire-inner phase=after-set-opc`
- 因而下一轮 fresh FireMarshal / FireSim 复现，不再被“本地 binary 可能没吃到新 marker”这个问题阻塞。

## 2026-04-11 当前结论更新

### 最新补充：`run10` 已进入 `S99run`，并把前沿卡点推进到 `artifacts mapping load begin`

- `2026-04-11 14:56:57 UTC`，
  当前 live `uartlog`
  已明确走到：
  - `running /etc/init.d/S99run`
- 因而这轮不能再把问题归到：
  - Linux early boot
  - `S40network`
  - wrapper 根本没启动
- 用**私网 SSH**
  `192.168.1.228`
  对 live image 做 `debugfs` 检查后，已确认 image 内已经落下：
  - `/firemarshal.sh`
  - `/etc/init.d/S99run`
  - `/firemarshal.env`
  - `/root/pipeline-runtime-debug/bertmini-batch8.log`
  - `/root/pipeline-runtime-debug/bertmini-batch8.runner-early.stage`
  - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
  - `/root/pipeline-runtime-debug/bertmini-batch8.status`
- 当前这轮 image 内最关键的 runtime 稀疏日志已经推进到：
  - `yaml model load end`
  - `init load-pipeline-yaml end`
  - `init validate-artifacts begin`
  - `artifacts mapping cache disabled ... reason=env`
  - `artifacts mapping load begin layer_mapping=/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.yaml`
- 但截至
  `2026-04-11 15:03 UTC`
  为止，当前 live image 里**始终没有**继续看到：
  - `artifacts mapping load end`
  - `artifacts mapping parse begin`
  - 任意 `artifacts mapping parse progress`
  - `artifacts mapping parse end`
  - `init validate-artifacts end`
  - 更后面的 `submit-begin / rr-acquire-begin`
- 更重要的是，host 侧证据也和这个停点一致：
  - live image 文件
    `/home/ubuntu/sim_slot_0/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy0-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
    的 `mtime`
    停在：
    `2026-04-11 15:00:15 UTC`
  - 但同一时刻
    `/home/ubuntu/sim_slot_0/heartbeat.csv`
    仍继续增长到：
    `664s`
  - 也就是说：
    **guest/仿真没有死，但从 `artifacts mapping load begin` 之后没有新的文件落盘证据**
- 这轮因此需要把当前前沿 blocker 暂时改写为：
  **pairdummy / sbus128 / no-deep-log 路径下，live 停点位于 `prt_validate_gemmini_artifacts() -> parse_mapping_file() -> load_file()` 的更早 read/load 区间。**
- 但这里还不能下结论说
  `layer_mapping` 路径本身在这套配置上天然不通，
  因为本地同日旧 capture：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-nochkpt-noaudit-exportsubmithang-20260411/bertmini-batch8.log`
  已经证明：
  - 同一 `pairdummy+sbus128`
  - 同一个 `12,029,923 bytes` 的 layer mapping yaml
  - 同一个 `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`
  路径曾经成功走过：
  - `artifacts mapping load end`
  - `artifacts mapping parse end`
  - 并最终推进到更靠后的 export-submit hang
- 这条对照目前只说明：
  - 当前 live 停点**不一定**是 layer mapping 语义本身错误
  - 更可能是：
    - `load_file()` 内部缺少更细 marker，导致还不知道卡在 `open/lseek/malloc/pread` 哪一步
    - 或者 `no deep log` 这条新路径触发了新的、比旧 export-submit hang 更早的 I/O 行为差异
- 当前必须记住的一个细节是：
  上述旧 capture 的 `status` 里
  `guest_deep_log_enable=1`，
  而这轮 `run10` 明确是
  `guest_deep_log_enable=0`。
  因而不能把那份旧成功现场直接等价解释成
  “关闭 deep log 后 mapping load 一定也能过”。

### 最新补充：fresh image 已重新 build/install 并完成私网远端 freshness 闭环，`run10` 正在复现“no deep log”路径

- `2026-04-11 14:41 UTC` 之后，
  当前 pairdummy workload 已重新完成：
  - `marshal build`
  - `marshal install`
  - 本地 image freshness 自动校验
- 当前这轮 fresh FireMarshal 产物对应日志：
  - build tmux：
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-build3.pane.log`
  - install tmux：
    `/home/ubuntu/chipyard/tmp/firemarshal-tmux/marshal-pairdummy-install1.pane.log`
  - 两者 exit code 都是 `0`
  - 两者末尾都已打印
    `verify_pairdummy_firemarshal_image_freshness.sh`
    的 `PASS`
- 这轮新 run farm 已重启：
  - launch session：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch7a.pane.log`
  - 实例：
    `i-07def72243ac94e24`
  - 私网 IP：
    `192.168.1.228`
- 本轮 `infrasetup` 要以第二次干净重跑结果为准：
  - 第一轮：
    `pairbert-b8-d12s128-infra10`
    在
    `instance_liveness`
    处退出，
    但这轮中间做过额外观测，不要把它直接写成新的稳定 blocker
  - 随后已做最小复现：
    用与 FireSim 相同的 Fabric 配置对
    `192.168.1.228`
    执行
    `uname -a`
    /
    `echo $0`
    都成功
  - 第二轮干净重跑：
    `pairbert-b8-d12s128-infra11`
    已成功完成，exit code 为 `0`
- 当前这轮远端 freshness 也已再次闭环通过，而且**只用私网 SSH**：
  - 脚本：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`
  - workload：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
  - private ip：
    `192.168.1.228`
  - 校验结果：
    - local image sha256 =
      `185f452f477d5f86953c6c8b02fbda4f763dad2f93337caf53d741fd3d953a2f`
    - remote image sha256 =
      `185f452f477d5f86953c6c8b02fbda4f763dad2f93337caf53d741fd3d953a2f`
    - image 内 env 已确认是当前这轮目标配置：
      - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- 因而当前不能再把问题归到 stale image / stale env。
  这轮真正正在跑的是：
  **file-only sparse log + no deep log + no checkpoint/audit**
  的新 image。
- 新一轮 `runworkload` 已启动：
  - tmux session：
    `pairbert-b8-d12s128-run10`
  - 结果目录：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-11--14-50-51-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
  - run log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-11--14-50-51-runworkload-J2SAVCJFQ8GB2FFA.log`
- 当前这轮 `run10` 的目标非常明确：
  - 验证在关闭 guest deep log 后，
    是否还能复现
    `submit-begin -> rr-acquire-begin`
    的停点
  - 如果停点消失，
    则
    `PRT_MARKER_LOG`
    / deep log 直写 guest 文件
    很可能就是上一轮的新 blocker
  - 如果停点仍在，
    再根据 `prt_dma.c` 新加的 sparse marker
    继续缩小到
    `dma_blocking_submit()`
    内部更细区间

### 最新补充：已进入“关闭 checkpoint/audit 文件写”的受控复现，且远端新 image 已按私网校验闭环

- 本轮受控复现的目的已经明确收敛为：
  区分旧 freeze 到底是
  `fixed-weight DMA/硬件提交路径卡住`
  还是
  `checkpoint/audit 高频文件写把 child 卡在 guest I/O`。
- 因此这轮没有继续改 runtime 主逻辑，
  而是先把复现场景压缩到最小：
  - 只跑 `METHODS=ours2`
  - 关闭：
    - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0`
  - 保留文件侧粗/细日志：
    - `PIPELINE_RUNTIME_LOG_PROFILE=manual`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=1`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=log`
  - 仅对 `segment 0` 开 deep marker：
    - `DEEP_LOG_ENABLE=1`
    - `DEEP_LOG_SEGMENT=0`
    - `DEEP_LOG_GLOBAL_STAGE=4294967295`
    - `DEEP_LOG_LOCAL_STAGE=4294967295`
    - `DEEP_LOG_SUBBATCH=4294967295`
    - `DEEP_LOG_STAGE_RADIUS=0`
    - `DEEP_LOG_SUBBATCH_RADIUS=0`
  - 打开 DMA 提交跟踪并缩短 export timeout：
    - `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=1`
    - `EXPORT_DMA_TIMEOUT_MS=10000`
- 上一轮 freeze 的最关键静态证据仍然成立：
  - guest/仿真本身没死：
    `heartbeat.csv` 持续增长
  - 粗日志最后稳定停在：
    `worker stage=0 subbatch=4 done`
  - `checkpoint.log` 最后完整成功到：
    `token=2371 wait-doneflag-done`
  - 下一笔待提交 DMA 为：
    - `src=0x1047a2800`
    - `dst=0x40605c00`
    - `bytes=1024`
  - 文件最后卡在半行：
    `dma stage=0 checkpoint=program-post-fence ...`
  - 所以这轮“先去掉 checkpoint/audit 文件写再复现”的方向是有依据的，
    不是盲目换配置。
- 这轮新 image 已重新 `marshal build` + `marshal install`，
  并且当前不再只看本地 freshness：
  - `2026-04-11 14:14 UTC`，
    新实例
    `i-006f62c2159f5907d`
    上的 `firesim infrasetup`
    已完成，tmux exit code 为 `0`
  - 当前 run host：
    - 私网 IP：
      `192.168.1.173`
    - 公网 IP：
      `184.32.138.98`
  - 但后续 live 查询和 SSH
    **仍然只能使用私网地址**
    `192.168.1.173`；
    这是当前调试硬约束，不允许改回公网路径
  - 已通过：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`
    对远端 image 做闭环校验
  - 校验结果表明：
    - 本地 image SHA256 =
      `5974c9d3127c2d5b9f6b45790f473ff18fb93bc35c635c3b67bb4ccba51fae0d`
    - 远端 `/home/ubuntu/sim_slot_0/...pairdummy.img`
      SHA256 =
      `5974c9d3127c2d5b9f6b45790f473ff18fb93bc35c635c3b67bb4ccba51fae0d`
    - `/firemarshal.env` 中已确认带有本轮诊断开关：
      - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='1'`
      - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
      - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- 因而到当前时刻，
  “是不是旧 image / 旧 rootfs / 旧 env”这条怀疑链已被排除；
  下一步应直接启动这轮 `runworkload`，
  用 guest 文件日志判断 freeze 在关闭 checkpoint/audit 后是否仍存在。

### 最新补充：这轮 live 复现已越过旧 freeze 点，但又暴露出一个更靠后的 `stage 0 tensor 2 export DMA submit` 卡点

- 这轮 decisive run 使用的关键前提必须写清楚：
  - workload/result 目录：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-11--14-17-00-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
  - 实例：
    `i-006f62c2159f5907d`
  - 私网：
    `192.168.1.173`
  - 本轮 live 证据已固化到：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-nochkpt-noaudit-exportsubmithang-20260411/`
  - run farm 已按正规流程终止，
    终止后实例状态已进入：
    `shutting-down`
- 这轮 guest 端最终确认到的运行配置为：
  - `audit_log_enable=0`
  - `checkpoint_log_enable=0`
  - `guest_deep_log_enable=1`
  - `stdio_capture_mode=log`
  - `dummy_gemmini_mode=1`
  - `skip_model_bin_load=1`
  - `skip_input_load=1`
  - `skip_golden_check=1`
  - `mlockall_mode=2`
- 这轮最重要的新事实是：
  关闭 `checkpoint/audit` 文件写之后，
  runtime **明确已经越过上一轮旧 freeze 点**，
  不再停在
  `worker stage=0 subbatch=4 done`
  附近。
- 当前稀疏日志已经明确推进到：
  - `worker stage=0 subbatch=0..6 done`
  - `worker stage=0 subbatch=7 begin`
  - `segment=0 sink-progress=7/8 elapsed_ms=2008 fatal=0 stop=0`
- 也就是说，
  “关掉 checkpoint/audit 后仍然在旧 fixed-weight DMA 前就冻结”
  这个说法已经被这轮 live 证据排除。
- 但这轮又暴露出一个新的、更靠后的 live 卡点：
  - `heartbeat.csv` 继续稳定增长
  - guest `status` 仍是 `state=running`
  - 但 `bertmini-batch8.log` 与 `bertmini-batch8.deep.log`
    在约 `70s` 观察窗口内大小完全不再增长：
    - sparse log:
      `977089` bytes
    - deep log:
      `2336657` bytes
- 当前 deep log 的最后关键路径已经缩到：
  `stage=0 / subbatch=7 / tensor=2 / export alias DMA`
  - 先完成了一段更早的 bounce path：
    - `tok=2992..3016`
    - `done_pa=0x10224e078`
  - 随后进入真正的 alias export：
    - `export-target stage=0 tensor=2 layer=0 slot=3 target=address target_slot=3 dst=0x3faac9f400 bytes=65536 pages=64 dma=0 timeout_ms=10000 begin`
    - 并成功推进到：
      `tok=3017..3029`
      ，其 `wait-end` 均为
      `rc=0 hw_done=0`
  - **最后一条新日志** 停在：
    - `dma-submit stage=0 tensor=2 phase=submit-begin src=0x40502400 dst=0x1047c7800 bytes=1024 src_acc=0 dst_acc=0 timeout_ms=10000`
  - 后续本该出现的：
    - `rr-acquire-begin`
    - `submit-end tok=3030 ...`
    - `wait-begin tok=3030 ...`
    都没有再出现
- 当前 sparse log 尾部则停在重复的 doneflag 地址翻译：
  - `va=0x3faac44078 -> pa=0x10224e078`
  - `va=0x3faac43ff8 -> pa=0x47ee2cff8`
  - 但这些只是文件里的最后已有行；
    它们**没有**在后续观察窗口里继续增长。
- 因而当前更准确的新 blocker 描述应更新为：
  **在关闭 checkpoint/audit 文件写后，旧 freeze 点已被越过；新的 live 卡点位于 `dma_submit_wait_annotated() -> prt_dma_submit() -> dma_blocking_submit()` 的更早入口区间，落在 `submit-begin` 之后、`rr-acquire-begin` 之前。**
- 这条区间的静态代码顺序当前已核对：
  - `submit-begin` 这条 marker 来自
    `prt_dma.c`
    的
    `dma_submit_wait_annotated()`
  - 真正缺失的是其内部调用
    `prt_dma_submit()`
    后，
    `dma_blocking_submit()`
    里更早的几步：
    - `prt_trace_on_dma_submit(rt)`
    - `prt_trace_log_event(...)`
    - `dma_debug_capture_req(tok, req)`
    - 以及紧接着的
      `rr-acquire-begin` marker
- 当前最重要的两个推断如下：
  - 第一，
    `checkpoint/audit` 文件写**不是当前唯一 blocker**；
    它们被关掉后，执行确实更靠后了，但新的 hang 仍然存在
  - 第二，
    由于
    `PRT_MARKER_LOG`
    在当前 build 下走的是
    `prt_deep_write_all_impl()`
    直写 guest deep log 文件路径，
    而本次 freeze 又恰好发生在
    下一条 deep marker
    `rr-acquire-begin`
    之前，
    所以当前存在一个**很强但尚未最终证实**的新怀疑：
    **deep log 文件追加本身可能就是新的阻塞源。**
- 因而下一轮最小化验证不应该再沿用当前同等体量的 deep log，
  而应该改成：
  - 关闭 `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE`
  - 保留 file-only sparse log
  - 在
    `dma_blocking_submit()`
    的这几个点之间，
    加少量 `PRT_PROGRESS_LOG/ERR_LOG`
    级别的稀疏 marker：
    - `submit-inner-before-trace`
    - `submit-inner-after-trace-count`
    - `submit-inner-after-trace-event`
    - `submit-inner-after-capture-req`
    - `submit-inner-before-rr-marker`
    - `submit-inner-after-rr-marker`
  - 这样可以把“deep log 追加阻塞”与“`prt_dma_submit()` 内部真实逻辑阻塞”区分开
    ，而不会再被海量 deep marker 干扰。

## 2026-04-09 当前结论更新

### 最新补充：当前 `pairdummy/sbus128` 的核心静态结论已经转到 `ALL_RINGBUFFER` 语义错位

- 本轮最重要的静态结论如下：
  - 当前 `bertmini segment=3` 的问题，**不能再先验地归因到**
    `mlockall` / done-flag / pagemap / yaml 文本格式。
  - 对当前这条 bring-up 线，最值得优先对齐的是
    `HybridMapper -> MudnacSim -> pipeline-runtime`
    三者之间的 tensor/buffer 语义，尤其是 `ALL_RINGBUFFER`。
- 已静态确认：
  - `bertmini` 当前 case 里的 `tensor 6`
    **不是** segment 边界 tensor；
    它是 `segment 3` 内部
    `stage0(layer 3) -> stage1(layer 4)` 的运输 tensor。
  - 证据一：
    `model.layers.yaml` 中，
    layer 3 输出 slot 和 layer 4 输入 slot
    对 `tensor 6` 使用的是同一对 alias 地址：
    - `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml`
    - layer 3:
      - `address[3] = 470016`
      - `address2[3] = 8997888`
    - layer 4:
      - `address[2] = 470016`
      - `address2[2] = 8997888`
  - 证据二：
    `pipeline_mapping...ours2.yaml` 中，
    `segment_idx: 3` 明确写成：
    - stage 0 export `tensor 6`:
      `exportTensorTypeList: [ALL_RINGBUFFER]`
    - stage 1 entry `tensor 6`:
      `entryTensorTypeList: [ALL_RINGBUFFER, DRAM]`
    - 同时还有独立 ring binding：
      - `ring_buffer_count: {6: 2}`
      - `bufferBindingKindList` 中存在 `RING`
      - 对应 `tensor 6`
  - 证据三：
    同一份 pipeline yaml 里，
    `tensor 19 / 22 / 46`
    也都出现了同类模式：
    前一 stage export 为 `ALL_RINGBUFFER`，
    后一 stage entry 为 `ALL_RINGBUFFER`，
    且都发生在 segment 内部，而不是 segment 首尾边界。
- 基于 `HybridMapper` 代码，当前更强的结论是：
  - segment 边界 tensor 在初始化时会被标成 `TYPE_IO / IO_SINGLE`
  - 后续 SA 搜索又显式跳过 `TYPE_IO`
  - 因而在**现有搜索流程**里，
    segment 边界 tensor 原则上不应被改造成
    `INTER_PURE_DECOUPLING / ALL_RINGBUFFER`
- 基于 `MudnacSim` 代码，当前更强的结论是：
  - `ALL_RINGBUFFER` 的设计语义是
    **pure ring transport**
  - 也就是：
    - 直接在 ring slot 上生产/消费数据
    - 不额外保留 stage 本地 pipe buffer storage
    - 不应再无条件 materialize 回 model alias
- 当前 `pipeline-runtime` 的主要语义偏离点是：
  - 它虽然已经把 `ALL_RINGBUFFER`
    正确分类成
    `C7_ENTRY_ALL_RING / C8_EXPORT_ALL_RING`
  - 也能在 `stage_tensor_current_pages()` 中
    回退到 ring slot pages
  - 但在 export 路径里，
    仍会通过
    `sync_stage_export_aliases()`
    和
    `copy_tensor_pages_to_model_aliases()`
    把同 tensor id 的所有 layer slot alias
    再 materialize 一遍
  - 这与 `MudnacSim` 的 ring-only 语义不一致
- 当前工作树里有一个局部止血 patch：
  - 通过 `(dst,size)` 去重 alias target，
    避免对同一地址重复 copy
  - 这个 patch **不是最终修复**
  - 它只能说明“当前 alias materialization 可能重复命中同一目标”，
    不能说明“当前语义已经正确”
- 这条主线的详细审计文档已新增：
  - `docs/hybridmapper_mudnacsim_tensor_semantics_audit_20260409.md`
- 后续凡是讨论 `pairdummy segment=3`，
  应优先引用这份语义审计，
  不要再把 `tensor 6` 直接写成
  “segment 边界不该是 ALL_RINGBUFFER”。

## 2026-04-08 增量状态

### 最新补充：image freshness 现在是硬约束，且已有自动校验

- 当前已经确认一个真实失误模式：
  - 只看 `marshal build/install` 的 exit code，
    不能证明 guest image 里已经带上最新脚本/二进制。
  - 这次就实际踩到了两层问题：
    - 先是增量 build 没刷新 rootfs
    - 后来又发现改的是 workload overlay 副本，
      但 `host-init.sh` 会在 build 时用真正源文件重新覆盖它
- 因而从现在开始，这条约束必须固定：
  - 每次 pairdummy workload 做完 FireMarshal `build` 或 `install`，
    都必须检查 image freshness；
    **未通过不得继续 `infrasetup` / `runworkload`。**
- 当前自动校验工具已经落地：
  - 脚本：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh`
  - 它会精确比较 image 内三项内容与 host 当前产物是否完全一致：
    - `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
    - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
    - `/firemarshal.env`
  - 当前 `scripts/firemarshal-tmux-run.sh`
    已经对
    `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
    这条 workload 的 `build/install`
    自动执行这项检查；
    如果检查失败，tmux wrapper 必须整体失败。
- 当前本地已验证：
  - 新 image 的 freshness check 通过
  - 因而后续如果再出现“看起来像旧 image”的现象，
    优先先看 freshness check，而不是直接推进 FireSim run

### 最新补充：`mode=2 + targeted lock` 已越过旧 blocker，当前 live 停在 `yaml pipeline file read` 之后

- 截至 `2026-04-08 14:40 UTC`，
  当前 `sbus128 pairdummy` live run 使用：
  - 实例：
    `i-04882f1770f2d3b6e`
  - 私网：
    `192.168.1.21`
- 这轮现场已经确认：
  - guest 已进入 `S99run`
  - `/root/pipeline-runtime-debug/` 已创建
  - `bertmini-batch8.status` 显示：
    - `state=running`
    - `mlockall_mode=2`
    - `skip_model_bin_load=1`
    - `skip_input_load=1`
    - `skip_golden_check=1`
  - `bertmini-batch8.log` 明确已经越过旧 blocker：
    - `[prt-early] process memory lock mode=disabled flags=0x0`
    - `[prt-early] skip mlockall by build config`
    - `yaml model load end ...`
    - `yaml pipeline file read ... bytes=67866`
- 与旧成功 run 对照后，当前这轮新的精确停点是：
  **稳定停在 `yaml pipeline file read` 之后、`yaml pipeline load end` 之前。**
- 同一份 host 侧 HybridMapper artifact 当前静态信息如下：
  - `model.layers.yaml` / `pipeline_mapping.*.yaml` /
    `gemmini_layer_mapping.*.yaml`
    的 mtime 均为 `2026-03-26`
  - 说明这轮 image 的核心变化主要来自
    guest userspace / runtime / wrapper，
    **不是** pipeline yaml 本身在 `2026-04-08` 当天被重新生成。
- 当前工作树又新增两类最小调试修改：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_yaml_loader.c`
    - 为 `prt_load_pipeline_yaml()` 增加
      `parse begin / parse line / segment-push / stage-push / validate begin / validate segment-*`
      级别的停点日志
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh`
    - `write_pipeline_runtime_guest_env()` 不再把空值导出成 `VAR=''`
    - 这样可避免 runner 二次 source `/firemarshal.env` 时，
      把 wrapper 已经传入的有效参数覆盖成空串
- 当前已经静态证实一处真实的软件差异：
  - 旧 image / wrapper 路径下，
    `status` 与 runner 实际运行参数是一致的
  - 新 image 里，
    `/firemarshal.env` 会把未设变量导出为空串，
    runner 再次 source 后会覆盖 wrapper 传入值；
    现象之一就是：
    - wrapper status 里 `trace_enable=0`
    - runner 日志里却打印 `trace enable=1`
- 另外，已经补了一个本地 smoke test：
  `/home/ubuntu/chipyard/tmp/prt_yaml_loader_smoketest.c`
  - 在 x86 host 上直接调用当前源码的
    `prt_load_model_yaml()` 和 `prt_load_pipeline_yaml()`
  - 使用同一份
    `model.layers.yaml` +
    `pipeline_mapping....ours2.yaml`
    可以完整成功跑通：
    - `model ok layers=40`
    - `pipeline ok segments=32 subbatch=1`
  - 因而当前结论是：
    **pipeline yaml 内容和 parser 主逻辑本身并未在 host 上复现卡死；**
    当前 blocker 更像是
    RISC-V guest / runtime binary / guest userspace 环境
    相关的问题。
- 约束保持不变：
  后续 SSH / live 查询仍然**只能使用私网地址**
  `192.168.1.21`。

### 最新补充：新停点 image 已重建并 install，当前卡在新一轮 `launchrunfarm` 的 F2 容量等待

- 为了继续沿 `yaml pipeline` 主线缩小停点，
  当前工作树已经完成：
  - 本地交叉编译验证：
    `rerocc_pipeline_runtime-linux`
  - FireMarshal build：
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-build-2026-04-08--14-42-41-OFJWWUMWQYY1PCFS.log`
  - FireMarshal install：
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-install-2026-04-08--14-43-50-7U71IARKRWOAP47R.log`
- 新 image 当前为：
  `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
  - mtime：
    `2026-04-08 14:42:55 +0000`
  - sha256：
    `8aaf59c1460cc5780ed5910aacb1eb8118cf6c1a4c1b913cc9b8fca2a7d450e0`
- 新 runtime binary 当前为：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
  - sha256：
    `db0e83e2267ca8f3571252dad7d1a589f4d71a5951c6dc26e6ebde8160beb325`
- 已静态确认：
  - 新 image 里的 `/firemarshal.env`
    不再导出空值覆盖 runner 参数
  - 新 binary 已包含：
    - `yaml pipeline parse begin`
    - `yaml pipeline parse line=...`
    - `yaml pipeline validate segment-begin`
    - `yaml pipeline validate end`
- 旧 run 的现场已经固化到：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-mode2-pipelineyamlhang-20260408/`
- 旧 runfarm 已按正规流程终止：
  - instance：
    `i-04882f1770f2d3b6e`
  - 最新状态已到：
    `shutting-down`
- 下一轮 FireSim 已经重新发起：
  - launch session：
    `pairbert-b8-d12s128-launch2`
  - pane log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch2.pane.log`
  - 当前 manager 还没有失败退出，
    但暂时卡在 AWS F2 容量不足重试：
    `Tried all subnets, but there was insufficient capacity to launch your instances`
- 因而截至当前更新：
  **代码/镜像侧已准备完毕，下一步 blocker 是 AWS `f2.6xlarge` 容量。**

### 最新补充：新 image 下 `MCL_CURRENT` 再次卡在 `before mlockall`，已切到 targeted lock 路线

- 截至 `2026-04-08 14:10 UTC`，
  用私网 `192.168.1.159`
  live 抓到的这轮 `sbus128 pairdummy` 现场已经确认：
  - guest 已进入 `S99run`
  - firemarshal wrapper 已经启动 runner
  - `bertmini-batch8.log` 稳定停在：
    - `[prt-early] before process memory lock`
    - `[prt-early] process memory lock mode=current-only flags=0x1`
    - `[prt-early] before mlockall`
  - `bertmini-batch8.status` 同时显示：
    - `state=running`
    - `mlockall_mode=1`
    - `skip_model_bin_load=1`
    - `skip_input_load=1`
    - `skip_golden_check=1`
- 与 `2026-04-08 09:08 UTC` 那轮成功现场对比，
  旧日志明确有：
  - `[prt-early] after mlockall rc=0`
  - `yaml model load begin ...`
  因而对“当前这版 rootfs/image”来说，
  **`MCL_CURRENT` 也已经不能再视为稳定可过**。
- 这轮 live 证据已经固化到：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-currentonly-mlockhang-20260408/`
  其中包括：
  - `bertmini-batch8.log`
  - `bertmini-batch8.status_and_stage.txt`
  - `heartbeat.csv`
  - `uartlog.txt`
- 基于这轮证据，当前工作树已做两处最小修复：
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
    - 新增通用 helper，
      把真实 file-loaded blob 也走
      `prefault + mlock(buf)`，
      覆盖：
      - model bin
      - input blob
      - golden blob
    - synthetic model 也统一复用同一条 targeted lock helper
  - `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
    - 默认把
      `PIPELINE_RUNTIME_MLOCKALL_MODE`
      从 `1`
      切到 `2`
      （即这条调试 workload 不再走 process-wide `mlockall`）
- 这批修改对应的新 FireMarshal 生成物已经完成：
  - build log：
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-build-2026-04-08--14-11-42-R32QWU6O6OSSHSHF.log`
  - install log：
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-install-2026-04-08--14-12-14-NXLZ4T6GOFAW9QSZ.log`
  - image：
    `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
    - mtime：
      `2026-04-08 14:11:57 +0000`
    - sha256：
      `b126decc8089e70adeeaa2981fc23d96729b65b0b5754446c14674a9dffdead6`
  - bin：
    `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-bin`
    - sha256：
      `8093b3a3787436cd249d3f2c333308bf6d62a54e6f84554a5927d13c8cbd157f`
- 当前已经按正规流程终止旧 runfarm：
  - instance：
    `i-04e1845c31be1eb56`
  - 私网：
    `192.168.1.159`
  - 截至本次更新状态：
    `shutting-down`
- 新一轮 `launchrunfarm` 已经发起，
  但当前阻塞不是代码，
  而是 AWS `f2.6xlarge` 暂时容量不足；
  FireSim manager 正在自动轮询重试。
- 约束保持不变：
  后续 live SSH 查询**只能使用私网地址**，不能回退到公网地址。

### 最新补充：`prefault + targeted mlock(blob)` 已复跑，当前停点前移到 `load_model_yaml()`

- 截至 `2026-04-08 12:47 UTC`，基于
  `sbus128 pairdummy`
  的 fresh 复跑已经完成一轮：
  - runtime：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
  - hwdb：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
  - AGFI：
    `agfi-0dc8dcfa4c7735f40`
  - 实例：
    `i-002abfde0b580a968`
  - 私网：
    `192.168.1.199`
- 这轮 guest 仍然使用：
  `PIPELINE_RUNTIME_MLOCKALL_MODE=1`
  （也就是 `MCL_CURRENT` only），
  同时已经带上 synthetic-model 的
  `prefault -> mlock(blob)` 修复。
- 这轮最重要的新证据不是
  `segment 0 not_ready (-7)`，
  而是：
  runtime 子进程当前**没有走到 synthetic-model 分配日志**，
  live guest 文件稳定停点已经进一步前移：
  - 第一轮 marker 停在：
    `[prt-progress] yaml model file read path=/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml bytes=18518`
  - 第二轮加入 `prt_yaml_loader.c` parser marker 之后，
    又进一步停在：
    `[prt-progress] yaml model load begin path=/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml`
  - 在一次 `65s` 的 live 观察窗口内，
    `bertmini-batch8.log` 大小固定为：
    `4599`
  - 后面一直没有出现：
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
- 因而截至当前更新，最新优先级最高的 blocker 已经前移为：
  **`prt_load_model_yaml()` 里的 `load_file()` 本体。**
- 这轮 live 证据已经固化到：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/pairdummy-sbus128-prefaultmlock-yamlhang-20260408/`
  其中包括：
  - `bertmini-batch8.log`
  - `bertmini-batch8.status_and_stage.txt`
  - `heartbeat.csv`
  - `uartlog.txt`
- 为了继续缩小这个停点，当前工作树已新增：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_yaml_loader.c`
  的细粒度 marker 和 parser 防自旋保护，包括：
  - `yaml model parse begin`
  - 前 12 行和每 64 行的解析进度日志
  - `layer-push` / `layer-index` 进度日志
  - `yaml model parse error line=... key=...`
  - `yaml model parse end`
  - 对 `strtoul/strtoull` 无前进场景直接返回 `PRT_ERR_PARSE`
- 在此基础上又继续加入了
  `load_file()` 级别的 step marker：
  - `yaml file open begin/end/fail`
  - `yaml file seek-end begin/end/fail`
  - `yaml file ftell begin/end/fail`
  - `yaml file seek-set begin/end/fail`
  - `yaml file alloc begin/end/fail`
  - `yaml file read begin/end/fail`
  - `yaml file close end`
- 这批 `prt_yaml_loader.c` 修改已经完成：
  - 主机侧语法编译验证
  - RISC-V 交叉编译验证：
    `/tmp/prt_yaml_loader.riscv.o`
- 这轮 run 已经按 manager 正规流程终止：
  - `terminaterunfarm` tmux exit code：
    `0`
  - AWS 当前状态：
    `shutting-down`

### Live/SSH 约束补充

- 如果要 SSH / live 查询 run host，**只能使用私有地址**。
- 即使同时知道公网地址，也不要使用公网地址做任何查询、挂载、抓日志或排障。
- 这条约束必须继续保留到后续所有 session。

### 最新补充：`sbus128 pairdummy` 上的 `mlockall current-only` 调试

- `2026-04-08` 当天又做了一步**静态收敛**，结论是：
  当前 `segment[0] failed: not_ready (-7)` 很可能不是
  `MCL_CURRENT` 本身的问题，
  而是
  `build_stage_conv_desc()`
  早期调用
  `stage_prepare_exec_views()`
  时，
  对 stage-0 fixed tensor 做
  `prt_dma_copy_dram_to_spm_pages()`
  之前，
  synthetic-model 的匿名 `mmap` 页还没有被 materialize。
- 这条返回链的关键点是：
  `stage_prepare_exec_views()`
  会在 worker 进入 `wrkrdy` / `gemm-run` marker 之前，
  直接把 fixed tensor 从 host DRAM 搬到 SPM；
  Linux/RISC-V 路径下这一步会走
  `prt_host_virt_to_phys()`，
  如果 host 页还没 present，
  就会直接得到
  `PRT_ERR_NOT_READY (-7)`。
- 因而当前更精确的判断是：
  **必须保证 future 映射里会被 DMA 读取的页在第一次 DMA 前已经 present；**
  但这不等于
  `MCL_FUTURE`
  这个 flag 本身必须保留为唯一实现方式。
- 当前树里已经加入一个**最小定点修复**：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
  里的 synthetic-model 匿名 `mmap`，
  现在会在返回前先按页 prefault，再对整个 blob 做定向 `mlock(...)`，
  目的是把
  `stage 0 fixed-tensor DMA`
  对
  `MCL_FUTURE`
  的首次 materialize 依赖拿掉，同时补上更接近已有 Linux DMA 负载的 buffer-level 锁页语义。
- 这个修复当前只完成了**交叉编译级验证**，
  还没有重新跑一轮 FireSim；
  所以下一轮要做的不是回去讨论
  “`MCL_FUTURE` 哲学上是不是必须”，
  而是直接用 fresh image/bin 在 `sbus128` 上复跑确认：
  `segment 0 not_ready (-7)` 是否前移/消失。

- 这轮为了继续在 `sbus128` 上缩小停点，已经引入一个**临时调试编译开关**：
  - `PRT_MLOCKALL_MODE=0`:
    保持原行为 `MCL_CURRENT | MCL_FUTURE`
  - `PRT_MLOCKALL_MODE=1`:
    只做 `MCL_CURRENT`
  - `PRT_MLOCKALL_MODE=2`:
    跳过 `mlockall`
  - `PRT_MLOCKALL_MODE=3`:
    只做 `MCL_FUTURE`
- 当前工作树里，默认只把
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
  这条 `pairdummy` 调试入口切到：
  `PIPELINE_RUNTIME_MLOCKALL_MODE=1`；
  其它入口仍保留 `0`，即原始锁页语义。
- 这轮已经重新完成：
  - FireMarshal `clean`
  - FireMarshal `build`
  - FireMarshal `install`
  - FireSim `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- 这轮新的 fresh pairdummy artifact 已固定为：
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
- 这轮新的 `sbus128 pairdummy` FireSim run：
  - 实例：
    `i-0dec87dd6cd788fee`
  - 私网：
    `192.168.1.158`
  - `runworkload` 结果目录：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-08--09-08-19-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
  - `runworkload` manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-08--09-08-19-runworkload-8VCBZYTD51YC25EE.log`
  - `terminaterunfarm` 已发起；
    截至本次更新实例状态为：
    `shutting-down`
- 这轮最重要的新结论是：
  - `mlockall_mode=1` 下，
    **已经明确越过**
    `before mlockall`
  - guest 粗日志在本地结果目录中明确记录：
    - `[prt-early] process memory lock mode=current-only flags=0x1`
    - `[prt-early] before mlockall`
    - `[prt-early] after mlockall rc=0`
    - `[prt-early] after mlockall mode=current-only flags=0x1`
  - 之后不仅没有卡在 `mlockall`，
    而且已经完整越过了 synthetic-model 路径：
    - `synthetic-model alloc before-compute`
    - `synthetic-model alloc after-compute size=17055744 ...`
    - `synthetic-model alloc before-mmap size=17055744`
    - `synthetic-model alloc after-mmap ptr=... size=17055744`
    - `synthetic-model alloc kind=mmap-lazy size=17055744 align=64`
    - `init synthesize-model-bin end ...`
- 因而对**当前这条 `pairdummy sbus128` 调试线**，可以把结论更新为：
  - `MCL_CURRENT` 本身不是当前 blocker
  - 先前“卡在 `mlockall` 前后”的现象更可疑的是
    `MCL_FUTURE`
    或
    `MCL_CURRENT | MCL_FUTURE`
    的组合作用
  - 一旦改成 `current-only`，程序会继续推进到更后面的 runtime 逻辑
- 这轮新的、比 `mlockall` 更后的停点已经固定为：
  - `worker stage=0 exit stop=0 fatal=-7 requested=0`
  - `segment[0] failed: not_ready (-7)`
  - `runtime_run failed: not_ready (-7)`
  - `BERTMINI_PIPELINE_RUNTIME_FAIL`
- 同时这轮 guest status 明确记录：
  - `state=finished`
  - `exit_code=1`
  - `mlockall_mode=1`
  - `dummy_gemmini_mode=1`
  - `skip_model_bin_load=1`
  - `skip_input_load=1`
  - `skip_golden_check=1`
- 这说明另一个重要事实：
  FireSim manager 这轮是
  `runworkload exit code = 0`
  正常收尾的，
  但 guest runtime 自身是
  `exit_code=1`
  失败退出。
  以后不能再把 manager 的成功退出误记成 workload pass。

- 用户要求“切回原来那个 sbus16 的硬件开展调试”后，已先按当前仓库里**真正存在且稳定跑通过的旧硬件基线**
  继续，而不是去造一个仓库内并不存在、且文档已说明 elaboration 不合法的
  `4c12p12 literal sbus16` 入口。
  本轮实际使用的是旧 coupled-DMA baseline：
  - AGFI：
    `agfi-06eb561d00d5c5dc1`
  - TARGET_CONFIG：
    `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`
  - FireSim runtime：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_reloadlogs.yaml`
  - FireSim hwdb：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb.yaml`
  - FireSim build recipes：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_coupleddma_10mhz.yaml`
- 这轮旧硬件调试前，已重新核对并固定 fresh workload artifact：
  - workload：
    `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
  - image：
    `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync.img`
    - mtime：
      `2026-04-08 04:39:15 +0000`
    - sha256：
      `064df3922b1836e2ce5b3b80b7b8d9bb433e814c514a88c89415333e38552aa6`
  - bin：
    `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-bin`
    - mtime：
      `2026-04-08 04:39:13 +0000`
    - sha256：
      `35c84ba5cc90637b623180e31b2a52cf0e96e43af29079370a958614e02b4352`
  - FireMarshal build log：
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-build-2026-04-08--04-38-18-WWRWDN2227C80CYC.log`
  - FireMarshal install log：
    `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-install-2026-04-08--04-39-26-TW7KSZ61I6CJLXZ6.log`
- 旧硬件这轮 `launchrunfarm -> infrasetup -> runworkload` 已完整走过：
  - 实例：
    `i-04a38386cefb6a2b4`
  - 私网：
    `192.168.1.72`
  - 公网：
    `44.255.191.81`
  - `infrasetup` 已确认通过：
    - `FireSim fingerprint: 0x46697265`
    - `FireSim driver readiness preflight passed for slot 0.`
- 旧硬件这轮 `runworkload` 的关键信号已经固定：
  - guest Linux 明确进入：
    `running /etc/init.d/S99run`
  - guest 文件日志链路工作正常，且没有把 runtime/bin 日志重新打回 UART：
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
  - status 明确记录：
    - `state=running`
    - `uart_log_enable=0`
    - `guest_log_enable=1`
    - `guest_deep_log_enable=1`
    - `audit_log_enable=1`
    - `dummy_gemmini_mode=1`
    - `skip_model_bin_load=1`
    - `skip_input_load=1`
    - `skip_golden_check=1`
- 旧硬件这轮的最终结论已经明确：
  - 它**也会**复现当前 synthetic-model 路径的停点
  - guest 粗日志最后一行稳定停在：
    `init synthesize-model-bin begin reason=skip-model-bin-load`
  - 两次取样之间：
    - `bertmini-batch8.log` 大小固定为 `28657` 字节
    - `bertmini-batch8.status` 大小固定为 `647` 字节
    - 但 `heartbeat.csv` 继续从
      `9580624629 cycles @ 1029s`
      增长到
      `11251493392 cycles @ 1196s`
  - 因而本轮旧硬件 A/B 说明的是：
    当前卡点并不是 pairdummy / sbus64 / 新硬件独有；
    在 `DUMMY_GEMMINI_MODE=1 + skip-model-bin-load=1` 的 synthetic-model 路径下，
    旧 coupled-DMA baseline 也会冻结在
    `init synthesize-model-bin begin`
- 这轮旧硬件抓回本地的关键证据保存在：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/oldhw-20260408/`
  - `uartlog.txt`
  - `heartbeat.csv`
  - `bertmini-batch8.log`
  - `bertmini-batch8.status`
- 为节约机时，这轮旧硬件 run 已人工判定为 hang，并已执行：
  `terminaterunfarm --forceterminate`
  - 截至本次更新，实例
    `i-04a38386cefb6a2b4`
    已进入：
    `shutting-down`

- 新的 A/B 硬件标签已固定为：
  - `f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c12p12_sbus64_20mhz`
  - AGFI：
    `agfi-03b04a5e09190465d`
- 这轮对比要求保持不变：
  - 不改 `pipeline runtime` 代码
  - 不改 `mlockall(MCL_CURRENT | MCL_FUTURE)` 原行为
  - 不改当前 pairdummy image/bin/workload 入口
  - 只把 pairdummy 硬件从 `sbus128` 切到 `sbus64`
- `sbus64` 的第一次标准 `infrasetup` 尝试没有进入 workload，
  失败在 FireSim driver build：
  - AGFI metadata 指向的 deploy quintuplet 需要
    `chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus64`
  - 但当前本地源码没有这个 Scala config 类
  - 因而 `sbus64` 首次失败目前应归类为
    “infra / driver config mismatch”，而不是 runtime 停点变化
- 为了继续做“只换 AGFI”的 A/B，本地 `sbus64` hwdb 已临时加入：
  - `deploy_quintuplet_override:
    ...PairManagerDummy16x16Sbus128-...`
  - 目的只是先复用现有 `sbus128` driver quintuplet，把 run 推进到 boot/workload 阶段，
    观察在同一 guest/runtime 入口下，`sbus64` 是否仍停在同样的软件位置
- 旧的 `sbus64` 首次失败实例：
  - `i-011327485d63c8832`
  - run_farm_tag：
    `pairbertb8d12s64a`
  - 当前已确认进入：
    `terminated`
- 基于上面的 override，新的 `sbus64` A/B 复跑已经启动：
  - `launchrunfarm` 成功
  - 新实例：
    `i-079f93c6a04f70d95`
  - 私网：
    `192.168.1.38`
  - run_farm_tag：
    `pairbertb8d12s64a`
  - `launchrunfarm` manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-08--01-56-21-launchrunfarm-QO7J65AWRFA8WMHS.log`
  - `infrasetup` manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-08--01-57-13-infrasetup-8F7GKMRQKL8XNXHF.log`
  - 当前观察点：
    这轮已经推进过第一次 `sbus64` 的“本地缺 deploy config 直接失败”门槛，
    进入了与旧 `sbus128` 成功样例相同的 `GoldenGateMain` 长阶段；
    截至 `2026-04-08 02:22 UTC`，尚未出现
    `FireSim fingerprint` / `driver readiness preflight passed`
    或新的显式异常
- 上面这轮 `sbus64` 复跑随后已继续推进并确认：
  - `infrasetup` 最终成功
  - 关键证据：
    - `FireSim fingerprint: 0x46697265`
    - `FireSim driver readiness preflight passed for slot 0.`
  - `runworkload` 已成功进入 guest Linux
  - 远端 `uartlog` 已明确到达：
    - `running /etc/init.d/S99run`
  - 同时，直接从远端块镜像只读提取出的 guest 文件日志显示：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
      记录 `state=running`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
      最后一行稳定停在：
      `init synthesize-model-bin begin reason=skip-model-bin-load`
  - 二次取样确认这不是单纯“慢”：
    - `bertmini-batch8.log` 大小固定为 `28656` 字节
    - `bertmini-batch8.status` 大小固定为 `647` 字节
    - 但 `heartbeat.csv` 继续从
      `15426633737 cycles @ 813s`
      增长到
      `21370073356 cycles @ 1110s`
  - 因而当前更准确的结论是：
    `sbus64` A/B 并没有消掉当前停点；
    在不改 runtime 代码、保留原始锁页策略的前提下，
    这轮仍然是 **guest 进入 runtime 后冻结在
    `init synthesize-model-bin begin`**
  - 这轮抓回本地的关键证据保存在：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/sbus64-20260408/`
    - `uartlog.txt`
    - `heartbeat.csv`
    - `bertmini-batch8.log`
    - `bertmini-batch8.status`
  - 为节约机时，这轮 run 已人工判定为 hang，并已发起：
    `terminaterunfarm --forceterminate`
  - 实例：
    `i-079f93c6a04f70d95`
    截至本文档更新时状态已进入：
    `shutting-down`

- 已确认一个关键事实：`2026-04-02` 冻结旧主线跑通到 final mismatch 的那轮，
  **并没有走当前 pairdummy bring-up 的 synthetic-model 路径**。
  旧主线在 `validate-artifacts` 之后走的是：
  - `init load-model-bin begin path=/root/rerocc-linux-tests/pipeline-runtime/bertmini/runtime_model.bin`
  - `init load-model-bin end ...`
- 当前 pairdummy bring-up 因为默认打开：
  - `DUMMY_GEMMINI_MODE=1`
  - `PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD=1`
  所以在同一个位置之后走的是：
  - `init synthesize-model-bin begin reason=skip-model-bin-load`
- 因而当前 `sbus128 pairdummy` 线上停在 `synthesize-model-bin`，
  **不能**被表述成“旧硬件同一路径不挂、新硬件才挂”。
  更准确的说法是：
  - 旧 frozen mainline 与当前 pairdummy bring-up 同时改变了硬件和软件分支
  - 旧成功结果不能直接为当前 synthetic-model 路径背书
- `2026-04-07--16-24-24` 这轮 `sbus128 pairdummy` run 已人工终止并确认 run farm 实例已回收；
  这轮 live log 的最后稳定停点仍是：
  - `init synthesize-model-bin begin reason=skip-model-bin-load`
- 用户已明确要求：下一轮 **不要**继续改 `pipeline runtime`，特别是不要改锁页策略；
  继续保留当前 guest 的 `mlockall(MCL_CURRENT | MCL_FUTURE)` 原行为，
  先只换 AGFI 做 A/B。
- 下一轮要切到的新硬件配置是：
  - build / hardware config label：
    `f2_gemmini_rerocc_globalnoc_pairmanager_dummy16x16_4c12p12_sbus64_20mhz`
  - AGFI：
    `agfi-03b04a5e09190465d`
  - 本轮实验目的：
    在 **不改 image / 不改 runtime 代码 / 不改 workload 入口** 的前提下，
    只把 `sbus128 -> sbus64`，观察 `synthesize-model-bin` 停点是否仍然存在

## 2026-04-07 增量状态

- `2026-04-02` 冻结的旧主线结论不变：
  `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz` 上的
  `bertmini` mainline 仍然是“可执行完成，但 final golden mismatch 停在 tensor=48”。
- 本轮新增的是一条**独立 bring-up 分支**，目标是把 Linux/F2 `bertmini`
  入口真正切到新的 pair-manager dummy16x16 硬件，而不是继续复用旧 `8x8 small` 配置。
- 这条新分支当前不覆盖旧 frozen mainline；它的目的只是验证：
  - FireMarshal workload 是否已真正切到 pair-manager 语义
  - FireSim runtime/hwdb 是否已真正切到 `4c12p12 pair sbus128` AGFI
  - 在新硬件上，旧 `bertmini` mapping 能否先以部分 manager 跑起来

### 本轮新增入口

- 新 host-init wrapper：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
  - 默认打开：
    - `DUMMY_GEMMINI_MODE=1`
    - `PAIR_MANAGER_MODE=1`
    - `NUM_CORES=2`
    - `NUM_GEMMINI=2`
    - `NUM_DMA=2`
    - `GEMMINI_BASE_ID=0`
    - `DMA_BASE_ID=0`
- 新 workload json：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
- 新 FireSim hwdb：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
  - `default_hw_config`：
    `firesim_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128`
  - AGFI：
    `agfi-0dc8dcfa4c7735f40`
- 新 FireSim runtime：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
- 新一轮 `sbus64` A/B hwdb：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus64_bertmini.yaml`
- 新一轮 `sbus64` A/B runtime：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`

### 当前工作树与 frozen mainline 的关系

- 旧 frozen mainline 的**结果结论**仍然有效，但当前工作树已经不再是
  `2026-04-02` 那组脚本/默认值的逐字重建源。
- 除了新增 `pairdummy` 入口之外，本轮还改到了共享 workload / runtime 脚本：
  - `rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync.sh`
  - `rerocc-linux-tests-coupleddma/workload/host-init.sh`
  - `rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
  - `rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
- 其中一个关键偏移是：
  当前共享 `host-init-fileonly-sync.sh` 默认也会打开 `DUMMY_GEMMINI_MODE=1`。
- 因此：
  - 旧 frozen mainline 应继续作为 regression baseline / 结果基线使用
  - 但如果未来要“重新构建旧 mainline 镜像”，不能直接假定当前工作树仍与
    `2026-04-02` 冻结镜像等价
  - 必须先明确“要复用 frozen artifact，还是要基于当前脚本重新 build”

### 本轮为什么只先开 2 个 manager

- 当前 `conference/HybridMapper/output/pipeline_runtime/bertmini/` 里仍只有旧
  `TARGET_KEY`：
  `rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024`
- 目前没有新的 `12 pair / pair-manager` 专用 `bertmini` mapping artifact。
- 因此本轮 bring-up 先保持旧 `TARGET_KEY`，只在新硬件上启用前 `2` 个 pair-manager；
  目的不是吃满 `12 pair`，而是先验证 runtime 与新硬件接口是否通顺。

### 本轮镜像新鲜度证据

- 新 workload image：
  `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.img`
  - size:
    `323608576`
  - mtime:
    `2026-04-07 16:17:17 +0000`
  - sha256:
    `f601a7259a6b90bc015521fbd04868abed7da49f33f3cc28531445115b90d71a`
- 新 workload bin：
  `/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-bin`
  - size:
    `20957176`
  - mtime:
    `2026-04-07 13:21:18 +0000`
  - sha256:
    `ad997f0fdf70dbbbfd3ca37cfe1e5f3a6f7ae9ad47040fbb2017aa5036c19490`
- FireMarshal build log：
  `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-build-2026-04-07--16-17-04-6518KVBGUSSMZKDO.log`
- FireMarshal install log：
  `/home/ubuntu/chipyard/software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-install-2026-04-07--16-18-43-B10HFS1ZW6D6X73V.log`
- FireSim deployed workload：
  `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
  - `common_bootbinary` / `common_rootfs` 已明确指向上面这组新 `.bin/.img`

### 本轮运行态

- `launchrunfarm` 已成功：
  - tmux pane log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-launchrunfarm-pairbert-b8-d12s128.pane.log`
  - manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--13-22-41-launchrunfarm-0LDCB0XTXTYFDNPR.log`
  - 实例：
    `i-0fb056ae227c11453`
  - run_farm_tag：
    `pairbertb8d12s128a`
- `infrasetup` 已成功结束：
  - tmux pane log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-infrasetup-pairbert-b8-d12s128.pane.log`
  - manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--13-23-33-infrasetup-CEEMNNFR3RP95D99.log`
  - tmux exit code：
    `0`
  - 关键证据：
    - `FireSim fingerprint: 0x46697265`
    - `FireSim driver readiness preflight passed for slot 0.`
- `runworkload` 已启动，正在运行：
  - tmux pane log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/firesim-runworkload-pairbert-b8-d12s128.pane.log`
  - manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-07--14-21-24-runworkload-LP89LE8WKK9UB6EC.log`
  - 结果目录：
    `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-07--14-21-24-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-f2-gemmini-rerocc-pairmanager-dummy16x16-4c12p12-sbus128-linux-bertmini-batch8-fileonly-sync/`
  - 当前可见状态：
    - slot 0 driver preflight 已通过
    - simulation job 已启动

## 冻结结论

- 当前 `bertmini` 主线已经不再是 “Linux/F2 上 DMA 卡死”。最新可信结论是：**当前 mainline 已能完整执行完成，但 final golden mismatch**。
- 当前 golden mismatch 调查暂时搁置。除非新的 fresh run 明确从“执行完成”退化回“卡死”，否则不要把当前状态重新写回旧的 DMA submit hang。
- 当前交接默认应视为“没有需要继续挂着跑的实验”；恢复实验前，先重新核对 manager 状态和 EC2 实例，确保 run farm 已完全回收。

## 当前最可信主线结果

冻结 workload / 配置：

- runtime config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_reloadlogs.yaml`
- hwdb:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb.yaml`
- build recipe:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_coupleddma_10mhz.yaml`
- workload json:
  `/home/ubuntu/chipyard/sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync.json`

冻结结果目录：

- result dir:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/`
- coarse log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/bertmini-batch8.log`
- deep log:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/bertmini-batch8.deep.log`
- status:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/bertmini-batch8.status`
- uartlog:
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-04-02--11-14-34-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-f2-rerocc-linux-bertmini-pipeline-runtime-batch8-fileonly-sync-reloadlogs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync0/uartlog`

这轮结果的关键证据：

- `bertmini-batch8.log` 已明确到达：
  - `segment=31 threaded backend complete target_subbatch=8`
  - `golden mismatch: tensor=48 bytes=65536 mismatch=1823 first_idx=12 actual=8 golden=119 max_abs=135`
  - `golden mismatch summary: tensors=1 total_mismatch=1823 first_tensor=48 ...`
  - `runtime_run failed: mismatch (-13)`
- `bertmini-batch8.status` 已明确记录：
  - `state=finished`
  - `exit_code=1`
  - `uart_log_enable=0`
  - `guest_log_enable=1`
  - `guest_deep_log_enable=1`
- `uartlog` 已明确记录：
  - `Simulation complete.`
  - `*** PASSED *** after 44898716942 cycles`
  - `Script done on 2026-04-02 12:31:53+00:00 [COMMAND_EXIT_CODE="0"]`

当前正确解释：

- FireSim/guest/关机回收链路都已经跑通。
- 当前主线 failure 发生在 runtime final compare，而不是 Linux 启动、DMA submit、export wait、或 guest poweroff 阶段。
- 因而当前最准确的状态表述是：
  **“bertmini mainline completes, but current CPU-derived golden disagrees at tensor 48.”**

## 当前稳定工作负载

### 1. 主线回归 workload

- 名称：
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync`
- 用途：
  - 回归 Linux/F2 启动是否正常
  - 回归文件日志链路是否正常
  - 回归 `segment=31` 能否完整结束
  - 回归当前 failure 是否仍停留在 `tensor=48` mismatch
- 当前预期：
  - 应完成运行并触发 FireSim copy-back
  - 允许仍然返回 `tensor=48` mismatch

### 2. 小 Linux smoke

- 结果目录：
  `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-20--06-16-29-rerocc-lc-linux-coupleddma-regression-small-pipelinefiles-f2-rerocc-linux-regression-small-pipelinefiles/`
- 当前仍可采信的 marker：
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=1024`
  - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage PASS`
  - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking pass=1`
  - `NONBLOCKING_SUMMARY s1=1 s2=1 s3=1 s4=1`
  - `ALL_TESTS_PASS`
  - `COMMAND_EXIT_CODE="0"`
- 用途：
  如果未来又怀疑 FireSim infra、Linux bring-up、CoupledDMA 基本路径或 nonblocking 小回归坏掉，先回到这条 workload 对照，不要直接跳进 `bertmini` 主线。

## 当前稳定技术事实

### 硬件 / 运行时配置

- 当前主线 `default_hw_config` 是：
  `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
- 当前主线 AGFI 是：
  `agfi-06eb561d00d5c5dc1`
- 当前主线 TARGET_CONFIG 是：
  `GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`

### 当前数据类型与 padding 语义

- 当前 Gemmini 基本配置不是“8-bit input / 16-bit output”。
- 当前静态代码显示：
  - `inputType = SInt(8.W)`
  - `accType = SInt(32.W)`
  - `spatialArrayOutputType = SInt(20.W)`
- 当前卷积越界语义是 **zero padding**，不是 edge clamp。

### 当前同步语义

- 当前 runtime 在 `spm_xlate_enable=1` 的 scene 下，会把 `sync_mode` 强制收敛到 `blocking_debug`。
- 结果是当前主线 scene 会进一步强制：
  - `dma_backend = PRT_DMA_BACKEND_BLOCKING_FENCE`
  - `gemmini_mode = PRT_GEMMINI_MODE_BLOCKING_FENCE`
- 因而当前 `bertmini` 主线不要再按“异步 overlap 已完全打开”的前提去推理。

### 当前 `num_cores` 语义债

- 当前 runtime 里的 `num_cores` 不是“纯 CPU/hart 数”的干净语义。
- 现状是它被同时拿去表示：
  - CPU/hart 相关上界
  - accelerator slot / local acc domain 数
  - page allocator / `pages_per_acc` 的分配域数
- 这也是当前代码里会强制
  `num_cores >= num_gemmini_mgrs`
  的直接原因；这条绑定来自实现残留，不是冻结的架构要求。
- 当前静态证据包括：
  - runtime init 会直接把 `num_cores` 拉高到不少于 `num_gemmini_mgrs`
  - fallback manager 选择在未显式绑定时会走 `stage_idx % num_cores`
  - page allocator、SPM PT pool、page leak 检查都按 `num_cores * pages_per_acc` 建模
- 因而后续不要把这条绑定解释成：
  “CPU 数必须和 Gemmini 数绑定”。
- 更准确的解释是：
  当前实现还没有把
  `num_cpu_harts`
  、
  `num_acc_slots/page_domains`
  、
  `num_gemmini_mgrs/num_dma_mgrs`
  彻底拆开。
- 这和更高层目标并不矛盾：
  当前代码本身已经独立保留了
  `PRT_MAX_CORES=64`
  、
  `PRT_MAX_ACTIONS=6`
  、
  `PRT_MAX_STAGES=128`
  这些上限；后续如果恢复架构性重构，正确方向应是拆语义，而不是继续拿 `num_cores` 代表一切。

### 当前日志策略

- 当前主线结论建立在“bin/runtime 日志走 guest 文件，而不是 UART”这个前提上。
- 当前冻结结果的 status 已明确是：
  - `uart_log_enable=0`
  - `guest_log_enable=1`
  - `guest_deep_log_enable=0`
- 当前粗粒度日志文件：
  `/root/pipeline-runtime-debug/bertmini-batch8.log`
- 当前细粒度日志文件：
  `/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
- 当前细日志支持按 `segment/global_stage/local_stage/subbatch` gating；后续如果只想看某个卡点附近，优先用 gating 缩范围，而不是重新把 bin 日志打回 UART。

## 2026-04-11 当前 live freeze 结论

- `2026-04-11 16:11 UTC` 从正在运行的 guest image 里直接抽取到：
  - `bertmini-batch8.status` 仍是 `state=running`
  - `guest_log_enable=1`
  - `guest_deep_log_enable=0`
  - `periodic_sync_enable=1`
- 同一份 image 内的
  `/root/pipeline-runtime-debug/bertmini-batch8.log`
  最后稳定停在：
  - `tok=2145`
  - `rr-acquire-inner phase=after-csr-write ...`
  - 然后只剩半截：
    `rr-acquir`
- 这说明：
  - 旧 blocker
    `after-rr-marker -> rr-acquire-begin`
    已经越过
  - 当前 freeze 不是 deep log 路径，因为这轮 `guest_deep_log_enable=0`
  - 当前更合理的根因是 sparse guest 文件日志发生 partial write 后，
    旧实现继续补写剩余字节，结果阻塞在热路径里
- 当前已做的根因修补：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_progress.h`
  - 函数：
    `prt_write_fd_all_impl()`
  - 行为从“循环直到整条日志全部写完”改成：
    “best-effort 单次写；只要内核已接受任意字节就立即返回；仅对 `EINTR/EAGAIN/EWOULDBLOCK` 做最多 4 次轻量重试”
- 当前已完成本地 RISC-V Linux 重编验证：
  - binary：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
  - mtime：
    `2026-04-11 16:11:14 +0000`
  - sha256：
    `23ce7ae9d6b595c61508b3716c64a2999ad897bb719fb58ca47b1379fc0f599f`
- 当前 stuck 实例：
  - `i-087cedd854b7d9e37`
  - private ip：
    `192.168.1.250`
  - 已于 `2026-04-11 16:13 UTC` 执行
    `terminaterunfarm --forceterminate`
  - AWS 状态已进入：
    `shutting-down`

## 当前 golden mismatch 为什么先搁置

当前 mismatch 是真实现象，但它不是“已经证明 RTL/硬件错误”的充分证据，原因至少有三层：

- 当前 runtime artifact manifest 仍是 `mode: fresh`，而 fresh 导出链只明确重新生成了 `runtime_model.bin` 和 `runtime_input.bin` 的 dummy 数据路径。
- 当前 `golden.*.bin` 仍是 host closure 里的 CPU backend 生成物，而不是来自独立硬件真值源。
- 当前 runtime 里仍有调试期硬编码语义：
  - conv activation 临时统一按 `RELU`
  - conv output scale 临时固定为 `1.0`
  - resadd 的 `A/B/C_scale` 固定为 `1.0`
  - resadd `relu=0`

所以当前更准确的说法是：

- 已经证明 FPGA backend 与当前 CPU reference 在这批 synthetic bertmini-shape artifacts 上存在分歧。
- 但还没有证明分歧一定来自 Gemmini RTL、CoupledDMA RTL、或当前硬件配置。

当前冻结策略：

- **golden mismatch 暂不继续深挖。**
- 只有当主线执行完成能力再次稳定复现后，才按“重新生成 golden -> 重新 build/install image -> fresh rerun”的顺序恢复这条调查。

## 保留下来的旧 blocker 证据

- `2026-04-01` 的旧 mainline hang 证据依然保留为“历史 regression 签名”，但不再是当前状态本身。
- 最有代表性的旧 capture 仍是：
  `/home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/bertmini-b8-fileonly-sync6-run-manualmon3-20260401-192.168.1.44-host-watchdog-20260401T152744Z.guest-deep-log.txt`
- 它冻结的旧边界是：
  `segment=3 stage=0 tensor=6 page=124 ... submit-begin -> [prt-marker] dma`
- 这条旧证据现在只用于：
  如果未来 fresh run 再次回退成 hang，可用来判断是否退回了旧 submit-window regression。

## 如果未来恢复 mismatch 调查

按下面顺序恢复，不要跳步：

1. 先修掉 host `pipeline_runtime` 全量构建里的现存告警/`-Werror` 阻塞。
2. 重新跑 host closure，刷新 `golden.*.bin`。
3. 重新 `marshal build` / `marshal install`。
4. 在跑 `infrasetup` 或 `runworkload` 前，重新核对 image/rootfs freshness。
5. 再做新的 FireSim run。
6. 每轮 run 结束后，立刻回收 run farm。

## 2026-04-11 17:28 UTC 最新续跑状态

- 本轮使用同一套 `12pair sbus128` 配置重新 fresh 跑到 `run15`：
  - runtime config：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
  - hwdb：
    `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
  - workload json：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
- 本轮已严格执行 freshness：
  - 本地 FireMarshal image freshness：PASS
  - remote image freshness：PASS
  - run host 私网 IP：
    `192.168.1.7`
- 在 `run15` 的 live guest image 中，新的 decisive 证据是：
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
  - `bertmini-batch8.runner-proc.stage` 只抓到：
    - `label=script-entry`
    - `cmdline=/bin/sh /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh --batch 8`
    - `wchan=do_wait`
  - `bertmini-batch8.log` 只有 firemarshal wrapper 自身前导行，没有任何 `[bertmini]` 或 `[prt-early]`
- 这说明当前 live blocker 不再是之前的 `rr-acquire` 热路径，而是更早：
  - wrapper 已经成功启动 runner shell
  - runner shell 在进入 runtime binary 前就卡住
  - 现象上卡点位于 `source /firemarshal.env` 附近，或至少在 runner 自己真正进入主体逻辑之前
- 基于这轮证据，已做一轮最小修正：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
    - 新增 `PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE`
    - 当 wrapper 已继承环境时，runner 跳过二次 `source /firemarshal.env`
    - `RUNNER_STAGE_SYNC_ENABLE` 默认从 `1` 改为 `0`
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
    - wrapper 在 spawn child 时显式传入：
      - `PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE=1`
      - `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=0`
- 这轮 live run 已于修补后主动回收：
  - 实例：
    `i-0fec203829a13527f`
  - AWS 状态在回收时为：
    `shutting-down`

## 2026-04-12 04:05 UTC run17 最新状态

- 当前活跃 run：
  - session：
    `pairbert-b8-d12s128-run17`
  - instance：
    `i-099d87c4249348d0f`
  - private ip：
    `192.168.1.78`
- 这轮再次确认：
  - guest image 内 `bertmini-batch8.status` 仍为 `state=running`
  - `runner-early.stage` 已到：
    - `skip-guest-env inherited path=/firemarshal.env`
    - `after-bin-spawn method=ours2 pid=191`
  - 说明旧的 runner/pre-main 卡点已经越过
- 直接对 live image 做 `debugfs cat` 后，当前最新 runtime 前沿是：
  - `stage=0 subbatch=5`
  - 已完成 `mgr=5/6/7` 的 pointwise `fence-end`
  - 已进入 `worker stage=0 subbatch=5 export-sync-enter`
  - 当前冻结在：
    `tensor=2` 导出到 `target=address2` 的 export DMA 路径
  - 更窄的最后 marker 是：
    `tok=2281` 的 `dma-submit ... phase=rr-acquire-end`
  - 下一条预期应当出现但没有出现的是：
    `phase=doneflag-begin`
- 这意味着当前不是卡在 `rr-acquire` 本身，而是卡在：
  - `rr-acquire-end`
  - 到
  - `doneflag-begin`
  之间的极短软件路径
- 二次采样确认当前 run 处于“仿真继续前进，但 runtime 日志已冻结”的状态：
  - `heartbeat.csv` 从 `1031` 继续增长到 `1087`
  - 但 `bertmini-batch8.status` / `bertmini-batch8.log` / `bertmini-batch8.deep.log` 字节数在 25 秒窗口内完全不变
- 本轮同时定位到 host monitor 两个缺陷：
  - `firesim-prt-host-watchdog.sh` 默认 `remote_img_glob` 只匹配旧镜像命名，抓不到当前
    `...batch8-fileonly-sync-pairdummy.img`
  - 默认 `arm_on_guest_status_nonzero=0`，而 file-only 模式又不依赖 UART arm marker，导致 monitor 不会进入 progress 追踪
- 已完成的本地修补：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    - 新增 `rr-acquire-end -> doneflag-begin` 之间的更细 marker：
      - `phase=rr-acquire-postcheck`
      - `phase=rr-state-install-end`
      - `phase=doneflag-clear`
  - 文件：
    `/home/ubuntu/chipyard/scripts/firesim-prt-host-watchdog.sh`
    - 默认 `arm_on_guest_status_nonzero` 改为 `1`
    - 默认 `remote_img_glob` 放宽为：
      `/home/ubuntu/sim_slot_0/*rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8*.img`
- 下一轮不要重复旧误判：
  - 不能再说“当前卡在 RR debug CSR read”或“当前一定已经越过旧 frontier”
  - 这轮后续已被 live image 文件日志纠正：
    `tok=2281`
    不是
    `tensor=2 export-sync`
    而是
    `segment=0 / stage=0 / tensor=0 / C1 entry DMA`
  - 更准确的冻结窗口应写成：
    `stage=0 / tensor=0 / tok=2281 / rr-acquire-end -> <after-rr-postcheck 之后的新稀疏日志>`

## 2026-04-12 04:30 UTC DMA completion 语义修正与本地重编状态

- 当前新的硬约束再补一条：
  - 任何后续编译动作之前，都必须先：
    - `cd /home/ubuntu/chipyard/sims/firesim`
    - `source sourceme-manager.sh --skip-ssh-setup`
  - 这条约束已经现场执行确认；
    manager 环境会把交叉编译器放进 `PATH`：
    `/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc`
- 这轮不是只加 marker，而是已经对 `pipeline-runtime` 的 DMA completion 主路径做了静态修正：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_types.h`
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_runtime.h`
- 当前已落地的修正点：
  - 不再把 `&tok->hw_done_flag` 当成每次 submit 时临时翻译的 completion 地址
  - 改成 runtime 级预分配 completion flag pool：
    - 启动时一次性分配
    - 先 prefault
    - 再 `mlock`
    - 再一次性做 `virt_to_phys`
    - submit 时只分配 slot，不再做 token 栈地址翻译
  - `blocking_fence` backend 的 wait 路径不再走 `doneflag` 轮询判断完成
  - 当前 blocking wait 改为：
    - `hw_dma_fence()`
    - 之后 `rr_fence_scope()`
  - stage-local bounce page 也补了：
    - prefault
    - `mlock`
  - 因而这轮修正保持了用户强调的“物理页锁定”要求，而不是去掉锁页
- 本地二进制已经按当前 `pairdummy fileonly-sync` 宏组合重新强制编过一遍：
  - `PIPELINE_RUNTIME_PROGRESS=1`
  - `PIPELINE_RUNTIME_PROGRESS_RAW=0`
  - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
  - `PIPELINE_RUNTIME_ONLY_MARKER=1`
  - `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
  - binary：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
  - SHA256：
    `8bfa0d4bc739e41484cb9ad3b74f1324ef32717d28401d7fe844ae005dd5500e`
- 已确认新 binary 含有这轮新字符串/marker：
  - `dma-completion-pool`
  - `dma-doneflag-slot begin/end`
  - `checkpoint=wait-fence-done`
  - `phase=rr-acquire-postcheck`
  - `phase=rr-state-install-end`
  - `phase=doneflag-clear`
  - `phase=doneflag-begin`
  - `phase=doneflag-end`
  - `dma-backend using blocking_fence completion via hw_dma_fence + rr_fence_scope`
- 当前还没做的事要写清楚：
  - 这轮只完成了代码修正与本地重编
  - 还没有：
    - `marshal build`
    - `marshal install`
    - remote image freshness check
    - `firesim infrasetup`
    - fresh `runworkload`
- 因此下一轮 live 验证前，必须按正规链路继续：
  1. 用新 binary 重新 build / install image
  2. 用私网 IP 做 remote freshness check
  3. 重新 `infrasetup`
  4. 再 fresh `runworkload`
  5. 第一优先级看 `tok=2281` 附近是否越过旧窗口，并打印出：
     - `rr-acquire-postcheck`
     - `rr-state-install-end`
     - `doneflag-clear`
     - `doneflag-begin`
     - `dma-doneflag-slot end`
     - `wait-fence-done`

## 2026-04-12 04:43 UTC FireSim fresh rerun 已启动，但仍在 Linux 启动期

- 新增一条执行约束，后续不要再违反：
  - 任何后续编译动作之前，除了进入正确目录外，也必须先：
    - `cd /home/ubuntu/chipyard/sims/firesim`
    - `source sourceme-manager.sh --skip-ssh-setup`
- 这轮 fresh FireSim 流程已经继续到 live run：
  - `infrasetup` 成功
  - remote image freshness check 成功
  - fresh `runworkload` 已启动
- 本轮确认的实例与私网地址：
  - instance id：
    `i-03e660582d21388b0`
  - private ip：
    `192.168.1.55`
- `infrasetup` 成功的关键证据：
  - AGFI：
    `agfi-0dc8dcfa4c7735f40`
    已经 `loaded`
  - FireSim driver readiness preflight 已通过
  - 远端已经启动：
    - `hw_server`
    - `virtual_jtag`
- remote image freshness check 已通过：
  - local / remote image SHA256 一致：
    `b56562fbaa6faa22cb659c7c6bb7ad4bbfe1596fd5100e5f0e86806d3da2f845`
  - guest image 里的关键 env 已确认：
    - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- `runworkload` 当前 session：
  - tmux：
    `pairbert-b8-d12s128-run19`
  - pane log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run19.pane.log`
  - host monitor log：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run19.monitor.log`
  - manager run log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-12--04-39-37-runworkload-AL2R74LS81FS0T9B.log`
- 注意有一个无效尝试：
  - `pairbert-b8-d12s128-run18`
  - 这是因为我当时把启动环境裁得过头，导致 wrapper 在 `source sourceme-manager.sh` 前丢了 conda
  - pane log 只有：
    `::ERROR:: you must have conda in your environment first`
  - 这不是 guest / hardware / runtime 问题
- 截至当前 live 观察，不能说已经回到旧 DMA blocker，也不能说已经越过旧 DMA blocker：
  - 当前 guest 还在 Linux 启动期
  - `heartbeat.csv` 已持续增长到：
    `3088332452, 178`
  - `uartlog` 目前还停留在 Linux 早期启动输出
  - image 内文件日志仍为空：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper-proc.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.wrapper.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner-early.stage`
    - `/root/pipeline-runtime-debug/bertmini-batch8.runner-proc.stage`
- 因此这轮 live run 的当前结论必须写精确：
  - FPGA 仿真在前进，当前没有新的死锁信号
  - 但 guest 还没有进入 firemarshal 用户态/runner 阶段
  - 所以还没有到能够判断 `tok=2281` 新 marker 是否越过的阶段

## 2026-04-12 04:49 UTC run19 已进入 runtime 主体，当前 frontier 还没到旧 tok=2281

- 这轮 live run 现在已经越过：
  - Linux 启动期
  - firemarshal wrapper / runner 启动期
  - pipeline yaml 加载与校验
  - layer mapping 大文件读取阶段
- 当前 session 仍是：
  - `pairbert-b8-d12s128-run19`
- host watchdog 已确认：
  - `guest-status arm observed at 2026-04-12T04:46:09Z hb='6814151757, 378'`
  - 之后多次看到 `guest_sparse` 文件继续增长：
    - `938`
    - `199091`
    - `253952`
    - `797010`
- 当前 image 内已经能稳定看到这些关键新证据：
  - `dma-completion-pool before-prefault`
  - `dma-completion-pool after-prefault`
  - `dma-completion-pool before-mlock`
  - `dma-completion-pool after-mlock rc=0 errno=0`
  - `dma-completion-pool ready slots=1024 bytes=4096 ...`
  - 说明这轮 completion pool 修正确实已经跑到了 guest 上，不是只停留在本地 binary
- 当前 runtime 已经完成：
  - `init load-pipeline-yaml end`
  - `init validate-artifacts begin`
  - `artifacts mapping load end ... bytes=12029923 elapsed_ms=530`
- 当前 runner 侧确认：
  - `after-runner-enter`
  - `after-resolve-manager-layout gemmini=12 dma=12 pair=1`
  - `before-prepare-hugetlb`
  - `after-prepare-hugetlb`
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=192`
- 当前 binary 进程信息已在 guest 文件侧看到：
  - pid：
    `192`
  - cmdline：
    `rerocc_pipeline_runtime-linux --backend fpga ... --skip-model-bin-load --skip-input-load --skip-golden-check`
- 当前 sparse log 最新已进入真正的 DMA submit 路径：
  - 可以看到连续的：
    - `dma-submit-inner stage=0 tensor=2 ...`
    - `rr-acquire-inner phase=after-csr-read ... acquired=1`
    - `rr-acquire-inner phase=before-set-opc`
    - `rr-acquire-inner phase=after-set-opc`
  - 当前已观测到的 token 大约推进到：
    - `tok=527`
- 这条结论要写清楚，避免误报：
  - 当前已经不再卡在“更早的 yaml / artifact load begin”
  - 当前也还没有到旧 live blocker
    `tok=2281`
  - 现在只是刚进入真正 relevant 的 DMA 执行区间，仍需继续盯到 `tok=2281` 附近

## 2026-04-12 04:56 UTC run19 已稳定冻结在 tok=2281，新增编译前 source 约束

- 新增硬约束，后续任何编译动作之前都必须先执行：
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `source sourceme-manager.sh --skip-ssh-setup`
- 已有硬约束继续有效：
  - 所有 SSH / remote inspection / remote freshness check 只允许使用私网 IP，不允许使用公网 IP
- 当前 `pairbert-b8-d12s128-run19` 的 live run 已不再前进：
  - host watchdog 最新已记录到：
    - `hb='17943834272, 948' idle=325s`
  - `guest_sparse` 在增长到
    `2465092`
    之后不再变化
  - FireSim manager 仍显示：
    - 实例仍在运行
    - simulation 仍在运行
- 因此这轮 run19 的当前结论已从“继续推进中”收敛为：
  - guest / host 都还活着
  - 但 runtime 文件日志已经稳定停在同一个 frontier
  - 当前 freeze frontier 仍是：
    `tok=2281`
- `tok=2281` 的最后可见上下文保持不变：
  - `stage=0`
  - `tensor=0`
  - `kind=3`
  - 末尾最后几行仍是：
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
- 重要静态事实仍需保留：
  - 代码里 `after-set-opc` 之后本应立即经过：
    - `rr-acquire-postcheck`
    - `rr-state-install-end`
    - `doneflag-clear`
    - `doneflag-begin`
    - `doneflag-end`
    - `program-*`
    - `wait-fence-done`
  - 对应代码位置在：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    的 `1473-1655` 附近
- 当前还有一个独立观测面缺陷，不能误当成根因，但必须记住：
  - 当前 build 开了：
    - `PIPELINE_RUNTIME_ONLY_MARKER=1`
    - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
  - 当前 guest env 仍是：
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
  - 由于 `PRT_MARKER_LOG` / `PRT_PROGRESS_HOT_LOG` 在这个组合下会落到 deep log 路径，
    所以它们可能被 deep log 关闭一起吞掉
  - 结果是现在 sparse log 只能稳定看到 `PRT_PROGRESS_LOG`，看不到 marker / hot wait log
- 下一步应优先做两件事：
  1. 静态核查 `tok=2281` 这条 `stage=0 tensor=0 kind=3` 的真实语义与代码分支
  2. 修复或绕过 marker / hot log 观测面缺陷，再按正规 FireSim 链路重跑

## 2026-04-12 05:16 UTC run20 已完成 fresh image 安装并进入 infrasetup，旧 `tok=2281` 语义误判已纠正

- 当前 fresh run farm：
  - 实例：
    `i-01d949923bf32b492`
  - 私网：
    `192.168.1.239`
  - `launchrunfarm` tmux：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-launch20.pane.log`
  - `infrasetup` tmux：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-infra20.pane.log`
  - `infrasetup` manager log：
    `/home/ubuntu/chipyard/sims/firesim/deploy/logs/2026-04-12--05-10-56-infrasetup-FII0L1VHPS8EGOQ8.log`
- 这轮之前已经完成：
  - FireMarshal `build`
  - FireMarshal `install`
  - local / remote image freshness check
  - image 内 runtime binary sha256 校验
- 当前 `infrasetup` 不是死锁：
  - tmux pane 表面仍停在
    `Checking if host instance is up...`
  - 但 manager log 已继续到远端安装 AWS FPGA SDK
  - 手工私网 SSH 也已确认实例可达
- 这轮必须保留一个新的纠正文案，避免后面再次误判：
  - run19 live image 文件日志已经证明：
    `worker stage=0 subbatch=6 done`
    之后立刻进入
    `stage=0 tensor=0 tok=2248..2281`
  - `PRT_PAGE_SIZE_BYTES=1024`
    且这些 token 都是 `bytes=1024`
  - 结合 segment0 mapping：
    `entryTensorIdList: [0]`
    `entryTensorTypeList: [DRAM]`
  - 所以
    `tok=2281`
    的真实语义是：
    `segment=0 / stage=0 / tensor=0 / C1 entry DMA`
  - 不是旧文档里写的
    `tensor=2 export-sync`
- 当前继续执行顺序：
  1. 等 `infrasetup` 自然完成
  2. 只用私网 `192.168.1.239` 做 remote image freshness check
  3. 用 `firesim-tmux-run.sh` 启动 `runworkload`
  4. 优先看 guest 文件日志中新补的 `after-rr-postcheck` / `after-program-*` / `dma-wait-inner` 稀疏日志，重新定位 freeze 点

## 2026-04-12 05:28 UTC run20 已明确越过旧 tok=2281 DMA blocker，DMA completion 主路径修正已在 live 中生效

- 当前 live：
  - session：
    `pairbert-b8-d12s128-run20`
  - 实例：
    `i-01d949923bf32b492`
  - 私网：
    `192.168.1.239`
  - run pane：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run20.pane.log`
  - host watchdog：
    `/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairbert-b8-d12s128-run20.monitor.log`
- 这轮在起跑前已再次完成：
  - `infrasetup`
  - remote image freshness check
  - runworkload 正规链路启动
- 这轮最关键的新证据：
  - live image 文件日志已经推进到：
    `stage=0 / tensor=0 / tok=2596`
  - 因而已经**明确越过旧 run19 的 `tok=2281` freeze frontier**
- 而且这不是“前沿漂移”或“换了一条语义路径”，而是旧 blocker 对应的那组新增稀疏日志已经真实出现：
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
- 对应 live 片段已经确认：
  - `tok=2593`
    到
    `tok=2596`
    的 `stage=0 tensor=0` 小页 DMA
    全部走完了：
    `rr-acquire -> postcheck -> state-install -> doneflag-acquire -> program -> fence-wait -> release`
  - `dma-wait-inner phase=after-fence ... status=0`
    持续出现
- 因而当前可以把“旧 `tok=2281` 卡在 `after-set-opc` 之后”的判断升级为：
  - **这轮落地的 DMA completion 主路径修正确实已经在 `sbus128` live hardware 上生效**
  - **旧 blocker 已被真实修掉，不是单纯跳过或侥幸绕过**
- 当前新的 live frontier已经更靠后：
  - 先经历了长时间的 layer-mapping 大文件读取与解析
  - 然后回到 `segment=0 / stage=0 / tensor=0` 的 page DMA 主线
  - 当前仍在继续推进，尚未收敛出新的稳定 freeze
- 下一步：
  1. 继续只用私网和 guest 文件日志监控 run20
  2. 判断它是自然跑通，还是在更靠后的新 frontier 上稳定冻结
  3. 若出现新 freeze，再按新 frontier 做静态和 live 对照

## 2026-04-12 07:35 UTC run23 新前沿进一步收敛：当前 live 的 `c7-entry-allring` 等待更像是 producer 侧 `ALL_RINGBUFFER` publish 前串行 alias sync

- 当前 live：
  - run：
    `pairbert-b8-d12s128-run23`
  - instance id：
    `i-040bca6fca765b51b`
  - 私网：
    `192.168.1.175`
  - 只允许继续使用这个私网地址做 SSH / remote inspect / remote freshness check
- 当前 guest 文件已经确认：
  - wrapper/status 仍是 `running`
  - runner 已经进入：
    `before-bin method=ours2`
    `after-bin-spawn method=ours2 pid=192`
  - 所以当前 live 跑的确实是
    `pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
    而不是 `gemini2`
- `ours2` 的实际 segment 语义已经和 live 对上：
  - `segment=1`
    有 6 个 stage
  - `tensor=3`
    是：
    `stage0 export = ALL_RINGBUFFER`
    `stage2 entry = ALL_RINGBUFFER`
  - `tensor=8`
    是：
    `stage4 export = ALL_RINGBUFFER`
    `stage5 entry = ALL_RINGBUFFER`
  - 当前主日志里的
    `worker stage=2 subbatch=0 waiting phase=entry-c7-ring-ready kind=c7-entry-allring tensor=3 ... ring=0/0/2`
    与这份 `ours2` mapping 完全一致
- 一个容易误判的点已经静态排除：
  - `C8` 的首个 subbatch 不是“丢首包”
  - 真正执行顺序是：
    1. `stage_wait_exports_ready()` 在 compute 前先对 `C8` 调一次 `prt_process_c8()`
    2. 这一步会把 export pipebuf 先 prime 到 ring slot
    3. compute 直接把结果写到 ring slot 页
    4. compute 后再次 `prt_process_c8()` 才真正 `ring_fill_locked()`
  - 所以当前 `stage2` 等 `tensor=3` 本身并不说明 ring 实现一定坏了
- 但新的静态嫌疑点已经更明确：
  - `stage0` 的 `tensor=3` 是内部 `ALL_RINGBUFFER` export
  - 它在真正 `C8 publish` 之前，仍然会先走：
    `sync_stage_export_aliases()`
  - 这条路径会通过
    `stage_tensor_current_pages()`
    取到当前 export pipebuf 页；对 `ALL_RINGBUFFER` 来说，这时页已经是 ring slot 页
  - 然后它会调用：
    `copy_tensor_pages_to_model_aliases()`
    把这个内部 transport tensor 再 DMA materialize 回 model alias
  - 因而当前 `stage2` 的 `c7-entry-allring` 等待，很可能不是 producer 没有 ring 语义，而是 producer 还被串行卡在 publish 之前的 alias sync
- 当前 live 观测面也需要记住两个限制：
  - `heartbeat.csv` 持续增长，只能说明仿真没死；**不能单独证明 guest 软件前进**
  - 当前 `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=0`
    所以 `runner.stage` / `runner-proc.stage` 不是每次 poll 都会即时刷盘，不能把“文件没更新”直接等价成 shell 没轮询
- 当前 guest status 已再次确认关键环境：
  - `periodic_sync_enable=1`
  - `periodic_sync_seconds=1`
  - `stdio_capture_mode=log`
  - `checkpoint_log_enable=1`
  - `uart_log_enable=0`
  - `mlockall_mode=2`
- 因而当前最合理的下一步不是先改 ring wait，而是：
  1. 继续监控 `run23` 是否自然越过 `tensor=3` 的 `c7` 等待
  2. 若稳定不前，再把断点加到
     `sync_stage_export_aliases(stage0, tensor=3)`
     和
     `copy_tensor_pages_to_model_aliases(tensor=3)`
  3. 后续若重跑，优先把 `runner-stage sync` 和 runtime 文件 flush 可见性补强，不要再只依赖当前这种“文件有 sync，但进程内部 stdout/stage 文件不一定及时 flush”的观测面

## 2026-04-12 07:28 UTC run23 已停止，当前进入“alias-sync 语义修正后重新 fresh image / rerun”阶段

- 这轮对 `run23` 的最终静态收敛结论继续保持：
  - 当前 live 前沿对应的是
    `ours2 / segment=1 / tensor=3`
    的内部
    `ALL_RINGBUFFER`
  - `stage2`
    的
    `c7-entry-allring`
    等待更像是在等 producer publish
  - 更可疑的不是 ring wait 自身，而是 producer 在 publish 前仍先串行跑了：
    - `sync_stage_export_aliases()`
    - `copy_tensor_pages_to_model_aliases()`
  - 对“当前 segment 内仍有 entry consumer 的内部 `ALL_RINGBUFFER` export tensor”，这条 alias materialization 本身就不应该发生
- 基于上面的静态结论，这轮已经在
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
  落下新的语义门控：
  - 当
    `export_buf->kind == PRT_BUF_C8_EXPORT_ALL_RING`
    且
    `has_entry_consumer_for_tensor(rt, tensor_id)`
    时，
    `sync_stage_export_aliases()`
    直接跳过 alias sync
  - 新增日志：
    `export-sync skip stage=%u tensor=%u reason=internal-all-ring-consumer`
- 这轮还额外补强了 guest 可观测性：
  - 在
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init-fileonly-sync-pairdummy.sh`
    中，
    `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE`
    默认从
    `0`
    改成了
    `1`
  - 目的不是改功能语义，而是让下一轮 live 的
    `runner.stage`
    /
    `runner-proc.stage`
    文件更及时刷盘，减少“guest 已推进但 stage 文件滞后”的观测噪声
- 当前编译约束要继续按最严格版本执行：
  - **任何编译动作之前**都必须先：
    - `cd /home/ubuntu/chipyard/sims/firesim`
    - `source sourceme-manager.sh --skip-ssh-setup`
  - 然后才允许回 repo 根目录继续：
    - `source env.sh`
    - workload/binary/image 相关编译或重建动作
- 这轮本地 runtime binary 已按上面约束重新成功编译：
  - 目标产物：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
  - 说明当前源码改动至少已经落到 host 侧可打包 binary，不是“只改源码还没进可执行文件”
- `run23` 旧实例也已经开始正规回收，不再继续监控旧 live：
  - 旧实例：
    `i-040bca6fca765b51b`
  - 私网：
    `192.168.1.175`
  - 规范终止会话：
    `pairbert-b8-d12s128-term23b`
  - `terminaterunfarm`
    已明确返回：
    `Instances terminated`
  - AWS 当前状态已进入：
    `shutting-down`
- 当前阶段的正确下一步已经切换为：
  1. 等旧实例彻底退出
  2. 重新对 pairdummy workload 执行
     `marshal build -> marshal install`
     并通过本地 image freshness check
  3. 继续只用私网做 remote image freshness check
  4. 然后重新走
     `launchrunfarm -> infrasetup -> runworkload`
  5. 下一轮重点验证：
     - `ours2 / segment=1 / tensor=3`
       是否越过旧
       `c7-entry-allring`
       frontier
     - 新加的
       `export-sync skip ... internal-all-ring-consumer`
       是否按预期出现
     - `runner.stage`
       可见性是否因为
       `PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE=1`
       明显改善

## 2026-04-12 07:49 UTC run24 已越过旧 `run23` 的 ring wait 前沿，但新的稳定冻结点收敛到 `segment=0 action-generate end` 之后

- 这轮 fresh rerun 已完成的正规链路：
  - 旧 `run23` 实例
    `i-040bca6fca765b51b`
    已正规终止
  - `marshal clean -> build -> install`
    已完成
  - 本地 image freshness：PASS
  - 新实例：
    `i-0adc648f1ec21fcfb`
  - 私网：
    `192.168.1.138`
  - `launchrunfarm`：PASS
  - `infrasetup`：PASS
  - remote image freshness：PASS
    - local / remote sha256 一致：
      `f5b18224ff2f3d9d4f4c80cfabc96e7f6a4723ac051fac244602c1d498ad8ef4`
  - `runworkload` 当前会话：
    `pairbert-b8-d12s128-run24`
- 这轮已经明确越过的旧前沿：
  - Linux boot
  - wrapper / runner early
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=206`
  - pipeline yaml 完整 parse / validate
  - layer mapping 大文件 open / pread / parse progress
  - synthetic model alloc / prefault / mlock
- 当前最新 live 文件证据：
  - `bertmini-batch8.status`
    仍是
    `state=running`
  - `bertmini-batch8.log`
    最后稳定停在：
    - `init ready segments=13 pipeline_subbatch_size=1 batch=8`
    - `segment=0 init begin stages=1 seg_subbatch_size=1 is_last=0`
    - `segment=0 action-generate begin`
    - `segment=0 action-generate end action=1`
  - 与此同时：
    - `heartbeat.csv`
      从
      `665`
      继续涨到
      `705`
    - `uartlog`
      字节数保持
      `15688`
    - `bertmini-batch8.log`
      的
      `size=305051`
      /
      `mtime=guest 6s`
      均保持不变
- 因而当前新的稳定冻结点可先收敛为：
  - **`segment=0 action-generate end action=1` 之后**
  - 更精确地说，是
    `prt_runtime.c`
    里：
    - `prt_action_generate()`
      已返回
    - 但后续
      `prt_action_alloc_acc()`
      /
      `prt_action_alloc_spm()`
      路径还没有任何新的 guest 文件日志落盘
- 当前静态缩点结果：
  - `prt_action_generate()`
    本身只是
    `calloc + prt_action_exec_ensure`
    ，不太像真正 blocker
  - `action-generate end`
    后面最先进入的是：
    `prt_action_alloc_acc()`
  - 这段按源码看仍是纯软件路径，理论上不应长时间冻结
  - 所以下一轮优先不是改语义，而是先把
    `alloc_acc`
    的调用前后和函数入口打透
- 为了做这轮缩点，已经新增了更细但仍局部的 instrumentation：
  - 在
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c`
    新增：
    - `segment=%u action-alloc-acc begin action=%u`
    - `segment=%u action-alloc-acc end action=%u unique_g=%u unique_d=%u`
  - 在
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c`
    新增：
    - `alloc-acc enter`
    - `alloc-acc stage-assign-ready`
  - 目的：
    区分是卡在
    `alloc_acc`
    调用前、
    `alloc_acc`
    函数入口、
    还是其内部首个分配之后
- 当前正确下一步：
  1. 终止 `run24`
  2. 按硬约束重新
     `marshal build -> install`
  3. 重新做 local / remote freshness 闭环
  4. 重新走
     `launchrunfarm -> infrasetup -> runworkload`
  5. 直接验证新日志是否推进到：
     - `action-alloc-acc begin`
     - `alloc-acc enter`
     - `alloc-acc stage-assign-ready`
     - `alloc-acc done`
     - `action-alloc-spm begin`

## 2026-04-12 08:47 UTC run25 已越过 `run24` 的 `action-generate` frontier，但新的稳定冻结点收敛到 `rr-acquire-inner ... after-set-opc`

- 当前 live run：
  - session：
    `pairbert-b8-d12s128-run25`
  - instance：
    `i-07cd6a9a82513be23`
  - private ip：
    `192.168.1.65`
  - remote image freshness：PASS
    - local / remote image sha256：
      `7cd382cac89f79f3591a9b1a37d348dd77fc086f0c25a8563d65559917d48eee`
- 这轮 host watchdog 已确认：
  - `guest-status arm observed at 2026-04-12T08:33:54Z hb='6937872059, 384'`
  - 后续 guest 稀疏日志继续增长到：
    - `938`
    - `1001`
    - `1899`
    - `93765`
    - `197618`
    - `213454`
    - `309459`
- 这轮已经明确越过：
  - Linux boot / wrapper / runner
  - `after-prepare-hugetlb`
  - `before-bin method=ours2`
  - `after-bin-spawn method=ours2 pid=208`
  - pipeline yaml parse / validate
  - layer-mapping 大文件 load / validate
  - synthetic model alloc / prefault / mlock
  - `segment=0 action-generate end action=1`
  - `segment=0 action-alloc-acc begin/end`
  - `segment=0 action-alloc-spm begin/end`
  - `segment=0 build-topology`
  - `segment=0 prepare-stage-spm`
  - `segment=0 bind-topology`
  - `segment=0 flush-spm-xlate`
  - `segment=0 worker-create`
- 当前最新稳定 live 文件证据：
  - `bertmini-batch8.log`
    末尾停在：
    - `rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-csr-write stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-csr-read stage=0 manager=0 opcode=2 cfg=0 ... acquired=1`
    - `rr-acquire-inner phase=before-set-opc stage=0 manager=0 opcode=2 cfg=0 ...`
    - `rr-acquire-inner phase=after-set-opc stage=0 manager=0 opcode=2 cfg=0`
  - 同时：
    - `heartbeat.csv`
      从
      `560`
      继续涨到
      `801`
    - `uartlog`
      字节数稳定在
      `15688`
    - `bertmini-batch8.log`
      的
      `size=309459`
      连续约
      `244s`
      不再增长
- 当前静态事实：
  - 在
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
    中，
    `prt_rr_acquire_scope(...)`
    返回后本应几乎立刻经过：
    - `dma-submit-inner ... phase=after-rr-postcheck`
    - `dma-submit-inner ... phase=after-rr-state-install`
    - `dma-submit-inner ... phase=after-doneflag-clear`
  - 因而当前新冻结点可先收敛为：
    **`prt_rr_acquire_scope()` 返回边界附近**
    更具体地说，是：
    - `rr-acquire-inner phase=after-set-opc`
      之后
    - `dma-submit-inner phase=after-rr-postcheck`
      之前
- 为缩点这条新前沿，已新增下一轮 instrumentation：
  - `prt_rerocc.c`
    新增：
    - `rr-acquire-inner phase=before-scope-valid`
    - `rr-acquire-inner phase=after-scope-valid`
    - `rr-acquire-inner phase=before-return`
  - `prt_dma.c`
    新增：
    - `dma-submit-inner ... phase=after-rr-acquire-call`
- 当前正确下一步：
  1. 终止 `run25`
  2. 按硬约束重新
     `marshal build -> install`
  3. 再做 local / remote freshness 闭环
  4. 重新走
     `launchrunfarm -> infrasetup -> runworkload`
  5. 重点验证新日志最远推进到：
     - `after-scope-valid`
     - `before-return`
     - `after-rr-acquire-call`
     - `after-rr-postcheck`

## 2026-04-12 09:08 UTC run26 已越过 `after-set-opc`，新的稳定冻结点进一步收敛到 `prt_rr_acquire_scope()` 真正返回边界

- 当前 live run：
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
  - `guest_sparse` 先后增长到：
    - `938`
    - `1504`
    - `2293`
    - `98304`
    - `197618`
    - `213453`
    - `309730`
  - 之后
    `heartbeat`
    继续从
    `560`
    增长到
    `624`
    ，但
    `guest_sparse`
    保持
    `309730`
    不再增长
- 本轮已经明确越过：
  - `run25` 的旧冻结点
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
    - `dma-submit-inner stage=0 tensor=2 phase=after-rr-acquire-call ...`
    - `dma-submit-inner stage=0 tensor=2 phase=after-rr-postcheck ...`
- 当前静态结论：
  - 新冻结区间已经从
    `after-set-opc -> after-rr-postcheck`
    再缩窄为：
    **`prt_rr_acquire_scope()` / `prt_rr_acquire_scope_cfg()` 的真实返回边界**
  - 更具体地说，是：
    - `rr-acquire-inner phase=before-return`
      之后
    - `dma-submit-inner phase=after-rr-acquire-call`
      之前
  - 因为
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c`
    里
    `before-return`
    后面只剩
    `return PRT_OK;`
    ，所以责任点优先怀疑：
    - `prt_rr_acquire_scope_cfg()` / `prt_rr_acquire_scope()` 的函数返回边界
    - 或 `rr_set_opc()` / CSR 包装对应的调用约定、副作用或寄存器破坏
- 当前正确下一步：
  1. 保留 `run26` 作为 live 证据，不急于终止
  2. 静态检查 `prt_rr_acquire_scope_cfg()`、`prt_rr_acquire_scope()`、`rr_set_opc()` 的源码与反汇编
  3. 若需要下一轮 live instrumentation，优先把 marker 打到：
     - `prt_rr_acquire_scope()` 包装函数入口/返回前
     - `prt_dma.c` 调用点的 `prt_rr_acquire_scope(...)` 前后 raw marker
     - 必要时直接在返回值保存到局部变量前后打 raw marker

## 2026-04-12 10:18 UTC run28 后续静态修复：已修 `dma_blocking_submit_and_wait()` token cleanup 泄漏，当前在等待 run29 的 AWS 容量

- 基于 `run28` 的 checkpoint 证据，freeze 已从：
  - `rr-acquire`
  - `program-post-src`
  - `wait-fence-done`
  进一步收敛到：
  - `doneflag-begin`
    之后
  - `doneflag-end`
    之前
- 与这一现象强一致的静态缺陷已确认位于：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  的
  `dma_blocking_submit_and_wait()`
  ：
  - 旧实现对栈上 `prt_dma_token_t tok` 只做
    `dma_blocking_submit()`
    和
    `dma_blocking_wait()`
  - 但没有执行
    `prt_dma_token_cleanup(&tok)`
  - 因而：
    - `dma_completion_flag_release(tok)` 不会发生
    - completion slot 会持续泄漏
    - 泄漏累积后极可能在后续某次
      `dma_completion_flag_acquire()`
      / `dma_debug_capture_done_flag()`
      处冻结
- 已做代码修复：
  - 在
    `dma_blocking_wait(...)`
    返回后，无论返回码成功或失败，都执行：
    `prt_dma_token_cleanup(&tok)`
  - 返回码继续原样向上传播
- 本轮 fresh rebuild/install 结果：
  - `marshal build` session：
    `pairdummy-prt-build36a`
  - `marshal install` session：
    `pairdummy-prt-install35`
  - local freshness：PASS
  - 新 runtime binary sha256：
    `4bd2d2c0e04c736052e97917f8ebe6ece5b1b28046cd340c056f5934cf048161`
  - `firemarshal.env` sha256：
    `2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
- 这轮还确认了一个命令约束坑，后续必须继续遵守：
  - `scripts/firemarshal-tmux-run.sh`
    在
    `/home/ubuntu/chipyard/software/firemarshal`
    下执行
  - `scripts/firesim-tmux-run.sh`
    在
    `/home/ubuntu/chipyard/sims/firesim/deploy`
    下执行
  - 因而 `build/install/launch/infrasetup/runworkload/terminaterunfarm`
    都优先传绝对路径，避免再次因为 cwd 差异触发
    `FileNotFoundError`
- 旧 live run 已处理：
  - `run28` instance：
    `i-067e360e239e22759`
  - private ip：
    `192.168.1.229`
  - 已执行：
    `terminaterunfarm --forceterminate`
  - AWS 状态已进入：
    `shutting-down`
- 当前最新 live 进展：
  - `launch29` session：
    `pairbert-b8-d12s128-launch29`
  - 当前阻塞不是软件 freeze，而是 AWS 返回：
    `insufficient capacity to launch your instances`
  - FireSim manager 正在按 4 个 subnet 持续 retry，暂未拿到新的
    `f2.6xlarge`
- 当前正确下一步：
  1. 等 `launch29` 成功拿到新实例
  2. 继续 `infrasetup29`
  3. 用私网 IP 做 remote freshness，确认远端 image sha256 与本地
     `4bd2d2c0e04c736052e97917f8ebe6ece5b1b28046cd340c056f5934cf048161`
     一致
  4. 重新 `run29`
  5. 重点验证是否越过 `run28` 的
     `doneflag-begin -> doneflag-end`
     冻结区间

## 2026-04-12 10:44 UTC run29 已穿过旧 doneflag blocker，新的冻结点收敛到更后的 `rr-acquire-begin -> rr-acquire-end`

- `run29` 基本信息：
  - instance：
    `i-0cf51fec4fa523dca`
  - private ip：
    `192.168.1.162`
  - remote freshness：PASS
  - remote image sha256：
    `54d04daa2be68d11e91eff9086371156a258170bbcf4232b14adc3cc1b684bbf`
- 本轮最重要结论：
  - 旧 blocker 已被真正穿过，不是“绕过报错”
  - 直接证据：
    - `guest_sparse` 从 `run28` 稳定卡住的 `402911`
      继续增长到
      `404707`
    - `checkpoint.log` 从 `0`
      增长到
      `1975927`
    - 末尾连续出现大量：
      - `doneflag-end`
      - `program-post-src`
      - `wait-fence-done`
  - 因而：
    `dma_blocking_submit_and_wait()` 的 token cleanup 修复是有效的，并且确实把旧 completion slot 泄漏卡点修掉了
- `run29` 新稳定冻结特征：
  - host heartbeat 从
    `11183439481, 609`
    继续增长到
    `20602259128, 1084`
  - 但 guest 文件稳定不再增长：
    - sparse log：
      `404707`
    - checkpoint log：
      `1975927`
  - 所以当前是新的更后 freeze，而不是旧 freeze 复现
- 当前最小新 frontier：
  - checkpoint 末尾稳定停在：
    `dma stage=0 checkpoint=rr-acquire-begin tensor=1000001 src=0x10475c800 dst=0x40205800 bytes=1024 dst_acc=0`
  - 之后没有：
    `rr-acquire-end`
- 当前静态理解：
  - 这批 DMA 位于 `tensor_id >= 1000000` 的 fixed-load 路径
  - 即使 checkpoint 名字仍叫 `rr-acquire-begin`，也还不能直接断言一定卡在真实的
    `prt_rr_acquire_scope()`
    调用里
  - 还需要进一步区分：
    1. 这次是 `reuse scope`
       还是 `real acquire`
    2. 若是 `real acquire`，是卡在：
       - `before-csr-write`
       - `after-csr-write`
       - `after-csr-read`
       - retry loop
       - `rr_set_opc()`
       - return 边界
- 针对这个新 frontier，已在当前工作区补充下一轮 instrumentation：
  - `prt_dma.c`
    新增 checkpoint：
    - `rr-scope-state`
    - `rr-acquire-call-begin`
    - `rr-acquire-call-end`
    - `rr-acquire-reuse`
  - `prt_rerocc.c`
    新增 checkpoint：
    - `before-csr-write`
    - `after-csr-write`
    - `after-csr-read`
    - `retry`
    - `before-set-opc`
    - `after-set-opc`
    - `before-scope-valid`
    - `after-scope-valid`
    - `before-return`
- 新 instrumentation 已完成 fresh rebuild/install：
  - build session：
    `pairdummy-prt-build37`
  - install session：
    `pairdummy-prt-install36`
  - local freshness：PASS
  - 新 runtime binary sha256：
    `19fe6ed9a9d65262da86e534ff8ef25eaa313c6bc9b6bd1ef626ba2eab370bde`
- `run29` 已终止：
  - `terminaterunfarm --forceterminate`
    已执行
  - AWS 状态进入：
    `shutting-down`
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

## 2026-04-12 11:02 UTC run30 已启动，当前仍在 guest 早期 boot，尚未进入上轮 freeze 前沿

- 当前 live 观测：
  - host watchdog 正在监控：
    `192.168.1.24`
  - heartbeat 已增长到：
    `2851323873, 164`
  - 远端 guest 文件日志当前仍未创建或仍为空：
    - `/root/pipeline-runtime-debug/bertmini-batch8.status`
    - `/root/pipeline-runtime-debug/bertmini-batch8.log`
    - `/root/pipeline-runtime-debug/bertmini-batch8.deep.log`
  - UART 仅作为早期 boot 辅助观测，当前已看到：
    - OpenSBI 启动
    - Linux kernel early boot 输出
- 为什么现在还不能判定异常：
  - `run29` 的 host watchdog 记录显示，guest-status arm 是在 heartbeat：
    `7086493407, 392`
    才首次出现
  - 也就是说，当前 `run30` 的 heartbeat 还明显早于上轮进入文件日志阶段的时间点
- 因而当前最合理动作：
  1. 继续监控 `run30`
  2. 等 guest 文件日志开始写入后，再判断是否重现
     `rr-acquire-begin -> rr-acquire-end`
     freeze
  3. 一旦进入新的 checkpoint frontier，继续按新增的
     `rr-scope-state / before-csr-write / after-csr-read / before-return`
     等子阶段收敛根因

## 2026-04-12 11:16 UTC run30 已复现 freeze，但 broad stage0 checkpoint 明显扰动了前沿；已把 checkpoint 粒度收窄到 `tensor_id >= 1000000`

- `run30` 关键结果：
  - instance：
    `i-00c8e36972f4291ee`
  - private ip：
    `192.168.1.24`
  - host watchdog 轨迹：
    - `guest_sparse`
      依次增长：
      `1504 -> 197618 -> 214485 -> 311620 -> 402910`
    - 到 heartbeat
      `11181643686, 609`
      后停止增长
    - heartbeat 继续涨到：
      `13745373055, 739`
  - 因而：
    `run30` 确认仍然存在新的更后 freeze
- 这轮比 `run29` 多得到的关键信息：
  - guest image 内
    `bertmini-batch8.checkpoint.log`
    已增长到：
    `2351104`
    字节
  - 末尾稳定显示：
    - `rr-scope-state tensor=0 have_scope=1 scope_valid=1 scope_cfg=0 rr_mgr=0 rr_opc=2`
  - 最后一行只剩半行：
    - `rr-acquire-reuse ten`
  - 当前最保守结论：
    - 这次观测到的前沿**不是**真实
      `prt_rr_acquire_scope()`
      调用内部卡死
    - 至少对这次 `run30` 捕获点来说，代码已经进入
      `have_scope=1`
      的 reuse 分支
- 但当前不能把 `run30` 的 `tensor=0` reuse 卡点直接当成最终根因：
  - `run29` 的原始 frontier 是：
    `tensor=1000001`
  - `run30` 由于对**所有 stage0 DMA**都开启 checkpoint，log 量明显增大，前沿被扰动到了更早位置
  - 所以更合理的判断是：
    - broad stage0 checkpoint 已经干扰 live 行为
    - 需要把 checkpoint 重新收窄回旧 frontier 附近
- 已做的最小修正：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  - 修改：
    - `checkpoint_submit`
      从
      `tok->stage_idx == 0U`
      收窄为
      `tok->stage_idx == 0U && tok->tensor_id >= 1000000U`
    - `checkpoint_wait`
      同样收窄为
      `tok->stage_idx == 0U && tok->tensor_id >= 1000000U`
  - 目的：
    - 保留对旧 frontier
      `tensor_id >= 1000000`
      fixed-load 路径的精细观测
    - 避免再次让全量 stage0 checkpoint 扰动行为
- 新 artifact 已 fresh build/install：
  - build session：
    `pairdummy-prt-build38`
  - install session：
    `pairdummy-prt-install37`
  - local freshness：PASS
  - 新 runtime binary sha256：
    `4ce1b924f13547db7b5d3fb22de19c3f23e2e2c38507a913cbe468993394eca5`
- `run30` 已终止：
  - `terminaterunfarm --forceterminate`
    已执行
  - AWS 状态已进入：
    `shutting-down`
- 当前 live 状态：
  - `launch31` session：
    `pairbert-b8-d12s128-launch31`
  - 当前 снова 遇到 AWS：
    `insufficient capacity to launch your instances`
  - FireSim manager 正在持续 retry

## 2026-04-12 12:xx UTC run31 已把真实 frontier 收敛回 `tensor=1000001`，当前仍卡在 `doneflag-begin` 之后；已继续收窄到 completion-slot acquire/release 内部

- `run31` 当前实例：
  - instance：
    `i-015953798c8a09223`
  - private ip：
    `192.168.2.51`
- host watchdog 关键轨迹：
  - `guest_sparse`
    依次增长到：
    `1504 -> 197619 -> 214744 -> 311621`
  - 到 heartbeat
    `10711921613, 585`
    之后不再增长
  - heartbeat 仍持续增长到至少：
    `15340408849, 818`
- 远端 image 内
  `bertmini-batch8.checkpoint.log`
  当前大小：
  `83819`
  字节
- checkpoint 末尾已经稳定回到原始 fixed-load 路径：
  - `rr-acquire-begin tensor=1000001 ...`
  - `rr-scope-state ... have_scope=1 scope_valid=1 scope_cfg=0 rr_mgr=0 rr_opc=2`
  - `rr-acquire-reuse tensor=1000001 ...`
  - `rr-acquire-end tensor=1000001 rc=0 ...`
  - `doneflag-begin tensor=1000001`
- 且当前最后一条稳定停在：
  - `doneflag-begin tensor=1000001`
- 因而当前最保守结论更新为：
  - broad stage0 checkpoint 扰动已经消除
  - 真实 frontier 已重新收敛到
    `tensor=1000001`
    fixed-load 路径
  - freeze 点位于：
    `rr-acquire-end`
    之后、
    `doneflag-end`
    之前
  - 也就是
    `dma_debug_capture_done_flag()`
    / `dma_completion_flag_acquire()`
    这一小段
- 新静态结论：
  - `PRT_PROGRESS_HOT_LOG`
    实际写入的是
    `deep.log`
  - 本 workload 当前：
    `guest_deep_log_enable=0`
  - 所以 `doneflag-begin` 之后新增的 hot log 不会成为真实 blocker
  - 于是焦点仍应落在 completion-slot acquire / release 与其互斥锁
- 已做的新最小改动：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c`
  - 新增 helper：
    `dma_should_checkpoint_doneflag_tok()`
  - 对
    `stage0 && tensor_id >= 1000000`
    仅在 completion-slot 路径补 checkpoint：
    - `doneflag-acquire-enter`
    - `doneflag-acquire-pool`
    - `doneflag-acquire-lock`
    - `doneflag-acquire-scan`
    - `doneflag-acquire-exit`
    - `doneflag-release-enter`
    - `doneflag-release-lock`
    - `doneflag-release-exit`
  - 同时对 completion lock 增加：
    - `pthread_mutex_trylock()` 先探测
    - 若 busy，再走 1 秒 `pthread_mutex_timedlock()`
  - 目的：
    - 明确到底是卡在 mutex 前、mutex 长时间 busy、扫描槽位、还是返回后
    - 避免再次出现“静默永远挂死但没有更细证据”的情况

## 2026-04-12 12:xx UTC run32 证明上一版 doneflag instrumentation 体量仍然过大，live frontier 被重新拉回到更早的 `rr-acquire-inner before-csr-write`

- `run32` 当前实例：
  - instance：
    `i-09e6930b736eba3f3`
  - private ip：
    `192.168.2.28`
- remote freshness：PASS
  - remote image sha256：
    `b4a803bac4724ce33a57e1628acf4631685c88c1cd649c22a7fb17cc0017c8f1`
  - local runtime binary sha256：
    `65b3d47e5d69f4153306015d785dfbcd4ffa1f8e4fa6b52e7697ae6fdbe248d4`
- host watchdog 轨迹：
  - `guest-status arm`：
    `hb='7087832023, 392'`
  - `guest_sparse`
    继续复现增长到：
    `1504 -> 198109 -> 215001 -> 309036`
  - 之后 heartbeat 继续涨到至少：
    `14397960670, 769`
    但 `guest_sparse` 不再增长
- 远端 image 内 guest 文件：
  - `bertmini-batch8.log`
    固定为：
    `309036`
    字节
  - `bertmini-batch8.checkpoint.log`
    仍是：
    `0`
    字节
- 当前主日志尾部稳定停在：
  - `rr-acquire-inner phase=before-csr-write stage=0 manager=0 opcode=2 cfg=0 csr=0x810 wdata=0x100`
- 这说明：
  - 这版新增的
    `pthread_mutex_timedlock + release checkpoint`
    虽然只在 doneflag 路径生效，但仍足以改变 binary layout / live 行为
  - 当前 run32 没有重新进入
    `tensor=1000001`
    fixed-load checkpoint 路径
  - 因而还不能用这轮结果判断 completion-slot 是否为根因
- 已据此继续收窄改动：
  - 删除 release 路径新增 checkpoint
  - 删除 `pthread_mutex_timedlock()`
  - 仅保留 acquire 路径最小观测：
    - `doneflag-acquire-enter`
    - `doneflag-acquire-pool`
    - `doneflag-acquire-lock`
    - `doneflag-acquire-scan`
    - `doneflag-acquire-exit`
- 目标：
    - 尽量把 live frontier 拉回 `run31`
      的原始 `tensor=1000001` 路径

## 2026-04-12 14:xx UTC run36 已稳定缩点到 `segment=0 action-alloc-spm begin`，并已补 alloc-spm / xlate / pt-pool 细探针

- 这一轮 live run：
  - instance：
    `i-0dc22546725ac2986`
  - private ip：
    `192.168.1.140`
  - remote freshness：PASS
  - guest 主观测面仍然是 image 文件系统里的：
    `/root/pipeline-runtime-debug/*`
  - 不是 `uartlog`
- `run36` 的实际推进明显越过了上一轮的 `artifacts mapping load`：
  - `bertmini-batch8.log` 已完整走过：
    - `artifacts validate end`
    - `synthetic-model alloc before/after prefault`
    - `synthetic-model alloc before/after mlock`
    - `init ready`
    - `segment=0 action-generate`
    - `segment=0 action-alloc-acc`
  - 最后一条稳定停在：
    `segment=0 action-alloc-spm begin action=1`
- 远端 guest 文件当时的关键状态：
  - `bertmini-batch8.log`：
    `305480`
    bytes
  - `bertmini-batch8.checkpoint.log`：
    `0`
    bytes
  - `bertmini-batch8.status` 明确显示：
    - `stdio_capture_mode=log`
    - `uart_log_enable=0`
    - `guest_log_enable=1`
    - `checkpoint_log_enable=1`
    - `mlockall_mode=2`
- host / guest 联合判断：
  - heartbeat 仍在继续推进
  - 但 guest sparse log 在
    `segment=0 action-alloc-spm begin action=1`
    后 45 秒内完全不再增长
  - 因而新的稳定前沿已经不是 yaml / mapping load，而是
    `prt_action_alloc_spm()`
- 已做的静态缩点：
  - 直接检查当前使用的
    `pipeline_mapping.rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128.ours2.yaml`
  - `segment 0` 只有：
    - `4` 个 `buffer binding`
    - `193` 个总页数
    - 绑定类型依次是：
      - `PIPE`
      - `PIPE`
      - `WEIGHT`
      - `WEIGHT`
  - 所以这不是一个“segment 太大导致 alloc-spm 合法但很慢”的解释，更像是卡在 `alloc-spm` 入口很靠前的某个子步骤
- 针对这一点，已新增细粒度 progress probe：
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_schedule_action.c`
  - 新增相位：
    - `alloc-spm enter`
    - `alloc-spm page-count`
    - `alloc-spm alias-window begin/end`
    - `alloc-spm xlate-ctx begin/end`
    - `alloc-spm binding-begin`
    - `alloc-spm ring-slot / weight / pipe-slot begin/end`
    - `alloc-spm binding-end`
  - 文件：
    `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_page_table.c`
  - 新增相位：
    - `spm-xlate-ctx alloc begin / slice end / end`
    - `spm-pt pool chunk-add begin / alloc-contig end`
    - `spm-pt alloc-contig begin`
    - `spm-pt hugetlb anon begin / mmap ok`
    - `spm-pt hugetlbfs begin / mmap ok`
    - `spm-pt anon begin / mmap ok`
    - `spm-pt probe begin / first-page begin / first-page end / fail / noncontig / end`
- 本地编译健全性检查：
  - 新探针源码已通过 RISC-V Linux 交叉编译
  - 手动编译时需要显式提供：
    - `CC_LINUX=$(which riscv64-unknown-linux-gnu-gcc)`
    - `abs_top_srcdir=/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests`
  - 但正式 binary 仍以 FireMarshal host-init 重编为准
- run36 已停止，避免继续浪费 F2 时间：
  - `firesim terminaterunfarm --forceterminate ...`
    已执行
  - 当时实例状态已进入：
    `shutting-down`
- 当前 fresh-image 闭环新一轮状态：
  - `marshal clean`：
    `pairdummy-prt-clean6`
    已完成
  - `marshal build`：
    `pairdummy-prt-build44`
    已完成
  - `marshal install`：
    `pairdummy-prt-install42`
    已完成
  - 新本地 runtime binary sha256：
    `d1ce235aafb2e2302af2f07cb6d2f24f0eb51ad904b69238a66668e3b828fc47`
  - 新 local / remote image sha256：
    `46600ead676100c343f9444b1f6f8a2a45bb1b82220e4528ec3d0706c2935390`
  - 这一轮 build 会显式沿用上一轮已知正确的编译环境变量：
    - `PIPELINE_RUNTIME_PROGRESS=1`
    - `PIPELINE_RUNTIME_PROGRESS_RAW=1`
    - `PIPELINE_RUNTIME_PROGRESS_HOT=1`
    - `PIPELINE_RUNTIME_ONLY_MARKER=0`
    - `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=log`
    - `DUMMY_GEMMINI_MODE=1`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=1`

## 2026-04-12 14:xx UTC 新 fresh FireSim 已启动，remote freshness 再次确认 PASS，当前还在 guest boot 早期

- 新实例：
  - instance：
    `i-06bd49efe6f4a7950`
  - private ip：
    `192.168.1.129`
- FireSim 管理动作：
  - `launchrunfarm`：
    `pairbert-b8-d12s128-launch37`
    成功
  - `infrasetup`：
    `pairbert-b8-d12s128-infrasetup37`
    成功
  - `runworkload`：
    `pairbert-b8-d12s128-run37`
    已启动
- remote freshness：
  - workload：
    `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy`
  - private ip：
    `192.168.1.129`
  - remote image sha256 =
    `46600ead676100c343f9444b1f6f8a2a45bb1b82220e4528ec3d0706c2935390`
  - runtime binary sha256 in image =
    `d1ce235aafb2e2302af2f07cb6d2f24f0eb51ad904b69238a66668e3b828fc47`
  - `firemarshal.env` 中仍确认：
    - `PIPELINE_RUNTIME_UART_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'`
    - `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='1'`
    - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='log'`
- `run37` 当前状态：
  - host watchdog 已挂上
  - heartbeat 正常增长
  - `uartlog` 已经进入 Linux boot
  - 但 image 内尚未出现
    `/root/pipeline-runtime-debug/bertmini-batch8.status`
    等 guest 文件
  - 说明当前还没到 runtime 主线，也还没到新的 alloc-spm probe
  - 下一次有效前沿应等待 guest 文件创建后再判断
## 2026-04-12 15:10 UTC update

- 用户确认的约束成立：`spm` 翻译页表如果要求“连续物理地址”，则不能把“普通匿名页 + 连续性探测”当作正式后备语义。
- 当前 `run38` 的现场也证明，这次并没有真的走到普通匿名页 fallback；实际卡点仍是：
  - `spm-pt alloc-contig hugetlb-check need_pages=1 total_pages=1 free_pages=1 can_try=1`
  - `spm-pt hugetlb anon begin ...`
  - 之后不再返回到 `mmap ok / failed`
- 因此当前修复方向已收紧为：
  - `spm` 翻译页表多页物理连续分配必须 strict hugepage
  - 优先使用 `hugetlbfs`，不再允许多页请求退回普通匿名页
  - bertmini runner 显式传 `--spm-pt-require-hugetlb 1`
- 代码已修改：
  - `pipeline-runtime/src/prt_page_table.c`
    - 对 `req_bytes > host_page_bytes` 的多页物理连续请求，强制 `require_hugetlb`
    - `runtime_pt_chunk_bytes()` 在 `spm_xlate_enable=1` 时固定采用单 hugepage chunk（硬件 PTE 上限下总能装入单个 2 MiB hugepage）
    - `alloc_contig_pt_storage()` 改为优先 `hugetlbfs`，并拒绝多页匿名页 fallback
  - `rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
    - 显式补 `--spm-pt-require-hugetlb 1`
  - `rerocc-linux-tests-coupleddma/workload/overlay/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
    - 显式补 `--spm-pt-require-hugetlb 1`
- 当前状态：
  - `run38` 已通过 `terminaterunfarm --forceterminate` 停止，实例 `i-089aa6c1a0ec02334` 进入 `shutting-down`
  - 正在执行新的 fresh-image 闭环：
    - `clean9`: PASS
    - `build47`: 进行中

## 2026-04-12 15:20 UTC update

- `build47`: PASS
- `install44`: PASS
- local freshness: PASS
  - runner-script sha256=`fdabde31d87372dfd82810ee79e0f9e8751e3af19d7b71400662ef1d491b25fa`
  - runtime-binary sha256=`849e8e60f766acc00076b6b7e6e824a5fae3e32ce43d4f67dfa4192d7c9bf62d`
  - firemarshal-env sha256=`2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
- 新 FireSim 闭环：
  - `launch39`: PASS
    - 新实例 `i-0d15f38a246cf50bb`
    - 私网 `192.168.1.193`
  - `infrasetup39`: PASS
    - 远端已加载 `agfi-0dc8dcfa4c7735f40`
    - FireSim driver readiness preflight passed
  - remote freshness: PASS
    - image sha256=`54f6be713bec84bd157a086feb6234e590c07fbe1b6b2903aafccf693428e39c`
    - runner sha256=`fdabde31d87372dfd82810ee79e0f9e8751e3af19d7b71400662ef1d491b25fa`
    - runtime sha256=`849e8e60f766acc00076b6b7e6e824a5fae3e32ce43d4f67dfa4192d7c9bf62d`
    - firemarshal env sha256=`2235210a97946dcd731292a8f07f87c6874b4487d5b626f74e7edd08b3b5ee8b`
  - `run39`: 进行中
- `run39` 当前现场：
  - host watchdog 已启动并绑定新实例 `192.168.1.193`
  - heartbeat 正常推进，但仍处于 guest boot 早期
  - 当前 heartbeat 约在 `189` 左右；参考此前同配置 run，通常到 `~392` 才进入 `S99run`
  - `uartlog` 文件大小目前仍停在 `7480` 字节，符合此前“UART 落盘易停滞”的已知现象，不能据此判断 guest 卡死
  - 目前应继续以 heartbeat 与后续 guest 文件日志为主，不应把当前阶段误判为新的 runtime 卡点
