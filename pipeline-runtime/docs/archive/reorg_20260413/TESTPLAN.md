# Pipeline Runtime Test Plan

## 1. 本轮通过标准

这轮不是最终 F2 correctness closure；当前通过标准是：

1. 全量重编通过
2. CLI sanity 通过
3. host closure / host watchdog / dual-log 链路可用
4. 代码层面确认当前关注路径和日志配置已经静态对齐

当前临时策略：

- 暂时不做多 action 验证
- 先恢复 `bertmini` 单 action主线
- Linux/F2 很贵，新的 mixed-log 早期边界必须先静态解释

## 2. 推荐验证阶梯

### Gate A. Artifact Export

```bash
cd /home/ubuntu/chipyard
python3 conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py --model bertmini
```

### Gate B. Native Clean Build

```bash
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j4
```

### Gate C. CLI Sanity

```bash
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime --help
```

### Gate D. Minimal Host Closure

```bash
cd /home/ubuntu/chipyard
METHODS=ours2 BATCH=1 SKIP_BUILD=1 SKIP_EXPORT=1 \
bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

### Gate E. Host Watchdog / Wrapper Sanity

```bash
cd /home/ubuntu/chipyard
bash -n scripts/firesim-prt-host-watchdog.sh
bash -n generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_capture.sh
```

### Gate F. Static No-Regression Search

```bash
cd /home/ubuntu/chipyard
rg -n "rt->(stage_thread_count|stage_threads\\[|stage_layer_ids\\[|stage_acc_ids\\[|stage_dma_ids\\[|stage_tile_counts\\[|stage_split_kinds\\[|stage_mgr_ids\\[|stage_spm_rebase_vpage\\[|stage_spm_window_pages\\[|stage_spm_shadow\\[|stage_spm_shadow_bytes\\[|stage_dma_bounce\\[|stage_dma_bounce_bytes\\[|stage_fixed_lazy_loaded\\[|pipebufs\\[|pipebuf_count|ringbufs\\[|ringbuf_count|isolate_pairs\\[|isolate_pair_count|shared_pairs\\[|shared_pair_count|topo_alloc_keys\\[|topo_alloc_count|topo_alloc_cap|topo_weight_pages\\[|topo_weight_count|topo_weight_cap)" \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/include -S
```

应当无匹配。

## 3. 下一轮实验顺序

1. 先静态复核并拆开两条边界：
   - 主线 file-heavy：
     `segment=31 / resadd_spatial / scope-drain`
   - mixed-log dual-log：
     `segment=2 / export-sync / DMA export`
2. 先审查代码路径：
   - `sync_stage_export_aliases()`
   - `copy_tensor_pages_to_model_aliases()`
   - `prt_dma_copy_spm_pages_to_dram()`
   - `dma_copy_spm_pages_to_host_linux()`
   - `dma_submit_wait_annotated()`
3. 再对比 mixed-log
   与 file-heavy
   的配置差异：
   - sparse UART
   - guest deep file
   - periodic sync
   - host watchdog
4. 如果静态分析后可以构造便宜的 export-DMA repro，
   优先 baremetal/F2；
   否则再做下一轮 Linux/F2
   - 当前已有两条 cheap baremetal gate：
     - `host-init-export-dma-bertmini-repro.sh`
       只压 `shared-spad -> alias` 重复 export
     - `host-init-export-dma-bertmini-segment3-repro.sh`
       更贴近 `segment=3`
       `export-sync addr0 -> export-sync addr1 -> c2-flush slot0(addr0)`
5. 如果再跑 Linux/F2，
   必须让 host watchdog
   自动超时收口，
   不要人工提前打断
6. 联合使用
   `heartbeat + uartlog + guest sparse/deep/status`
   判定是否真正前进
7. 多 action 验证继续暂缓到单 action 主线恢复之后

## 4. F2 纪律

- `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- 判断卡点时联查
  `uartlog`
  `heartbeat.csv`
  与 guest
  `sparse/deep/status`
- guest 输出必须 unbuffered
- 默认继续用 host watchdog
  自动抓证据与自动回收；
  不要人工提前停 farm
- 当前 batch8 dual-log workload 默认 deep-log gate 先保持：

```text
--deep-log-enable 1
--deep-log-segment 3
--deep-log-global-stage 3
--deep-log-local-stage 0
--deep-log-subbatch 0
```

- 新 replay 上先看：
  - `conv-rt`
  - `wrkrdy`
  - `wrk-tr-b/e`
  - `wrk-ev-b/e`
  - `wrk-issue`
  - `gmi`
  - `gic-*`
  - `wrk-exit`
  - `export-sync ... begin/end`
- 只有重新推进回主线更深的
  `stage=1 / global_stage=4`
  或
  `segment=31`
  后，再主盯：
  - `gf-aq-*`
  - `gf-rf-*`
  - `gf-dr-*`
  - `gf-rl-*`
