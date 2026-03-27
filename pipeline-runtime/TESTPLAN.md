# Pipeline Runtime Test Plan

## 1. 本轮重构的通过标准

这轮不是最终 F2 correctness closure；本轮通过标准是：

1. 全量重编通过
2. CLI sanity 通过
3. host closure 最小子集通过
4. 代码层面确认旧的 runtime-global topology/execution fields 已不再被使用

当前临时策略：

- 暂时先不做多 action 验证
- 先恢复之前的 `bertmini` 单 action 主线

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

### Gate E. Full Host Closure

```bash
cd /home/ubuntu/chipyard
BATCH=1 SKIP_BUILD=1 SKIP_EXPORT=1 \
bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
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

1. 先跑之前的 `bertmini` baremetal/host 路径，确认单 action 主线仍通
2. 再跑 Linux 路径，继续盯旧 `segment0/stage0/pointwise` 卡点
3. 最后再开 FireMarshal/F2
4. 多 action 验证暂缓到 `bertmini` 主线恢复之后

原因：

- Linux/F2 启动成本高
- 这轮首先要验证新 runtime ownership 是否消除了旧的全局状态污染

## 4. F2 纪律

- `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- 确认卡死后立刻停 farm
- 判断卡点时联查 `uartlog` 与 `heartbeat.csv`
- guest 输出必须 unbuffered
