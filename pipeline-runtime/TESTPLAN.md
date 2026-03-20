# Pipeline Runtime Test Plan

## 1. 验收目标

最终验收固定为：

- 模型：`bertmini`
- 方法：`ours2 / gemini2 / tangram2`
- 目标平台：Linux on FireSim F2
- 目标硬件：`GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA`
- 判据：runtime 输出与 CPU golden 一致

当前验证层级固定为：

1. artifact export gate
2. host closure gate
3. Linux packaging gate
4. Linux coupleddma interface-reference gate
5. FireMarshal build/install gate
6. FireSim F2 replay gate

## 2. Artifact Export Gate

导出命令：

```bash
cd /home/ubuntu/chipyard
python3 conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py --model bertmini
```

至少检查这些文件存在：

- `conference/HybridMapper/output/pipeline_runtime/bertmini/model.layers.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.ours2.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.gemini2.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/pipeline_mapping.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.tangram2.yaml`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/runtime_model.bin`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/runtime_input.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.bin`
- `conference/HybridMapper/output/pipeline_runtime/bertmini/golden.rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024.ours2.bin`

通过标准：

- exporter 不报错
- runtime 需要的文件名齐全
- 没有回退到旧接口命名

## 3. Host Closure Gate

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
- 至少能确认 `ours2` 路径通过；完整基线应继续覆盖 `ours2 / gemini2 / tangram2`

## 4. Linux Packaging Gate

当前静态检查入口：

```bash
cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload
HOST_INIT_CHECK_ONLY=1 bash host-init.sh
```

若当前机器具备 RISC-V Linux 交叉编译器，则执行完整 staging：

```bash
cd /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload
bash host-init.sh
```

通过标准：

- `host-init.sh` 能完成 artifact presence 检查
- 若执行完整 staging，overlay 中出现：
  - `/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.<target_key>.yaml`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.<target_key>.<method>.yaml`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/runtime_model.bin`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/runtime_input.<target_key>.bin`
  - `/root/rerocc-linux-tests/pipeline-runtime/bertmini/golden.<target_key>.<method>.bin`
- 最终 binary 仍包含 `[prt-early] enter main` 这类早期进度字符串

## 5. Linux Coupleddma Interface-Reference Gate

凡是涉及 `prt_dma.c`、`prt_rerocc.c`、`GemminiCoupledDMA.scala`、`Controller.scala` 或相关 DMA/Gemmini 接口调用的改动，必须先对照这些 Linux 正例：

- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/rerocc_dma_matrix_linux_coupleddma.c`
- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/rerocc_lc_gemmini_matrix_linux_coupleddma.c`
- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/rerocc_lc_coverage_linux_coupleddma.c`
- `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/rerocc_lc_nonblocking_linux_coupleddma.c`

通过标准：

- 关键调用序列没有明显背离 Linux 正例
- 不在 `set_dst -> set_src -> wait` 临界区里长期保留重日志或额外 probe
- 涉及 Linux host DRAM <-> shared-SPM DMA 的改动时，额外满足：
  - 仍然按 host page 分 chunk
  - 每 chunk 单独做 `virt_to_phys`
  - `bytes >= 64` 且 `src_mod64 != dst_mod64` 时不 direct submit 原始 host pointer
  - DRAM path 不新增绕过 chunk helper 的 overlap single-request shortcut
- 具体 guardrails 与当前审计结论见：
  `docs/linux_dma_guardrails.md`

## 6. FireMarshal Build/Install Gate

必须在真实环境中执行：

```bash
cd /home/ubuntu/chipyard
source env.sh
marshal build generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json
marshal install generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json
```

通过标准：

- build/install 成功
- 失败时先区分环境问题与 workload 问题，不把沙箱/挂载异常误判成 runtime regression

## 7. FireSim F2 Replay Gate

进入 manager 环境：

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh
```

长任务统一通过 tmux wrapper，例如：

```bash
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh launchrunfarm -c config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml -a <hwdb> -r <build-recipes>
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh infrasetup -c config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml -a <hwdb> -r <build-recipes>
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh runworkload -c config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml -a <hwdb> -r <build-recipes>
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh terminaterunfarm --forceterminate -c config_runtime_f2_rerocc_lc_linux_bertmini_pipeline_runtime.yaml -a <hwdb> -r <build-recipes>
```

运行时证据优先查看：

- `sims/firesim/deploy/logs/`
- `sims/firesim/deploy/results-workload/`
- `tmp/firesim-aws-f2/tmux/`
- 若 manager 尚未回收结果，则到 run host 上查看：
  `/home/ubuntu/sim_slot_0/uartlog`
  和 `/home/ubuntu/sim_slot_0/heartbeat.csv`

通过标准：

- guest workload 真正进入 `rerocc_pipeline_runtime-linux`
- `uartlog` 中出现最终 PASS 标记，而不是只看 manager exit code
- 如果 run 明确卡死，先回收 run farm，再写分析结论
- 若本轮改动触及 Linux DMA 路径，先用 small Linux coupleddma regression 做快门：
  - `DMA_MATRIX_RESULT`
  - `CASE_RESULT dma_dram_to_shared_misaligned_fullpage`
  - `SCENARIO_RESULT name=conv_dma_parallel_nonblocking`
  - `NONBLOCKING_SUMMARY`
  都应先过，再继续跑 bertmini / pipeline-runtime

## 8. 结果采信规则

- `uartlog` 是最终行为证据；`heartbeat.csv` 只用于判断 guest 是否仍在推进
- 慢启动不是 blocker；没有明确报错时不要过早把 Linux boot 判成 stuck
- 当前 live blocker、最新停点与下一轮实验，统一维护在：
  `conference/mudnac_hybridmapper_collab_docs/STATUS.md`
  和 `NEXT_SESSION_PROMPT.md`
