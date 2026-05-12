# Pairdummy sbus128 Workflow

更新时间：`2026-04-15 14:49 UTC`

## 1. 适用范围

本流程只服务当前主线：

- 硬件：
  `12-pair sbus128`
- workload：
  `bertmini batch8 file-only pairdummy`
- 目标：
  固定构建、freshness、FireSim、日志与 debugfs 观测流程

## 2. Canonical Inputs

- fixed profile：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
- fixed workflow：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
- workload json：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
- runtime config：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
- hwdb：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
- build recipes：
  `/home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml`

## 3. 固定 profile 摘要

当前主线固定 profile 重点参数：

- `PIPELINE_RUNTIME_PROFILE_ID=pairdummy-sbus128-fixed-v25`
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
- `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1`
- `PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START=4294967295`
- `PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END=4294967295`
- `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`
- 当前 profile 的目的：
  去掉旧的高频 fixed-load / export probe，
  并进一步收掉
  breadcrumb 已覆盖的 pointwise coarse guest log，
  让 pointwise 热路径继续回到 breadcrumb-first 的低扰动观测面；
  并把 export sparse 页日志窗口钉到不命中真实页的 sentinel，
  避免 baseline 意外回落到默认 `page>=48 / page%8 / 首尾页` 的热路径页日志；
  trigger-gated 短日志默认关闭，
  只在 triage 明确建议后做单变量 overlay rerun

## 4. 标准执行顺序

所有动作都通过 workflow 脚本发起：

```bash
cd /home/ubuntu/chipyard
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh show
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh debug-preflight
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh image-closure
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh launch
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh infrasetup
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh current-private-ip
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh remote-freshness <private-ip>
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh run <private-ip>
```

结束或异常后：

```bash
cd /home/ubuntu/chipyard
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh terminate
```

## 5. Freshness

### 5.1 Local freshness

- 由 `image-closure` 统一跑：
  - `marshal clean`
  - `marshal build`
  - `marshal install`
  - render effective guest `firemarshal.env`
  - patch local image `/firemarshal.env`
  - local image freshness
- local freshness 必须验证：
  - guest image
  - runner script
  - runtime binary
  - `firemarshal.env`
- 说明：
  - baseline `firemarshal.env` 默认保持 control 配置
  - 唯一允许的临时 overlay 仍然只有 `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
    与少量 `PIPELINE_RUNTIME_BREADCRUMB_*`
  - overlay 不能只停留在 host shell；必须先渲染成 effective guest env，再实际写入 image 中的 `/firemarshal.env`
  - `PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START/END` 在 control baseline 中不能留空；
    留空会重新打开默认 export sparse 页日志

### 5.2 Remote freshness

- run host 准备完成后，必须用私网 IP 执行：
  `remote-freshness <private-ip>`
- remote freshness 目标是 run host 上的 guest image，而不是 host 根文件系统。

## 6. Live 观测

- pane / manager / watchdog 日志看：
  - `tmp/firesim-aws-f2/tmux/`
  - `sims/firesim/deploy/logs/`
- guest 侧状态以 guest image 中的文件为准：
  - `bertmini-batch8.status`
  - `bertmini-batch8.log`
  - `bertmini-batch8.deep.log`
  - `bertmini-batch8.breadcrumb.bin`
- 主观测面是 guest 文件系统；
  `uartlog` 只做 boot 活性与 panic 辅助。
- Linux boot 早期若没有明确 boot error / panic / crash，
  且 `heartbeat.csv` 仍在推进，
  就继续等；
  不要只凭 `uartlog` 的静默窗口把它记成新的异常。

更细观测方法见：

- [`../testing/observability.md`](../testing/observability.md)

## 7. Trigger 单变量 rerun

默认规则：

- 不手工改动 fixed profile 的语义参数
- 当前唯一允许临时 overlay 的参数族是：
  `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
  与少量 `PIPELINE_RUNTIME_BREADCRUMB_*`
- overlay 只能来自
  `triage_prt_capture.py --emit-trigger-env`
  生成的 trigger block，
  或者人工指定的 breadcrumb 收窄条件

推荐操作：

```bash
cd /home/ubuntu/chipyard
(
  eval "$(
    python3 generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/triage_prt_capture.py <capture-dir> --emit-trigger-env \
      | sed -n '/^trigger_env:/,$p' | tail -n +2
  )"
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh debug-preflight
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh image-closure
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh launch
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh infrasetup
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh current-private-ip
)
```

说明：

- 用 subshell 是为了避免 trigger env 泄漏到后续普通 rerun
- `debug-preflight` 会拦截多类高风险 probe 同开
- `image-closure` / `infrasetup` 会把当轮 trigger overlay 物化到 guest image 的 `/firemarshal.env`
- 若本轮 claim 属于 `observability_only`，
  后面必须补 control rerun

低扰动 breadcrumb-only rerun：

```bash
cd /home/ubuntu/chipyard
(
  export PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=0
  export PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH=5
  export PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS=0
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh debug-preflight
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh launch
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh infrasetup
  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh current-private-ip
)
```

说明：

- 这类 rerun 的目标是减少 breadcrumb 写入量，不是在热路径增加新日志。
- 只改 guest env 时，不需要重新 `marshal build/install`；
  `infrasetup` 会重新 patch local image 中的 `/firemarshal.env`
  并把新 image 下发到 run farm。
- 允许临时覆盖的 breadcrumb 变量只包括：
  `ENABLE/PATH/SEGMENT/GLOBAL_STAGE/LOCAL_STAGE/SUBBATCH/STAGE_RADIUS/SUBBATCH_RADIUS`

## 8. 禁止事项

- 不要绕过 fixed profile 手工拼 env。
- 不要绕过 wrapper 直接裸跑 `marshal` 或 `firesim`。
- 不要在 freshness 失败时继续解释成新的 runtime blocker。
- 不要用公网 IP SSH。
