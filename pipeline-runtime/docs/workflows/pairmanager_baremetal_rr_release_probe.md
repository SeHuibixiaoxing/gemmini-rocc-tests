# Pair-Manager Baremetal RR Release Probe

更新时间：`2026-04-19 10:35 UTC`

## 1. 目的

- 避开 `Linux + metasim` 的长启动路径。
- 在 **pair-manager 12-pair sbus128** 硬件语义下，
  用一个极小 baremetal workload
  直接验证：
  - `rr_release()` 后是否卡在 `RRCFG readback`
  - 或是否卡在紧邻的 `RROPC3 restore`

## 2. 关键文件

- baremetal 程序：
  `generators/gemmini/software/gemmini-rocc-tests/bareMetalC/learn-gemmini/rerocc_lc_pairmanager_rr_release_probe.c`
- host-init：
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/host-init-pairmanager-rr-release-probe.sh`
- workload：
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-pairmanager-rr-release-probe-tracerv-inst.json`
- local metasim runtime config：
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_pairmanager_rr_release_probe_tracerv_inst.yaml`

## 3. 默认编译语义

- `NUM_CORES=4`
- `NUM_GEMMINI=12`
- `NUM_DMA=12`
- `GEMMINI_BASE_ID=0`
- `DMA_BASE_ID=0`
- `PAIR_MANAGER_MODE=1`
- `cfg_id=0`
- `manager_id=0`
- `bytes=4096`
- `mode=2`
- `tracerv_markers=1`

## 4. 模式

- `mode=0`
  - `wait -> fence -> release -> RRCFG readback`
- `mode=1`
  - `raw release -> RRCFG readback`
- `mode=2`
  - `raw release -> immediate RROPC3 restore`
- `mode=3`
  - `wait -> fence -> release -> immediate RROPC3 restore`

当前默认优先 `mode=2`，
因为它最直接对应
“`restore-begin` 已经过，但 `RROPC3 csrrw` 可能不退休”。

## 5. TraceV 触发点

- start marker：
  `.word 0x00008013`
- end marker：
  `.word 0x00010013`

当前最终 ELF 已静态确认关键窗口：

- start marker：`0x80002228`
- `rr_release(cfg0)`：`0x8000223c`
- `RROPC3 restore`：`0x80002260`
- end marker：`0x80002274`

因此若 trace 只到 start marker 之后、restore 之前，
就能直接把 frontier 钉在这条 `RROPC3 csrrw`。

## 6. 构建

```bash
cd /home/ubuntu/chipyard
export PATH="/home/ubuntu/chipyard/.conda-env/riscv-tools/bin:$PATH"
bash generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/host-init-pairmanager-rr-release-probe.sh
```

修改模式时，例如：

```bash
cd /home/ubuntu/chipyard
export PATH="/home/ubuntu/chipyard/.conda-env/riscv-tools/bin:$PATH"
REROCC_RR_PROBE_MODE=1 \
bash generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/host-init-pairmanager-rr-release-probe.sh
```

## 7. 运行

按 FireSim manager 正规流：

```bash
cd /home/ubuntu/chipyard
scripts/firesim-tmux-run.sh \
  --session-name pairmanager-rr-probe-launchrunfarm \
  launchrunfarm \
  -c config_runtime_local_metasim_rerocc_lc_baremetal_pairmanager_rr_release_probe_tracerv_inst.yaml \
  -a config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml \
  -r sims/firesim-staging/sample_config_build_recipes.yaml
```

后续再按：

- `infrasetup`
- `runworkload`

## 8. 判据

- UART 最后一条若停在：
  - `before-readback`
    说明卡在 `RRCFG readback`
  - `before-restore-opc3`
    说明卡在 `RROPC3 restore`
- `TRACEFILE*` 若存在，
  直接对照上述 PC 窗口判断最后退休指令。
