# no-DMA performance prep for ours2 vs gemini2

调试类别：main

## 目标

在暂不恢复真实 DMA 的情况下，比较 HybridMapper 两类编排的模型执行时间：

- 用户口径 `ours` -> runtime artifact 方法名 `ours2`
- 用户口径 `gemmini` -> runtime artifact 方法名 `gemini2`

计时口径排除预处理。最终 F2 结论优先使用 trace 中的 `model_compute_ns`；同时记录
`model_exec_ns` 作为包含 segment action build / bind / xlate 等 runtime 模型执行窗口的
较宽口径。

## 已确认的关键结论

`cfg32/NIC/noTrace + no-DMA compute` 已在 `agfi-077451484fe3b63c3` 上完整 PASS：

- 配置：dummy8x8、sbus64、4 cores / 12 Gemmini managers / 12 DMA managers、batch8、
  pair-manager mode。
- 结果：`Simulation complete` / `*** PASSED *** after 22734035102 cycles`。
- checkpoint commit：`9742924 Record cfg32 no-DMA compute pass`。

这排除了 no-DMA 路线下的 artifact 读取、Gemmini compute、SPM xlate 和普通 pipebuf
控制流作为当前 blocker。真实正确性主线后续应回到 DMA submit/completion、
`hw_dma_fence()` / blocking wait、host buffer/direct DMA 与 producer publish。
`doneflag` 仍不能作为 DMA completion 证据。

## 本地 artifact

生成命令：

```bash
./.conda-env/bin/python conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py \
  --model bertmini \
  --methods ours2,gemini2 \
  --target-keys rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64 \
  --mode fresh \
  --runtime-root output/pipeline_runtime_experiments/20260510_no_dma_perf
```

输出目录：

```text
/home/ubuntu/chipyard/conference/HybridMapper/output/pipeline_runtime_experiments/20260510_no_dma_perf/bertmini
```

审计结果：

- `ours2`: PASS, `segments=15`
- `gemini2`: PASS, `segments=19`
- target:
  `rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64`
- page size: `1024`

实验目录与当前 overlay 的 pipeline YAML 哈希一致：

- `ours2`: `7daad3716f35098bee8462430dab6b94dc5c46910a8365a520b012d77f681727`
- `gemini2`: `a817a9747b5f9fcff1303d21d386504fe343b85536dd8d04d48a5bfbefe29f5f`

因此本轮不需要覆盖 overlay artifact。

## 本地 dry-run

本地 CPU/no-DMA dry-run 只用于验证 trace 字段和 runtime 口径，不作为 F2 性能结论。

共同参数：

```text
--backend cpu
--batch 8
--num-cores 4
--num-gemmini-mgrs 12
--num-dma-mgrs 12
--pair-manager-mode 1
--pages-per-acc 1024
--spm-page-bytes 1024
--no-dma-compute
--skip-model-bin-load
--skip-input-load
--skip-golden-check
```

trace 摘要：

| method | model_exec_ns | model_compute_ns | preprocess_ns | dma_submit_count | gemm_issue_count | drops |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ours2 | 22492145861 | 22489377164 | 70223519 | 0 | 320 | 0 |
| gemini2 | 22434696309 | 22431582785 | 70224510 | 0 | 320 | 0 |

## 新增运行入口

为避免污染前一轮 gdbserver 现场，本轮新增独立 no-DMA perf profile：

- `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_fixed_env.sh`
- `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh`
- runtime config:
  `/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_no_dma_perf_cfg32_nic_notrace.yaml`

profile 展开确认：

- `METHODS="ours2 gemini2"`
- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
- `TRACE_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`
- `PIPELINE_RUNTIME_LOCAL_GDB_ENABLE=0`
- breadcrumb / deep log / DMA probes / debug trigger 均关闭
- `debug-preflight_status=pass`

RISC-V Linux runtime binary 已强制重建；`strings` 确认包含：

- `model_exec_ns=%llu`
- `model_compute_ns=%llu`
- `preprocess_ns=%llu`
- `postprocess_ns=%llu`

## 下一步

1. 跑 `image-closure`，让 FireMarshal image 包含 fresh runtime binary 和 perf guest env。
2. 本地 freshness 通过后，只开一轮 F2 run farm。
3. `launch -> infrasetup -> run`，同一 guest 顺序运行 `ours2` 和 `gemini2`。
4. copy-back 后解析：
   - `/root/pipeline-runtime-debug/traces/ours2.trace`
   - `/root/pipeline-runtime-debug/traces/gemini2.trace`
5. 记录 `model_compute_ns` / `model_exec_ns` / `run_ns` / `preprocess_ns`。
6. 立即 `terminaterunfarm --forceterminate` 并确认没有残留 `f2.*` 实例。

## 2026-05-10 first F2 attempt

第一轮 perf F2 attempt 暴露出 profile 问题，已终止：

- session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-054251`
- instance: `i-0b53ac2d9918a90c1`, private IP `192.168.1.192`
- termination:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-terminaterunfarm-20260510-061959`
- EC2 state after terminate command: `shutting-down`
- evidence:
  - remote image freshness matched local SHA.
  - guest reached `/etc/init.d/S99run`.
  - wrapper status showed `profile_id=pairdummy-sbus64-dummy8x8-no-dma-perf-v1`,
    `no_dma_compute_enable=1`, `trace_enable=1`, `gdbserver_enable=0`.
  - guest log reached `[prt-early] calling runtime_init`, but did not reach
    `[prt-early] runtime_init done` before manual termination.
  - heartbeat advanced to `37530897115, 1917`, so host simulation was alive.
  - `ours2.trace` existed but size was still `0`, meaning runtime had not exited and had not
    dumped model execution timing.

Conclusion: this run was dominated by preprocessing/runtime init, not model execution. The perf
profile inherited `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1` from the fixed debug profile, while
the image already contained `gemmini_layer_mapping...yaml.cache.bin`. This is the wrong setting for
the requested comparison because preprocessing is explicitly outside the timing target.

Fix for the next attempt:

- set `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=0` in
  `pairdummy_sbus64_dummy8x8_no_dma_perf_fixed_env.sh`.
- rebuild/patch image, rerun freshness, then rerun one F2 attempt.
