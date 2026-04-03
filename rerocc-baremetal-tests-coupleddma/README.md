# ReRoCC coupled-DMA baremetal workload

This workload builds:

- `build/bareMetalC/rerocc_lc_matrix_baremetal_coupleddma-baremetal`
- `build/bareMetalC/rerocc_lc_coverage_baremetal_coupleddma-baremetal`
- `build/bareMetalC/rerocc_lc_nonblocking_baremetal_coupleddma-baremetal`
- `build/bareMetalC/rerocc_lc_export_dma_bertmini_repro-baremetal`
- `build/bareMetalC/rerocc_lc_export_dma_bertmini_segment3_repro-baremetal`
- build metadata under `build/rerocc-baremetal-tests-coupleddma/`

It validates:

- DMA copies for `dram->shared`, `shared->dram`, and `shared->shared`
- Gemmini `conv`, `resadd`, and shared-window `mvin/mvout`
- shared-spad cross-page access paths (1KB page boundary) for:
  - Gemmini shared input/weight/output tensors
  - CoupledDMA shared source/destination windows
- non-blocking overlap scenarios (`conv` vs `dma`, `resadd` vs `dma`, cross-manager parallel issue)

Build entrypoint:

- `rerocc-baremetal-tests-coupleddma/workload/host-init.sh`
- `rerocc-baremetal-tests-coupleddma/workload/host-init-coverage.sh`
- `rerocc-baremetal-tests-coupleddma/workload/host-init-nonblocking.sh`
- `rerocc-baremetal-tests-coupleddma/workload/host-init-export-dma-bertmini-repro.sh`
- `rerocc-baremetal-tests-coupleddma/workload/host-init-export-dma-bertmini-segment3-repro.sh`

Export repro entrypoint:

- workload json:
  `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-export-dma-bertmini-repro.json`
- built payload:
  `rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_export_dma_bertmini_repro.riscv`
- default shape:
  two worker cores, one `512KiB` export lane plus one `64KiB` concurrent export lane

Segment-3 repro entrypoint:

- workload json:
  `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-export-dma-bertmini-segment3-repro.json`
- built payload:
  `rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_export_dma_bertmini_segment3_repro.riscv`
- default shape:
  two worker cores; stage0 executes a `bertmini segment=3`-shaped sequence
  `export-sync addr0 -> export-sync addr1 -> c2-flush slot0(addr0)`
  on one `512KiB` tensor, while stage1 keeps a concurrent `64KiB` export lane active

Metasim baremetal suite entrypoint:

- `sims/firesim/deploy/run_rerocc_coupleddma_baremetal_metasim_suite.sh`

Run directory policy:

- Every test uses its own `FIRESIM_RUNS_DIR` under `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR`.
- Override root with `FIRESIM_RUNS_ROOT=/path/to/root`.
- Optional tags:
  - `SUITE_TAG=...` for `run_rerocc_coupleddma_baremetal_metasim_suite.sh`
  - `GATE_TAG=...` for `run_rerocc_coupleddma_validation_gates.sh`
- Optional watchdog knobs (both scripts):
  - `RUN_TIMEOUT_SECS` hard timeout for `firesim runworkload`
  - `STALL_TIMEOUT_SECS` fail fast if `uartlog/heartbeat` stops updating
  - `MONITOR_POLL_SECS` activity check period
