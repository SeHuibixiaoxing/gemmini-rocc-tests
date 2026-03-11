# ReRoCC coupled-DMA baremetal workload

This workload builds:

- `rerocc_lc_matrix_baremetal_coupleddma.riscv`
- `rerocc_lc_coverage_baremetal_coupleddma.riscv`
- `rerocc_lc_nonblocking_baremetal_coupleddma.riscv`

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
