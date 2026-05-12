# Pair-Manager Baremetal RR Release Probe Plan

更新时间：`2026-04-19 11:10 UTC`

## 目标

- 不再走 `Linux + metasim`。
- 使用 **pair-manager 硬件** + **baremetal workload** + **local metasim**，
  快速验证：
  - `rr_release()` 返回点之后，是否卡在紧邻的 `RRCFG readback`
  - 或者卡在紧邻的 `RROPC3 restore`
- 同时预埋 `TraceV selector=3` instruction marker，
  让后续能直接看到 PC 是否停在目标 CSR 指令。

## 设计

- workload：`rerocc_lc_pairmanager_rr_release_probe`
- 配置固定到主线 pair-manager 语义：
  - `NUM_CORES=4`
  - `NUM_GEMMINI=12`
  - `NUM_DMA=12`
  - `GEMMINI_BASE_ID=0`
  - `DMA_BASE_ID=0`
  - `PAIR_MANAGER_MODE=1`
- 指令序列只保留：
  - acquire cfg
  - `RROPC3 <- cfg`
  - `RROPC2 <- cfg`
  - 一条极小 DMA
  - `rr_release` / `RRCFG readback` / `RROPC3 restore`

## 默认模式

- 默认编译模式：`mode=2`
  - `raw release -> immediate RROPC3 restore`
- 备选模式：
  - `mode=0`: `wait -> fence -> release -> readback`
  - `mode=1`: `raw release -> readback`
  - `mode=3`: `wait -> fence -> release -> restore`

## 执行原则

- 先用 `TraceV instruction trigger` 看最小窗口。
- 若 trace 明确卡在 `before-readback` 之后：
  说明卡在 `RRCFG readback`
- 若 trace 明确卡在 `before-restore-opc3` 之后：
  说明卡在 `RROPC3 csrrw`

## 后续运行入口

- workload source:
  `generators/gemmini/software/gemmini-rocc-tests/rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-pairmanager-rr-release-probe-tracerv-inst.json`
- runtime config:
  `sims/firesim/deploy/config_runtime_local_metasim_rerocc_lc_baremetal_pairmanager_rr_release_probe_tracerv_inst.yaml`
