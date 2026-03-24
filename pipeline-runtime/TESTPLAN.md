# Pipeline Runtime Test Plan

日期：2026-03-24

## 1. 验收目标

最终验收固定为：

- 模型：`bertmini`
- 方法：`ours2 / gemini2 / tangram2`
- 目标平台：Linux on FireSim F2
- 目标硬件：`GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`
- 判据：runtime 输出与 CPU golden 一致，且 `uartlog` 中出现 `BERTMINI_PIPELINE_RUNTIME_PASS`

固定约束：

- 不改硬件，只做软件修复
- 不恢复 `host_addr` 特判
- 不把多 manager 路径降级成 single manager
- 不破坏 shared-spad `all-bank / 1KB interleaved / multi-manager` 设计目标
- bertmini 路径禁止 CPU fallback
- Linux 启动阶段只要没有明确错误且 heartbeat 继续增长，就继续等待

## 2. 当前验证基线

当前推荐的验证阶梯：

1. artifact export gate
2. host closure gate
3. Linux packaging gate
4. Linux coupleddma interface-reference gate
5. baremetal shared-spad interleaved regression gate
6. FireMarshal build/install gate
7. FireSim F2 replay gate

## 3. 当前已锁定结论

### 3.1 baremetal correctness gate 已闭环

决定性回归目录：

- `/home/ubuntu/chipyard/sims/firesim/deploy/results-workload/2026-03-24--12-47-02-rerocc-lc-baremetal-coupleddma-explicit-interleaved-small-f2-rerocc-baremetal-explicit-interleaved-small`

决定性结论：

- `ALL_TESTS_PASS`
- `*** PASSED *** after 27505375802 cycles`

已确认通过的关键历史卡点包括：

- `copy_explicit_cross_1kb_interleaved_b_mvin2`
- `copy_explicit_cross_1kb_interleaved_b_mvin2_clean`
- `resadd_explicit_cross_1kb_interleaved`

因此 baremetal 已不再是 active blocker；它现在是 runtime 语义回归门。

### 3.2 已闭环问题的根因

1. `copy_explicit_cross_1kb_interleaved_b_mvin2`
   - 根因不是 interleaved shared-spad 翻译坏掉
   - 根因是 accumulator address / accumulate-on-write 语义理解错误
   - 修复方式是先显式初始化目标 acc 行，再 `mvin2`
2. `resadd_explicit_cross_1kb_interleaved`
   - 根因不是 hardware / alias translation
   - 根因是手写 explicit overlap 序列缺少完整 manager-visible completion chain
   - 修复方式是：
     `A mvin -> rr_fence -> B mvin2 -> rr_fence -> mvout`
3. pointwise `J=128`
   - 根因不是硬件块宽上限
   - 根因是早期 baremetal VA / PTE overlap

### 3.3 当前不应再重开的假设

- shared-spad `1KB` interleaved alias translation 天然有问题
- `mvin2` 天然不能读 interleaved shared-spad
- standard WS resadd 不能用于当前 shared-spad 设计
- pointwise `J=128` 天然不被支持
- 需要 RTL 改动才能继续推进

## 4. pipeline-runtime 当前改进方向

### 4.1 优先检查 runtime 的 page-placement / manager contract

当前最强怀疑是：

- fixed-weight 页分配已经按多 manager 视图处理
- 但 entry/export tensor 的 local slot 页分配、exec-view rebase、manager binding 仍可能残留 `stage_acc` 偏置

因此先看：

- `prt_runtime.c`
  - `build_topology_from_pipeline(...)`
  - `runtime_stage_local_page_accs(...)`
  - `register_shared_plan(...)`
  - `stage_prepare_exec_views(...)`
  - `stage_tensor_exec_addr(...)`

目标 contract：

- 同一 stage 的多 manager 共享一段连续 shared-spad alias VA 视图
- 每个 manager 对这段 VA 独立完成页表翻译
- 物理页仍保持 `all-bank / 1KB interleaved` 分配

### 4.2 收紧 HybridMapper -> runtime 元数据语义

当前已经进入主线的方向：

- `tensorStride` 已开始由 HybridMapper 导出
- runtime 已开始消费 `tensorStride`

仍需继续验证：

- `input / weight / output size`
- `in_stride / weight_stride / out_stride`
- `pad`

之间的关系是否完全自洽。

原则：

- 不再回到 guessed stride
- 若 `pad` 仍通过 runtime 推导，则必须继续做 size/shape 校验

### 4.3 Gemmini adapter 优先标准语义

- standard WS path 优先
- 手写 explicit path 只能在必要时保留
- 手写 explicit path 必须满足 baremetal 已验证语义：
  - accumulator 地址位语义
  - `rr_fence(cfg_id)` completion 语义
  - 不能把 `mvin2` 当成纯覆盖写

## 5. 各 gate 的执行与通过标准

### 5.1 Artifact Export Gate

导出命令：

```bash
cd /home/ubuntu/chipyard
python3 conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py --model bertmini
```

通过标准：

- exporter 不报错
- `conference/HybridMapper/output/pipeline_runtime/bertmini/` 中 runtime 所需文件齐全
- `model.layers.yaml` 保持 `tensorStride` / `tensorSize` 元数据

### 5.2 Host Closure Gate

构建命令：

```bash
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all
```

执行命令：

```bash
METHODS=ours2 BATCH=1 SKIP_EXPORT=1 SKIP_BUILD=1 \
bash /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

通过标准：

- `pipeline_runtime` 构建成功
- closure 脚本最终打印 `BERTMINI_HOST_CLOSURE_PASS`

### 5.3 Linux Packaging Gate

静态检查：

```bash
cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload
HOST_INIT_CHECK_ONLY=1 bash host-init.sh
```

完整 staging：

```bash
cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload
bash host-init.sh
```

通过标准：

- overlay 中存在 runtime binary 和 bertmini artifacts
- 最终 binary 保留早期进度字符串

### 5.4 Linux Coupleddma Interface-Reference Gate

涉及 Linux coupleddma / DMA helper / ReRoCC helper 改动时，对照这些正例：

- `rerocc_dma_matrix_linux_coupleddma.c`
- `rerocc_lc_gemmini_matrix_linux_coupleddma.c`
- `rerocc_lc_coverage_linux_coupleddma.c`
- `rerocc_lc_nonblocking_linux_coupleddma.c`

通过标准：

- 不背离 Linux 正例的关键调用顺序
- 不绕过 host-page chunk helper
- 不新增危险的 DRAM 直通 shortcut

### 5.5 Baremetal Shared-SPad Interleaved Regression Gate

最小回归源码：

- `generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`

workload：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-explicit-interleaved-small.json`

runtime config：

- `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_explicit_interleaved_small.yaml`

当前通过标准：

- `uartlog` 中关键用例全 PASS
- 最终出现 `ALL_TESTS_PASS`
- 最终出现 `*** PASSED ***`

注意：

- 该 gate 现在是“全绿回归门”，不再是“旧差分必须继续保持 FAIL/PASS 组合”的调查门

### 5.6 FireMarshal Build/Install Gate

必须通过 `tmux` wrapper 执行：

- `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`

固定流程：

```bash
cd /home/ubuntu/chipyard
source env.sh
marshal build generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json
marshal install generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json
```

### 5.7 FireSim F2 Replay Gate

FireSim manager 固定流程：

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
```

然后通过 `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh` 依次执行：

1. `launchrunfarm`
2. `infrasetup`
3. `runworkload`
4. `terminaterunfarm`

通过标准：

- 不能只看 manager exit code
- 必须看 guest `uartlog`
- Linux boot 若无明确失败且 heartbeat 在动，则继续等待

## 6. 当前主线的成功标准

- Linux `bertmini` pipeline-runtime 闭环
- `ours2 / gemini2 / tangram2` 都能过
- `BERTMINI_PIPELINE_RUNTIME_PASS` 出现在 `uartlog`
- baremetal regression 保持全绿
