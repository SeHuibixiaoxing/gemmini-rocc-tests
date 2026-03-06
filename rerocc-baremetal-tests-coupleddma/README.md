# ReRoCC coupled-DMA baremetal workload

This workload builds:

- `rerocc_lc_matrix_baremetal_coupleddma.riscv`

It validates:

- DMA copies for `dram->shared`, `shared->dram`, and `shared->shared`
- Gemmini `conv`, `resadd`, and shared-window `mvin/mvout`

Build entrypoint:

- `rerocc-baremetal-tests-coupleddma/workload/host-init.sh`
