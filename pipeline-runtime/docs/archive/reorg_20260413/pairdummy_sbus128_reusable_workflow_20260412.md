# Pairdummy sbus128 可复用固定流程

更新时间：`2026-04-12`

## 1. 目标

这份文档只解决一个问题：

- 对当前 `12-pair sbus128` dummy-model bring-up 主线，如何用一套固定配置和固定脚本，稳定地完成 `image` 构建、freshness 校验、FireSim 启动与 `runworkload`，避免重复犯“路径看错、配置漏配、用了旧 image、拿错入口”的错误。

本文档不讨论：

- `real model` 主线
- `sbus64` / 其他硬件变体
- `DRAM_DEPEN` / buffer 语义等更深层实现缺陷

## 2. Canonical Source

当前这条主线的唯一固定入口如下：

- 固定语义 profile：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh`
- 固定 workflow 脚本：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh`
- 固定 workload json：
  `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json`
- 固定 FireSim runtime config：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml`
- 固定 hwdb：
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml`
- 固定 build recipes：
  `/home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml`

以后这条主线默认只认上面这些入口，不再接受临时 shell 里零散覆写一组 env 变量的做法。

## 3. 固定配置

当前固定 profile 明确写死这些语义：

- `TARGET_KEY=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128`
- `METHODS=ours2`
- `TARGET_BATCH=8`
- `NUM_CORES=4`
- `NUM_GEMMINI=12`
- `NUM_DMA=12`
- `PAIR_MANAGER_MODE=1`
- `GEMMINI_BASE_ID=0`
- `DMA_BASE_ID=0`
- `DUMMY_GEMMINI_MODE=1`
- `PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD=1`
- `PIPELINE_RUNTIME_SKIP_INPUT_LOAD=1`
- `PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK=1`
- `PIPELINE_RUNTIME_MLOCKALL_MODE=2`
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`
- `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=log`
- `PIPELINE_RUNTIME_UART_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`
- `PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_AUDIT_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=1`
- `PIPELINE_RUNTIME_LOG_PROFILE=coarse`
- `CAPTURE_PERIODIC_SYNC_ENABLE=1`
- `CAPTURE_PERIODIC_SYNC_SECONDS=1`

固定 profile 同时明确删除这些“容易漂移、但不是语义必需”的默认调试项：

- 不再通过默认值打开 mapping cache probe start/end
- 不再通过默认值打开 parse probe start/end
- 不再通过默认值打开 deep-log segment/global-stage/local-stage/subbatch gating
- 不再让 generic `fileonly-sync` 入口默认开启 `DUMMY_GEMMINI_MODE=1`

如果后续需要专门调某个 `segment`，应新增单独 debug profile 或明确修改固定 profile，而不是在 shell 中临时叠 env。

## 4. 入口与 freshness 规则

### 4.1 guest 真实入口

这条 workload 在 guest 里真正执行的入口是：

- `/firemarshal.sh`

不要再用以下路径推断真实执行入口：

- `/root/firemarshal.sh`
- workload 目录中的
  `rerocc-linux-tests-coupleddma/workload/overlay/root/firemarshal.sh`

对当前 FireMarshal 构建链路来说，真正会进入 image 的 `/firemarshal.sh` 内容由 FireMarshal 生成层决定；本地校验应以 image 内最终 `/firemarshal.sh` 为准。

### 4.2 local freshness

本地主线 freshness 必须校验以下 5 项：

- `/firemarshal.sh`
- `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh`
- `/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`
- `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
- `/firemarshal.env`

对应脚本：

- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh`

### 4.3 remote freshness

remote freshness 只允许使用私网 IP。

对应脚本：

- `/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh`

该脚本会：

1. 先重跑 local freshness
2. 再校验远端 `.img` 的 `sha256`
3. 拒绝公网地址

### 4.4 runworkload 前置条件

`runworkload` 之前必须完成这条闭环：

1. `marshal clean`
2. `marshal build`
3. `marshal install`
4. `local freshness`
5. `launchrunfarm`
6. `infrasetup`
7. `remote freshness`
8. `runworkload`

不能跳步，也不能拿“上次 build 过的 image”直接继续。

## 5. 固定 workflow

以后这条主线统一使用：

```bash
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_workflow.sh
```

常用命令：

```bash
# 查看固定 profile 和固定路径
.../pairdummy_sbus128_workflow.sh show

# 完整 image 闭环
.../pairdummy_sbus128_workflow.sh image-closure

# 启动 run farm
.../pairdummy_sbus128_workflow.sh launch

# 基础设施部署
.../pairdummy_sbus128_workflow.sh infrasetup

# 查询当前唯一运行中的私网 IP
.../pairdummy_sbus128_workflow.sh current-private-ip

# 用私网做 remote freshness
.../pairdummy_sbus128_workflow.sh remote-freshness 192.168.x.y

# remote freshness 通过后启动 runworkload
.../pairdummy_sbus128_workflow.sh run 192.168.x.y

# 结束 run farm
.../pairdummy_sbus128_workflow.sh terminate
```

## 6. 这个方案解决什么问题

### 6.1 防止“入口路径看错”

- 不再靠人脑记忆判断 `/firemarshal.sh` 来自哪里
- 统一通过 image freshness 脚本检查 image 内最终内容

### 6.2 防止“配置漏配”

- pairdummy 固定 profile 从 shell 临时 env 收敛到单个脚本
- `METHODS`、`dummy mode`、`skip flags`、日志路径、pair-manager 配置全部固定

### 6.3 防止“generic 脚本污染旧主线”

- generic `host-init-fileonly-sync.sh` 不再默认 `DUMMY_GEMMINI_MODE=1`
- pairdummy 专用入口单独 source 固定 profile

### 6.4 防止“用了旧 image”

- workflow 把 `image-closure` 和 `remote-freshness` 固定成标准步骤
- `run` 命令在真正启动 `runworkload` 前会先做 remote freshness

### 6.5 防止“监控又退回 UART”

- 固定 profile 写死：
  - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=log`
  - `PIPELINE_RUNTIME_UART_LOG_ENABLE=0`
  - `PIPELINE_RUNTIME_GUEST_LOG_ENABLE=1`

## 7. 仍然需要人工确认的项

这套固定方案不会替你做这些语义判断：

- 当前 runtime 是否已经恢复到能越过 `segment0`
- 当前 synthetic-model 锁页回归是否已修复
- 当前更深层 `DRAM_DEPEN` / `ALL_RINGBUFFER` 语义是否正确

它解决的是：

- 不要再因为流程和配置层面的错误，把调试结论污染掉
