# Pipeline Runtime Test Plan

更新时间：`2026-04-14 14:40 UTC`

## 1. 目标

当前测试不是一次性做 full correctness closure，而是分 4 层逐步验证：

1. 语义是否仍与 `HybridMapper + MudnacSim` 基线一致
2. 构建、脚本、image、freshness 是否闭环
3. 当前硬件配置与 runtime 配置是否匹配
4. `bertmini` dummy-model 是否在 `12-pair sbus128` 上继续向前推进

## 2. 静态校验

### 2.1 语义静态校验

目标：

- 对照对齐文档和上游源码，确认 runtime 能表达目标 buffer 语义
- 对照 artifact，确认 `conv + resadd` 当前边界没有被新改动破坏

主要核查项：

- `DRAM / DRAM_DEPEN / ISOLATE_SPM / SHARED_SPM / ALL_RINGBUFFER`
  的 entry/export 行为
- `vAccIdxList / pAccIdxList`
  是否满足 runtime 需要的 slot permutation / physical binding 约束
- 每个 segment 的 local stage 数是否仍落在当前 RR cfg 预算内
  注意：当前实现里的 `spm_xlate` helper 固定使用 `cfg15`，但普通
  `rr_cfg_id_for_stage(stage_id, opcode_id)` 映射并没有真正把 `cfg15`
  保留出来；例如 `stage_id=7` 且 `opcode_id=3` 时也会映射到 `cfg15`。
  因此这里应按“存在 cfg slot 复用风险”来核查，而不能再机械地假设
  “`cfg15` 已正式保留给 `spm_xlate`”。
- tensor 视图、slot 数、页数与 stage local SPM 需求
- `conv + resadd` 尺寸不匹配规则
- `tensor_id` 与 buffer 语义的一致性

入口：

- [`../architecture/alignment_constraints.md`](../architecture/alignment_constraints.md)
- [`../plans/runtime_alignment_plan.md`](../plans/runtime_alignment_plan.md)
- `conference/HybridMapper/*`
- `conference/MudnacSim/*`

推荐命令：

- `python3 scripts/audit_pipeline_runtime_artifact.py --pipeline-yaml <pipeline.yaml> --hardware-yaml <hardware_target.yaml> --model-yaml <model.layers.yaml> --expect-target-key <target_key>`

### 2.2 runtime / hardware 静态匹配校验

目标：

- 确认当前 `pairdummy/sbus128` workflow、profile、脚本和硬件配置一致

主要核查项：

- fixed profile 与 workload json
- FireSim runtime config / hwdb / build recipes
- `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
- hugetlb / `spm_xlate` 约束
- freshness 脚本覆盖项
- guest-file-first 观测链

## 3. 构建与 workflow 校验

### 3.1 本地构建

- 快速静态校验优先：
  - `python3 -m py_compile scripts/triage_prt_capture.py`
  - `bash -n <touched-shell-scripts>`
  - `cc -std=gnu11 -fsyntax-only <touched-c-files>`
- `make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j4`
  只作为补充；
  host 本机对 `GNU asm` / 既有 unused helper 的告警，
  不能单独当成 runtime 语义回退证据。

### 3.2 Canonical workflow

每次动态实验前，固定执行：

1. `debug-preflight`
2. `image-closure`
3. `launch`
4. `infrasetup`
5. `current-private-ip`
6. `remote-freshness <private-ip>`
7. `run <private-ip>`

## 4. 动态校验

### 4.1 主动态 case

- 模型：
  `bertmini`
- 硬件：
  当前 `12-pair sbus128` target
- 产物：
  `HybridMapper` 输出的完整 pipeline runtime artifact

### 4.2 动态验证目标

- 先统计当前 bertmini 编排文件覆盖了哪些 buffer 类型
- 再在 fixed workflow 上运行 runtime
- 主观测面为 guest 文件系统，不用 `uartlog` 作为主证据
- 粗粒度日志用于快速推进
- 细粒度日志只在某个 segment/stage/subbatch/page 条件下开启，用于定位候选卡点
- 进入 live repro 之后，先用：
  `python3 scripts/triage_prt_capture.py <capture-dir>`
  汇总 frontier，再决定是否需要新的 probe
- 若需要新的 probe，
  先跑：
  `python3 scripts/triage_prt_capture.py <capture-dir> --emit-trigger-env`
  然后只做一次 trigger-gated 单变量 rerun

### 4.3 当前 acceptance

当前主线通过标准：

- fixed workflow 不漂移
- freshness 闭环通过
- guest 文件日志与 breadcrumb 正常落盘
- 当前 blocker 可以用低扰动证据定位
- frontier claim 已标明是否需要 control rerun
- 不出现回退到旧的 boot / hugetlb / stale-image / old export plateau

## 5. 记录

- 每次动态实验都必须新增：
  - `debug_records/<timestamp>.md`
  - 若有改动，再新增 `change_records/<timestamp>.md`
- `CURRENT_STATUS.md` 只收敛最新 authoritative 结论，不再写长时间线
