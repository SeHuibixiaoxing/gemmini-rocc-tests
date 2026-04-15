# Pipeline Runtime Breadcrumb Mechanism

日期：2026-04-13

## 目标

这套机制的目标不是替代所有日志，而是替代最容易扰动时序的热路径文本日志。

当前原则：

- 粗粒度进度仍然保留在 `bertmini-batch8.log`
- 热路径不再依赖 `snprintf + write(O_APPEND)` 文本打点
- 热路径改为写入固定大小的内存映射 breadcrumb 文件
- wrapper 继续通过周期性 `sync` 把该文件刷到 guest 文件系统
- host 通过 `debugfs` 把 breadcrumb 文件拉回本地，再用 decode 脚本解析

## 为什么要改

之前多轮排查里，DMA submit/wait、`spm-xlate flush/release` 这些位置反复出现“卡点前移”或“半行日志”现象。根因不是门控条件不够多，而是热路径日志本身仍然要走阻塞文件追加写。

相关旧路径在 [prt_progress.h](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_progress.h#L158) 到 [prt_progress.h](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_progress.h#L235)：

- guest log / checkpoint log 都是 `open(... O_APPEND ...)`
- 热路径日志本质上仍然会执行 `write()`

因此，即使 probe 条件已经缩得很窄，只要命中的是 submit/wait 这类 hot path，日志本身仍然可能改变 guest 时序。

## 新机制概要

### 1. 存储介质

breadcrumb 文件默认路径：

- `/root/pipeline-runtime-debug/bertmini-batch8.breadcrumb.bin`

它由 runtime 在初始化时：

- `open + ftruncate`
- `mmap(MAP_SHARED)`
- 写入固定大小头部和 slot 数组

热路径之后只更新内存里的固定字段，不再执行文本格式化和追加写。

### 2. 结构格式

定义在：

- [prt_breadcrumb.h](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include/prt_breadcrumb.h)

文件头 `prt_breadcrumb_file_t` 记录：

- magic
- version
- slot_count
- 过滤条件
- `update_count`
- `last_slot_idx`
- `last_kind`
- `last_phase`

每个 slot `prt_breadcrumb_slot_t` 记录：

- `seq`
- `mono_ns`
- `kind`
- `phase`
- `segment/global_stage/local_stage/subbatch`
- `tensor_id`
- `token_id`
- `manager_id`
- `page_idx`
- `rc`
- `flags`
- `src_addr`
- `dst_addr`
- `aux_u64_0`
- `aux_u64_1`

### 3. 一致性策略

每个 slot 使用一个简单的 seq 双写协议：

1. 更新开始前，`seq` 加 1，变成奇数
2. 写入所有字段
3. 更新结束后，`seq` 再加 1，变成偶数

读取时应只信任：

- `seq != 0`
- 且 `seq` 为偶数

这样可以避免读取到半更新状态。

## 当前覆盖范围

### DMA 热路径

当前已接入：

- `dma page begin/end`
- `dma submit begin`
- `dma rr postcheck`
- `dma doneflag end`
- `dma program begin`
- `dma program post fence`
- `dma program post dst`
- `dma program post src`
- `dma wait before fence`
- `dma wait after fence`
- `dma wait after shared fence`
- `dma wait after release`
- `dma wait after complete`
- `dma wait after trace complete`
- `dma submitwait after wait`
- `dma submitwait after cleanup`

相关代码：

- [prt_dma.c](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c)

其中 page 级上下文不是靠字符串传递，而是通过 thread-local transfer context 传入：

- 当前 chunk 的 `page_idx`
- 当前 `src/dst`
- 当前 `bytes`
- 当前 `manager_id`

这样 submit/wait 内层 breadcrumb 可以自动带上页号，不需要每个 phase 再打文本日志。

### SPM xlate 热路径

当前已接入：

- `flush begin/end`
- `release fence begin/end`
- `release begin/end`
- `restore begin/end`

相关代码：

- [prt_rerocc.c](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c)

### RR acquire 关键边界

当前已接入：

- `rr acquire before/after csr write`
- `rr acquire after csr read`
- `rr acquire before set-opc`
- `rr acquire after set-opc`
- `rr acquire before return`
- `rr acquire wrapper after-call`

这部分专门用于替代过去最容易出现半行日志的：

- `rr-acquire-inner phase=after-csr-write`
- `rr-acquire-inner phase=after-csr-read`

对于 `rr` breadcrumb：

- `tok` 字段复用为 `cfg_id`
- `src` 字段记录 `csr_id`
- `dst` 字段记录本次写入的 `wdata` 或 `cfg_id`
- `aux0` 记录最近一次 `cfg_state`
- `aux1` 记录 `retries`

## 过滤机制

breadcrumb 有独立过滤条件，不复用文本 deep log 的 enable 开关。

当前支持的 guest env：

- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE`
- `PIPELINE_RUNTIME_BREADCRUMB_PATH`
- `PIPELINE_RUNTIME_BREADCRUMB_SEGMENT`
- `PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE`
- `PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE`
- `PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH`
- `PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS`
- `PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS`

当前 `pairdummy_sbus128_fixed_env.sh` 的默认策略是：

- 开启 breadcrumb
- 只聚焦 `segment=0`
- 只聚焦 `global_stage=0`
- 只聚焦 `local_stage=0`
- 不限制 subbatch

对应配置文件：

- [pairdummy_sbus128_fixed_env.sh](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_fixed_env.sh)

## 当前固定调试策略

为降低扰动，当前 fixed profile 已切到以下模式：

- `PIPELINE_RUNTIME_PROGRESS_RAW=0`
- `PIPELINE_RUNTIME_PROGRESS_HOT=0`
- `PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START=36`
- `PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END=37`
- `PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE=0`
- `PIPELINE_RUNTIME_DMA_FIXED_LOAD_CHECKPOINT_ENABLE=0`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=1`

也就是说：

- 粗粒度 `guest sparse log` 仍保留
- submit/wait 类热路径文本日志默认关闭
- export 仅在 `page36..37` 这个窄窗口额外打印页级日志
- 细粒度边界依赖 breadcrumb

## 文件链路

### guest env 透传

host-init 白名单已加入 breadcrumb 变量和 fixed-load probe 变量：

- [host-init.sh](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/host-init.sh)

runner 已加入默认值、echo 摘要和 runtime 进程环境透传：

- [run_rerocc_pipeline_runtime_bertmini.sh](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh)

### wrapper / status

capture wrapper 现在会：

- 在 status 里记录 breadcrumb path 和 enable
- 每轮启动前删除旧 breadcrumb 文件

相关代码：

- [run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh)

### workload outputs

FireMarshal workload outputs 已加入 breadcrumb 文件：

- [rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json)

### host watchdog capture

timeout capture 现在会额外抓取：

- `*.guest-breadcrumb.bin`

相关代码：

- [firesim-prt-host-watchdog.sh](/home/ubuntu/chipyard/scripts/firesim-prt-host-watchdog.sh)

## 本地解析方法

decode 脚本：

- [decode_prt_breadcrumb.py](/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/decode_prt_breadcrumb.py)

典型流程：

```bash
ssh -i /home/ubuntu/firesim.pem ubuntu@<private-ip> \
  "sudo debugfs -R 'cat /root/pipeline-runtime-debug/bertmini-batch8.breadcrumb.bin' \
   '/home/ubuntu/sim_slot_0/<image>.img' 2>/dev/null" \
  > /tmp/bertmini-batch8.breadcrumb.bin

python3 /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/decode_prt_breadcrumb.py \
  /tmp/bertmini-batch8.breadcrumb.bin
```

如果是 watchdog 自动抓到的 capture：

```bash
python3 /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/decode_prt_breadcrumb.py \
  /home/ubuntu/chipyard/tmp/firesim-aws-f2/captures/<capture-prefix>.guest-breadcrumb.bin
```

## 解释规则

### DMA

常见判断方法：

- 最后 phase 是 `dma_doneflag_end`
  说明至少已经拿到 doneflag 物理地址
- 最后 phase 是 `dma_program_begin`
  说明卡点收敛到 submit 编程窗口
- 最后 phase 是 `dma_wait_before_fence`
  说明 submit 已经返回，正在进入 wait
- 最后 phase 是 `dma_wait_after_fence`
  说明硬件 fence 已经返回，后面只剩 shared fence / release

### SPM xlate

- 最后 phase 是 `spm_xlate_flush_begin`
  说明卡在 flush 指令发出前后
- 最后 phase 是 `spm_xlate_release_fence_begin`
  说明 flush 已过，卡在 release fence 邻域
- 最后 phase 是 `spm_xlate_restore_begin`
  说明 release 已过，卡在 opcode restore 邻域

## 当前约束

- 以后不要再在 DMA submit/wait、`spm-xlate flush/release` 热路径里继续堆新的文本日志
- 需要更细边界时，优先扩展 breadcrumb phase/field，而不是重新加 `PRT_PROGRESS_LOG`
- 粗粒度日志只保留阶段边界，不再承担 hot-path 单步可见性
