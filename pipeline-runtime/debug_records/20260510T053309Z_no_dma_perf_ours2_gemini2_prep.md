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

## 2026-05-10 second F2 attempt: trace event stall

第二轮 F2 perf attempt 已终止；它不再卡在 `runtime_init`，但进入模型执行后长时间不前进：

- runworkload session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-062722`
- instance: `i-09ce8610ae68633f7`, private IP `192.168.1.113`
- AGFI: `agfi-077451484fe3b63c3`
- remote/local image SHA matched:
  `aa49758c71edb1544a596e8b5b55bbd770fe9da1e2310cabc5f18f9ada78088b`
- guest env SHA:
  `97497d36763fe74a78c807a0a6a5a074881e8665824bd69bd73cafbb6c69c5f6`
- guest env confirmed:
  - `METHODS='ours2 gemini2'`
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='0'`
  - `TRACE_ENABLE='1'`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`
- evidence:
  - guest booted to `/etc/init.d/S99run`
  - wrapper spawned child `pid=143`
  - status showed `state=running`, `no_dma_compute_enable=1`, `trace_enable=1`
  - heartbeat reached `18317918397, 960`, then stopped for more than 10 minutes
  - UART showed RCU stall with running task `rerocc_pipeline`
  - trace dir and runner stage files were still empty, but logging/sync were intentionally disabled;
    the stronger evidence is stopped heartbeat plus RCU stall
- termination:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-terminaterunfarm-20260510-065919`
- archived evidence:
  `debug_records/artifacts/20260510T0656_no_dma_perf_trace_event_stall/`

Conclusion: this is not the earlier YAML/cache preprocessing problem. The important delta from the
known-good noTrace no-DMA pass is `TRACE_ENABLE=1`, which allocated the event ring, performed
cycle calibration, and enabled hot-path `prt_trace_log_event()` calls around GEMM/DMA/export
events. For the perf comparison we only need summary fields, so event-level trace is unnecessary
and risks perturbing the no-DMA compute path.

## 2026-05-10 summary-only trace local validation

Added `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY=1` for the no-DMA perf profile:

- runtime keeps `run_ns`, `model_exec_ns`, `model_compute_ns`, `preprocess_ns`, counters, and SPM
  summary fields.
- runtime does not allocate `trace_events` in summary-only mode.
- `prt_trace_calibrate_cycle()` returns immediately with overhead `0`.
- `prt_trace_run_start()` does not read a cycle reference in summary-only mode.
- `prt_trace_dump()` skips the event loop when no event ring exists and emits
  `trace_summary_only=1`.

Local validation:

- `make -C pipeline-runtime -j$(nproc)`: pass.
- workflow visibility:
  - `show`: `trace_enable=1`, `trace_summary_only=1`,
    `disable_mapping_cache=0`
  - `debug-preflight`: `debug_preflight_status=pass`,
    `debug_preflight_trace_summary_only=1`
- rendered guest env includes:
  `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
- CPU/no-DMA dry-run with summary-only trace:

| method | model_exec_ns | model_compute_ns | preprocess_ns | dma_submit_count | gemm_issue_count | trace_summary_only | trace_event_count |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ours2 | 22428985978 | 22426157291 | 68091939 | 0 | 320 | 1 | 0 |
| gemini2 | 22486483797 | 22483266989 | 68838534 | 0 | 320 | 1 | 0 |

These local timings only validate the measurement path; they are not F2 performance results.
Next F2 attempt must rebuild/patch the image, verify freshness, run once, copy traces, and
terminate the run farm immediately.

## 2026-05-10 image closure after summary-only trace

`image-closure` completed locally without starting F2:

```bash
./pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh image-closure
```

Results:

- clean tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-clean-20260510-070832`
- build tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-build-20260510-070837`
- install tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-install-20260510-070947`
- guest env SHA:
  `9b580a62b90d8e528d235462b4985e657ad6e9563f2eba980f2e70b1a26e707c`
- runtime binary SHA in image:
  `0d126d06d4186316961aff539e9f72670fe8eabbd13a457a79172842f87e3a56`
- local image freshness: PASS
- `/firemarshal.env` confirms:
  - `PIPELINE_RUNTIME_PROFILE_ID='pairdummy-sbus64-dummy8x8-no-dma-perf-v1'`
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
  - `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='0'`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`

Next F2 run should use this exact image state and compare
`/root/pipeline-runtime-debug/traces/{ours2,gemini2}.trace`.

## 2026-05-10 third F2 attempt: summary-only trace still stalls

第三轮 F2 perf attempt 已终止；`PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY=1` 排除了 event ring
和 cycle calibration 之后，run 仍然没有完成：

- runworkload session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-071554`
- instance: `i-0d16e2264e0110066`, private IP `192.168.1.88`
- AGFI: `agfi-077451484fe3b63c3`
- remote/local image SHA matched:
  `366d1865900027b792fc384e09e416173d7d01e74fadf0a2aad25f5b72e6bde2`
- guest `/firemarshal.env` confirmed:
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
  - `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='0'`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`
- evidence:
  - wrapper spawned child `pid=143`; UART RCU task was `rerocc_pipeline`, `pid=204`, `ppid=143`
  - heartbeat stopped at `18382742666, 963`
  - UART reported `rcu_sched detected stalls` for running task `rerocc_pipeline`
  - `ours2.trace` and `gemini2.trace` were not present, so no final timing summary was emitted
- archived evidence:
  `debug_records/artifacts/20260510T0742_no_dma_perf_summary_trace_stall/`
- termination:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-terminaterunfarm-20260510-074307`
  and the instance was last observed as `shutting-down`; later running-F2 query returned empty.

Conclusion: event-level trace was not the only cause. The remaining important delta from the
2026-05-09 full no-DMA PASS is the mapping-cache path. The earlier "enable mapping cache for perf"
correction is therefore superseded for correctness/debuggability: the perf profile now defaults
back to `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`, matching the stable YAML path. This can make
preprocessing slower, but the requested comparison uses `model_compute_ns` / `model_exec_ns` and
keeps `preprocess_ns` separate, so preprocessing time is not part of the reported performance
ratio.

Next local steps before any new F2 run:

1. Re-render/show the workflow and confirm `disable_mapping_cache=1`.
2. Rebuild or patch the guest image and verify `/firemarshal.env` freshness.
3. Run only one F2 attempt. If it still stalls, stop pursuing the non-GDB perf profile and return
   to the known-good gdbserver flow with thin breakpoints.

## 2026-05-10 local validation after cache-path rollback

After changing the perf fixed env to `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`:

- `pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh show`:
  `profile_id=pairdummy-sbus64-dummy8x8-no-dma-perf-v2-cache-disabled`,
  `trace_summary_only=1`, `disable_mapping_cache=1`.
- `debug-preflight`: `debug_preflight_status=pass`, tier 1.
- `bash -n` passed for the perf fixed env and workflow wrappers.
- host CPU/no-DMA summary-only dry-run with the cache-disabled/YAML path:

| method | run_ns | model_exec_ns | model_compute_ns | preprocess_ns | dma_submit_count | gemm_issue_count | trace_summary_only | trace_event_count |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| ours2 | 22531667487 | 22463185740 | 22460364344 | 68481540 | 0 | 320 | 1 | 0 |
| gemini2 | 22507923957 | 22440223223 | 22436967650 | 67700560 | 0 | 320 | 1 | 0 |

This is still only a measurement-path sanity check. F2 remains the authority for the final
no-DMA scheduling comparison.

## 2026-05-10 image closure after cache-path rollback

Ran:

```bash
./pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh image-closure
```

Results:

- clean tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-clean-20260510-075101`
- build tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-build-20260510-075106`
- install tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-install-20260510-075211`
- guest env SHA:
  `c97f389e837ddfcb456b87c2f8116b5fdf62715ddae1c500e5e85a7c20e61866`
- runtime binary SHA in image:
  `0d126d06d4186316961aff539e9f72670fe8eabbd13a457a79172842f87e3a56`
- local image freshness: PASS.
- rendered env confirms:
  - `PIPELINE_RUNTIME_PROFILE_ID='pairdummy-sbus64-dummy8x8-no-dma-perf-v2-cache-disabled'`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='1'`
  - `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`

No F2 instance was launched for this checkpoint.

## 2026-05-10 fourth F2 attempt: cache-disabled profile still early-stalls

The cache-disabled summary-only perf profile did not produce performance traces:

- launch session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-launchrunfarm-20260510-075330`
- infrasetup session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-infrasetup-20260510-075410`
- runworkload session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-075758`
- instance: `i-0e51e94770ad722c1`, private IP `192.168.1.205`
- AGFI: `agfi-077451484fe3b63c3`
- remote/local image SHA matched:
  `d82ab86239590c2d56893671ad464a089e1d60a6150815bc79f40650751db6fd`
- guest env SHA:
  `c97f389e837ddfcb456b87c2f8116b5fdf62715ddae1c500e5e85a7c20e61866`
- guest status confirmed:
  `profile_id=pairdummy-sbus64-dummy8x8-no-dma-perf-v2-cache-disabled`,
  `no_dma_compute_enable=1`, `trace_enable=1`, `gdbserver_enable=0`.
- evidence:
  - UART reached `running /etc/init.d/S99run`.
  - wrapper reached `after-child-spawn pid=143`.
  - `runner.stage` stayed empty and no `ours2.trace` / `gemini2.trace` was emitted.
  - heartbeat eventually produced one sample, then stopped at `18326725699, 965`.
  - UART did not show the later RCU stall seen in the prior summary-only/cache-enabled attempt.
- archived evidence:
  `debug_records/artifacts/20260510T0820_no_dma_perf_cache_disabled_early_stall/`
- termination:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-terminaterunfarm-20260510-082057`.
  Instance `i-0e51e94770ad722c1` was observed as `shutting-down`; later running-F2 query returned
  empty.

Conclusion: reverting the mapping-cache path was not enough. The remaining high-confidence
configuration delta from the known-good full no-DMA pass is stdio routing: known-good used
`PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=uart`, while the perf profile used `log`, redirecting
wrapper/runner stdout into the guest rootfs log file. That keeps the run inside a file-backed path
exactly while the child is launched, and the current frontier is before any runner stage is visible.
The perf profile now switches stdio back to `uart`. This is not meant to add new hot-path logging;
the known-good run's UART was only about 21 KiB, and `preprocess_ns` remains separate from
`model_compute_ns`.

## 2026-05-10 image closure after stdio rollback

Ran:

```bash
./pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh image-closure
```

Results:

- clean tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-clean-20260510-082543`
- build tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-build-20260510-082548`
- install tmux:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-install-20260510-082658`
- guest env SHA:
  `77b573ce41e82b0b04ade8467b58918af2cc5d7672c25aa1e1c62bd95f2b191f`
- runtime binary SHA in image:
  `0d126d06d4186316961aff539e9f72670fe8eabbd13a457a79172842f87e3a56`
- local image SHA:
  `6d47ec5297274bc32f66966450b44eab78d50b6ca504c7fdbe939e9ca27e6514`
- local image freshness: PASS.
- rendered env confirms:
  - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='uart'`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='1'`
  - `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`

No F2 instance was launched for this checkpoint. If one more non-GDB perf run is spent, the next
required sequence is `launchrunfarm -> infrasetup -> remote freshness -> runworkload -> copy traces
or live evidence -> terminate`. If it still stops before `runner.stage`, stop this perf profile path
and return to the known-good gdbserver thin-breakpoint flow.

## 2026-05-10 fifth F2 attempt invalidated by stale watchdog

After cleaning up local state only partially, one more F2 attempt was launched:

- launch:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-launchrunfarm-20260510-082953`
- infrasetup:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-infrasetup-20260510-083015`
- runworkload:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-083515`
- instance:
  `i-06c55736f7b960139`, private IP `192.168.1.39`
- AGFI:
  `agfi-077451484fe3b63c3`
- remote/local image SHA before run:
  `d2c02a5c8aee3fb79674a38d942c0f347c41239d73ec2c270b2522ea60eb7f05`
- guest env SHA:
  `77b573ce41e82b0b04ade8467b58918af2cc5d7672c25aa1e1c62bd95f2b191f`
- UART reached `S99run` and confirmed:
  `stdio capture mode: uart`, `no-dma compute enable: 1`, `trace enable: 1`,
  and `gdbserver enable: 0`.

This run is invalid as performance evidence. The instance was terminated by a stale local
runworkload watchdog from the earlier `20260510-054251` session, not by the current no-DMA runtime:

- old watchdog timeout:
  `2026-05-10T08:42:52Z`
- old watchdog action:
  `terminaterunfarm --forceterminate`
- old terminaterunfarm target:
  `i-06c55736f7b960139`
- CloudTrail:
  `TerminateInstances` at `2026-05-10T08:43:09Z`, source IP `54.68.14.45`
  (the current FireSim manager).
- current run status before termination:
  `guest_status=1738`, `guest_runner_post=363`, `guest_runner=0`; no `ours2.trace` /
  `gemini2.trace`.
- local copied results:
  only `HW_CFG_SUMMARY` and `sim-run.sh`.
- archived evidence:
  `debug_records/artifacts/20260510T0843_no_dma_perf_stale_watchdog_terminate/`

Cleanup completed after the diagnosis:

- killed stale same-tag no-DMA perf `runworkload` tmux sessions.
- killed orphan FireSim `runworkload` manager processes for the same runtime config.
- running/pending F2 query returned empty.

Workflow hardening: `pairdummy_sbus128_workflow.sh` now has a
`stale-runworkload-check` command and runs that check before `launch`, `infrasetup`, and `run`.
It refuses to proceed if a same-tag tmux session or FireSim runworkload process is still alive.

## 2026-05-10T09:22Z stdio/cache-disabled no-DMA perf attempt still stalls

After clearing the stale manager/watchdog state, a fresh F2 attempt was run with the intended
low-noise performance profile:

- launch:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-launchrunfarm-20260510-085540`
- infrasetup:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-infrasetup-20260510-085611`
- runworkload:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-090013`
- instance:
  `i-018ec159180f85f6c`, private IP `192.168.1.72`
- AGFI:
  `agfi-077451484fe3b63c3`
- remote/local image SHA before run:
  `56b5ff0df550439d18a03fa44b98b0e527c4048d1decbdc6fb174838a2e58d54`
- guest env SHA:
  `77b573ce41e82b0b04ade8467b58918af2cc5d7672c25aa1e1c62bd95f2b191f`

Effective guest config matched the intended no-DMA perf profile:

- `METHODS='ours2 gemini2'`
- `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='uart'`
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE='1'`
- `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
- `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
- `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`

Live evidence:

- UART reached `S99run`, started `ours2`, and reached `[prt-early] calling runtime_run`.
- `heartbeat.csv` advanced once to `18337109557, 965` and then stopped.
- The 2026-05-09 full PASS completed after the comparable heartbeat
  `18276268247, 964` by watchdog `hb_idle=187s`; this run reached `hb_idle=313s`
  at the same frozen heartbeat without completion.
- debugfs showed:
  - `state=running`
  - empty `runner.stage`
  - empty `/root/pipeline-runtime-debug/traces`
  - no `ours2.trace` / `gemini2.trace`
- Local result copy-back only had `HW_CFG_SUMMARY` and `sim-run.sh`.

Archived evidence:

```text
pipeline-runtime/debug_records/artifacts/20260510T0922_no_dma_perf_uart_cache_disabled_stall/
```

This run is not performance evidence. It reproduces the non-GDB no-DMA perf stall even after
returning to the known-good `uart` stdio path and cache-disabled YAML path.

Static comparison against the authoritative no-DMA PASS checkpoint `9742924` showed that the
runtime/scheduler/artifact-read surface has not been materially changed for no-DMA execution:

- `prt_scheduler.c`, `prt_gemmini_artifacts.c`, and `main.c` have no diff from `9742924..HEAD`
  in the inspected set.
- The only runtime diffs are summary trace timing fields and trace-summary-only event-ring
  suppression in `prt_runtime.c` / `prt_runtime.h`.

So the current stall should not be treated as evidence that artifact reading or the no-DMA
transport logic regressed. The next debugging step should avoid another blind non-GDB F2 perf run:
use the known-good gdbserver low-noise profile and a thin breakpoint/frontier around
`runtime_run` / segment execution, or locally inspect the summary trace timing additions for an
unintended interaction before spending another F2 cycle.

Cleanup:

- `terminaterunfarm --forceterminate` targeted `i-018ec159180f85f6c` successfully.
- The instance entered `shutting-down`.
- The stale runworkload tmux session and FireSim manager processes for this runtime config were
  killed locally.
- A running/pending F2 query returned empty after cleanup.

## 2026-05-10T09:45Z local audit of method plug-in path

The runner path was rechecked after the user clarified the intended experiment model.
The current execution model matches that expectation:

- `BIN` is fixed to
  `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`.
- `MODEL_YAML` and `LAYER_MAPPING_YAML` are shared for all methods.
- `METHODS='ours2 gemini2'` drives a loop where only
  `PIPELINE_YAML="${BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"`
  and `TRACE_PATH="${TRACE_DIR}/${method}.trace"` change.

Local image/env closure was also rechecked:

- `pairdummy_sbus64_dummy8x8_no_dma_perf_cfg32_nic_notrace_workflow.sh local-freshness`
  passed.
- effective guest env SHA:
  `77b573ce41e82b0b04ade8467b58918af2cc5d7672c25aa1e1c62bd95f2b191f`
- the image `/firemarshal.env` contains:
  - `METHODS='ours2 gemini2'`
  - `TRACE_ENABLE='1'`
  - `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY='1'`
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE='1'`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE='0'`

One evidence-chain gap remains: `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY` is present in
`render_pairdummy_guest_env.sh` and in the patched image, but not in the generic `host-init.sh`
env whitelist and not printed by wrapper status. Because `/firemarshal.env` uses `export`, a shell
inheritance check shows the variable survives `PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE=1`, so this is
not currently a proven root cause. It is still worth fixing before another expensive F2 round so
future status artifacts directly prove the runtime trace mode.

Local artifact audit results for the sbus64 dummy8x8 target:

| method | audit | segments | total stages | split summary | ring segments |
| --- | --- | ---: | ---: | --- | --- |
| `ours2` | PASS | 15 | 40 | `oc=32`, `single=4`, `resadd_spatial=4` | `[2,3,5,6,8,13,14]` |
| `gemini2` | PASS | 19 | 40 | `oc=32`, `single=2`, `resadd_spatial=6` | `[4]` |

The major artifact-shape difference is expected: `ours2` uses more `ALL_RINGBUFFER` /
`SHARED_SPM`, while `gemini2` mostly uses `ISOLATE_SPM`. Since the F2 perf stall occurs while
running the first method (`ours2`) before `gemini2`, the multi-method loop is not the first
suspect.

Local CPU/no-DMA summary-only dry-run with the same target and only the pipeline YAML swapped:

| method | result | model_compute_ns | run_ns | dma_submit_count | gemm_issue_count | gemm_fence_count | trace_summary_only | trace_event_count |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `ours2` | exit 0 | `22503326524` | `22528912251` | 0 | 320 | 320 | 1 | 0 |
| `gemini2` | exit 0 | `22486323411` | `22512025071` | 0 | 320 | 320 | 1 | 0 |

Trace files:

```text
tmp/pipeline-runtime-local-no-dma-audit-20260510/ours2.trace
tmp/pipeline-runtime-local-no-dma-audit-20260510/gemini2.trace
```

This is not F2 performance evidence. It does show that local artifact reading, no-DMA control flow,
summary trace emission, and the pluggable YAML method path are internally consistent. The remaining
high-probability F2 suspects are the FPGA backend/Gemmini instruction path under the no-GDB perf
profile, or the small trace timing changes since the known-good `9742924` checkpoint. The next F2
use should therefore be a known-good-like gdbserver thin-frontier run, not another blind no-GDB perf
rerun.
