# ReRoCC baremetal workload (for FireSim metasim)

This workload builds a single baremetal binary:

- `rerocc_lc_matrix_baremetal.riscv`

The binary runs both matrix tests:

- Gemmini ReRoCC matrix test (`custom3`)
- DirectDMA ReRoCC matrix test (`custom2`)

## FireMarshal configs

- `marshal-config/rerocc_lc_baremetal_small.json` (default 2c2g2d, full matrix)
- `marshal-config/rerocc_lc_baremetal.json` (default 4c4g4d, full matrix)
- `marshal-config/rerocc_lc_baremetal_custom.json` (custom target)

## Customization knobs

`rerocc-baremetal-tests/workload/host-init.sh` supports:

- `--target 2c2g2d|4c4g4d|custom`
- `--matrix full|diagonal|single`
- `--num-cores N --num-gemmini G --num-dma D`
- `--bytes B --gemmini-base-id B --dma-base-id B`

You can also pass the same values via environment variables:

- `REROCC_TARGET`, `MATRIX_MODE`
- `NUM_CORES`, `NUM_GEMMINI`, `NUM_DMA`
- `BYTES`, `GEMMINI_BASE_ID`, `DMA_BASE_ID`

## Minimal patch note: true multi-hart parallel/distributed execution

### Scope

- File: `bareMetalC/learn-gemmini/rerocc_lc_matrix_baremetal.c`
- Keep the original tight-coupled test paths untouched; only extend ReRoCC baremetal matrix test behavior.

### Key changes

- Enable true multi-hart participation in `thread_entry(cid, nc)`:
  - all `cid < logical_cores` harts execute workload (instead of only hart0)
  - each hart executes its own CPU row of Gemmini/DMA manager matrix cases
- Add per-hart result buckets and global aggregation:
  - `core_done[]`, `core_gemmini_pass/fail[]`, `core_dma_pass/fail[]`
  - hart0 waits for all active harts then prints `CORE_RESULT` and global matrix summary
- Isolate DMA completion flag per hart:
  - use `dma_complete_flag[cid]` so concurrent DMA checks do not interfere
- Improve robustness under contention:
  - use acquire-with-retry for ReRoCC config acquisition in Gemmini and DMA paths
- Make logical core count default to runtime hart count:
  - `REROCC_LOGICAL_CORES=0` means `logical_cores = nc`

### Build + run commands (metasim baremetal)

```bash
source ~/.ssh/AGENT_VARS
cd /home/wzy/proj/wp2/chipyard/sims/firesim
source ./sourceme-manager.sh --skip-ssh-setup

cd /home/wzy/proj/wp2/chipyard/software/firemarshal
./marshal -v build /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/marshal-config/rerocc_lc_baremetal_custom.json

cd /home/wzy/proj/wp2/chipyard/sims/firesim/deploy
firesim runworkload -c config_runtime_rerocc_metasim_small_baremetal.yaml -a config_hwdb.yaml -r config_build_recipes.yaml
```

### Pass criteria

- Per-hart summary lines exist for all active harts:
  - `CORE_RESULT cid=0 ...`
  - `CORE_RESULT cid=1 ...` (for 2-core config)
- Global matrix counts match expected full matrix:
  - `GEMMINI_MATRIX_RESULT mode=full pass=4 fail=0 expected=4`
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=65536`
- Final verdict:
  - `ALL_TESTS_PASS`

### Latest validation snapshot (2026-03-04)

- UART log: `/home/wzy/proj/wp2/chipyard/tmp/FIRESIM_RUNS_DIR/sim_slot_0/uartlog`
- Extracted result:
  - `CORE_RESULT cid=0 gemmini_pass=2 gemmini_fail=0 dma_pass=2 dma_fail=0`
  - `CORE_RESULT cid=1 gemmini_pass=2 gemmini_fail=0 dma_pass=2 dma_fail=0`
  - `GEMMINI_MATRIX_RESULT mode=full pass=4 fail=0 expected=4`
  - `DMA_MATRIX_RESULT mode=full pass=4 fail=0 expected=4 bytes=65536`
  - `ALL_TESTS_PASS`
