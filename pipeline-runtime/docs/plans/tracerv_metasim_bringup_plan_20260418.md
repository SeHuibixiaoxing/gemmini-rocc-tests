# TracerV Metasim Bring-Up Plan

更新时间：`2026-04-19 07:52 UTC`

## 1. 目标

- 先用 FireSim manager 驱动的 baremetal metasim 跑通 `TracerV`，
  再决定是否继续消耗 FPGA 机时。
- 把当前 `TracerV` 诊断从
  “看 `TRACEFILE-C*` 猜”
  提升为
  “直接确认 host 是否发生过 nonzero pull”。
- 把验证拆成由浅到深的两阶段：
  - `stage A`：
    最小链路验证
  - `stage B`：
    接近当前问题形态的指令触发验证

## 2. 已知约束

- Verilator 路线必须走 FireSim metasim，
  不走独立 `sims/verilator`。
- FireSim 只能走 manager 正规流：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- FireMarshal / FireSim 必须分别通过：
  - `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
  - `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- 用户已明确要求：
  `stage B` 后续只使用本机 `metasim`，
  不再为该轮验证额外拉起远端 CPU run farm。
- metasim 模式下：
  `target_config.default_hw_config`
  必须引用 `config_build_recipes*.yaml`，
  不能继续引用 `config_hwdb*.yaml`。
- 结果判断优先级：
  1. `metasim_stderr.out` 中是否出现
     `TracerV[*]: first nonzero host pull ...`
  2. `TRACEFILE-C*` 是否非零
  3. `TRACEFILE-C*` 内容是否与配置的 trigger 语义一致

## 3. 当前判断

- 当前 `pairdummy/sbus128` 主线运行时配置指向
  `config_hwdb` 中的 AGFI 条目，
  不能直接原样切到 metasim。
- 当前仓库里已经存在可直接用于 metasim 的 build recipe：
  - `firesim_rocket_singlecore_no_nic_l2_lbp_30mhz`
  - `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
- 因此最稳妥路径不是先补 pairdummy build recipe，
  而是先用现成 build recipe 完成 `TracerV` bring-up，
  再决定是否继续补 pairdummy 专属 metasim 底座。
- 2026-04-19 新结论：
  当前 active `selector=3 + reset200` local metasim run
  并没有卡回 boot ROM；
  它停在 baremetal `segment3_repro`
  的 marker 之前长 prefill 窗口：
  - hart0：
    `fill_pattern(stage0_src, ...)`
    PC `0x80002354 ~ 0x80002364`
  - hart1：
    `barrier_wait()`
    PC `0x80002030 ~ 0x80002034`
- 这轮真正暴露出的根因不是 tracing bridge 不通，
  而是：
  **instruction trigger marker 放在两个长 prefill 之后**
  - `stage0 = 512 KiB`
  - `stage1 = 64 KiB`
  - `fill_pattern()` 按字节循环
- 指令流双采样表明当前 full-size repro 在慢速前进而不是死锁；
  以 2026-04-19 07:47Z~07:48Z 的采样速率粗估，
  到 start marker 还需约 `4.6h`。
- 因而后续 bring-up 顺序要调整成：
  **轻量变体优先，full-size repro 复核放后。**

## 4. 分阶段计划

### 4.1 Stage A: 最小链路验证

目标：

- 用最轻量 baremetal 负载确认：
  `TracerV bridge -> host pull -> serialize -> copy-back`
  整条路径是通的。

执行对象：

- 硬件：
  `firesim_rocket_singlecore_no_nic_l2_lbp_30mhz`
- workload：
  `hello-baremetal.json`
- trigger：
  `selector=0`
  或完整周期窗口

所需改动：

- 新增 metasim runtime config
- 新增专用 workload 输出配置，
  至少补回：
  - `TRACEFILE*`
  - `metasim_stderr.out`

通过标准：

- `metasim_stderr.out` 出现
  `TracerV[*]: first nonzero host pull ...`
- 至少一个 `TRACEFILE-C*` 非零
- `uartlog` 正常完成 baremetal workload

失败分流：

- 若无 `first nonzero host pull`：
  优先怀疑 metasim 配置 / tracing 使能 / host pull 路径
- 若有 `first nonzero host pull` 但 `TRACEFILE-C*` 为空或异常：
  优先怀疑 serialize / flush / copy-back

### 4.2 Stage B: 指令触发验证

目标：

- 在 baremetal metasim 上验证
  `selector=3`
  的精确指令触发路径，
  尤其是 exact instruction compare 是否真实命中。

执行对象：

- 首选硬件：
  `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
- 首选负载：
  轻量 baremetal repro
  或在现有 baremetal repro 上加精确 marker

执行顺序修正：

- `Stage B1`
  先用 **轻量 segment3 变体**
  验证 marker 命中与 `TRACEFILE` 非零：
  - 优先复用现有
    `host-init-export-dma-bertmini-segment3-repro.sh`
    已支持的参数
  - 通过
    `REROCC_EXPORT_STAGE0_BYTES`
    /
    `REROCC_EXPORT_STAGE1_BYTES`
    缩小 prefill
  - 保持
    `REROCC_SEG3_TRACERV_MARKERS=1`
- `Stage B2`
  只有当 `Stage B1` 通过后，
  才回到 full-size repro
  复核同一条 trigger 路径

marker 约束：

- start:
  `.word 0x00008013`
- end:
  `.word 0x00010013`
- 不依赖 shell 级 `firesim-start-trigger`
  / `firesim-end-trigger`

通过标准：

- `metasim_stderr.out` 出现
  `first nonzero host pull`
- `TRACEFILE-C*` 非零
- trace 起止与 marker 窗口一致
- 对 `segment3` 线程，再额外要求：
  marker 命中前不再需要等待数小时级 prefill

失败分流：

- guest 明确执行 marker，
  但 host 仍无 nonzero pull：
  优先怀疑 trigger compare / arm path
- host 已有 nonzero pull，
  但 trace窗口不对：
  优先怀疑 trigger 起止配置或 trace 内容解释

### 4.3 Stage C: 是否升级到 FPGA

仅当以下任一条件成立时，再升级到 FPGA：

- baremetal metasim 已稳定证明
  `TracerV` host path 正常，
  但当前问题只在 FPGA 主线复现
- 或者 metasim 已把问题逼到
  pairdummy 专属硬件配置差异，
  需要更接近主线的设计/时序环境

## 5. 这轮具体执行顺序

1. 落盘本计划文件
2. 若 `stage A` 还未完成，先补完 `stage A`
3. 准备 `stage B1` 轻量 segment3 变体
4. 在得到用户同意后，执行一次新的 local metasim
   `infrasetup -> runworkload`
5. 若 `stage B1` 通过，
   再准备 `stage B2` full-size 复核
6. 每轮都写：
   - `debug_records/<timestamp>.md`
   - `change_records/<timestamp>.md`

## 5.1 Stage B 执行形态修正

- `stage B` 改为：
  `externally_provisioned.yaml + localhost + num_metasims=1`
- 不再执行：
  远端 `launchrunfarm`
- 仍保留 manager 正规流中的其余步骤：
  `infrasetup -> runworkload`
  只是 run host 变成本机

## 6. 本轮不做的事

- 不直接把 `pairdummy/sbus128` 的 FPGA runtime config 强行改成 metasim
- 不先上 `verilator-debug`
- 不先补大范围热路径日志
- 不先重开新的 FPGA run 作为第一反应

## 7. 预期产出

- 一个可重复的 baremetal metasim `TracerV` smoke 路径
- 一套明确的成功/失败判据
- 是否需要继续补 pairdummy metasim build recipe 的结论
- 对当前 `segment3` 线程，再补充一个明确产出：
  - 不再把“长 prefill 中的不同采样点”
    误判成新的 hang frontier
