# 20260512T044151Z 1C1P uartprobe manager0 wrapper

## Change

- Updated the 1C1P hwdebug Linux uartprobe wrapper defaults:
  - `DMA_BASE_ID`: `1 -> 0`
  - `UARTPROBE_DMA_MANAGER_ID`: `1 -> 0`

## File

- `rerocc-linux-tests-coupleddma/workload/run_rerocc_dma_export_alias_uartprobe_1c1p1_hwdebug_capture.sh`

## Reason

- 1C1P pair-manager hardware has only manager id `0`.
- DMA and Gemmini are distinguished inside the pair wrapper by the instruction/custom path, not by assigning DMA to logical manager id `1`.
- The previous Linux uartprobe run used stale/default manager id `1` and failed at acquire:
  `acquire timeout cfg=1 dma_mgr=1`.

## Validation so far

- FireMarshal clean/build/install completed through the required wrapper.
- Local guest image `/firemarshal.sh` was verified with `debugfs` and contains:
  - `DMA_BASE_ID="${DMA_BASE_ID:-0}"`
  - `UARTPROBE_DMA_MANAGER_ID="${UARTPROBE_DMA_MANAGER_ID:-0}"`
- Local image SHA:
  `2bdf904ba624492162ecce9684262759a2bf7f05db73107cc324f19ae7fe1e40`

## Pending validation

- Fresh F2 `infrasetup` must pass `+check-fingerprint`.
- Remote guest image freshness must be rechecked on the fresh run host.
- Linux uartprobe workload must be rerun and judged by guest files plus UART, not by `doneflag`.
