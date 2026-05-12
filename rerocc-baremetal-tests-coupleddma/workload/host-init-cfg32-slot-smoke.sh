#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--num-cores N] [--manager-id N] [--cfg-id N] [--dma-bytes B]"
  echo "          [--wait-spins N] [--boundary-sweep 0|1]"
  echo "          [--tracerv-markers 0|1] [--skip-gemmini-data-check 0|1]"
}

NUM_CORES="${NUM_CORES:-4}"
MANAGER_ID="${CFG32_SLOT_MANAGER_ID:-0}"
CFG_ID="${CFG32_SLOT_QUICK_CFG_ID:-16}"
DMA_BYTES="${CFG32_SLOT_DMA_BYTES:-64}"
WAIT_SPINS="${CFG32_SLOT_WAIT_SPINS:-20000000}"
BOUNDARY_SWEEP="${CFG32_SLOT_BOUNDARY_SWEEP:-1}"
TRACERV_MARKERS="${CFG32_SLOT_TRACERV_MARKERS:-1}"
SKIP_GEMMINI_DATA_CHECK="${CFG32_SLOT_SKIP_GEMMINI_DATA_CHECK:-0}"
OUTPUT_BASENAME="${CFG32_SLOT_OUTPUT_BASENAME:-rerocc_lc_cfg32_slot_smoke.riscv}"
OUTPUT_TAG="${CFG32_SLOT_OUTPUT_TAG:-cfg32-slot-smoke}"

while [ $# -gt 0 ]; do
  case "$1" in
    --num-cores)
      NUM_CORES="$2"
      shift 2
      ;;
    --manager-id)
      MANAGER_ID="$2"
      shift 2
      ;;
    --cfg-id)
      CFG_ID="$2"
      shift 2
      ;;
    --dma-bytes)
      DMA_BYTES="$2"
      shift 2
      ;;
    --wait-spins)
      WAIT_SPINS="$2"
      shift 2
      ;;
    --boundary-sweep)
      BOUNDARY_SWEEP="$2"
      shift 2
      ;;
    --tracerv-markers)
      TRACERV_MARKERS="$2"
      shift 2
      ;;
    --skip-gemmini-data-check)
      SKIP_GEMMINI_DATA_CHECK="$2"
      shift 2
      ;;
    --help)
      print_usage
      exit 0
      ;;
    *)
      echo "Unknown arg: $1"
      print_usage
      exit 1
      ;;
  esac
done

if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
  echo "riscv64-unknown-elf-gcc not found in PATH"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_BAREMETAL_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_BAREMETAL_DIR}" && pwd)"
BUILD_DIR="${GEMMINI_ROCC_TESTS_DIR}/build"
BUILD_BAREMETAL_DIR="${BUILD_DIR}/bareMetalC"
ARTIFACT_DIR="${BUILD_DIR}/rerocc-baremetal-tests-coupleddma"
BUILD_CONFIG="${ARTIFACT_DIR}/build-config-${OUTPUT_TAG}.txt"
OUT_BIN="${SCRIPT_DIR}/${OUTPUT_BASENAME}"

echo "[rerocc-cfg32-slot-smoke] num_cores=${NUM_CORES} manager_id=${MANAGER_ID} cfg_id=${CFG_ID} dma_bytes=${DMA_BYTES} wait_spins=${WAIT_SPINS} boundary_sweep=${BOUNDARY_SWEEP} tracerv_markers=${TRACERV_MARKERS} skip_gemmini_data_check=${SKIP_GEMMINI_DATA_CHECK} output=${OUT_BIN}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_PAIR_MANAGER_MODE=1"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_MANAGER_ID=${MANAGER_ID}"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_QUICK_CFG_ID=${CFG_ID}"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_DMA_BYTES=${DMA_BYTES}"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_WAIT_SPINS=${WAIT_SPINS}"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_BOUNDARY_SWEEP=${BOUNDARY_SWEEP}"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_TRACERV_MARKERS=${TRACERV_MARKERS}"
EXTRA_DEFS="${EXTRA_DEFS} -DCFG32_SLOT_SKIP_GEMMINI_DATA_CHECK=${SKIP_GEMMINI_DATA_CHECK}"

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_cfg32_slot_smoke-baremetal

cp -f "${BUILD_BAREMETAL_DIR}/rerocc_lc_cfg32_slot_smoke-baremetal" "${OUT_BIN}"

echo "num_cores=${NUM_CORES}" > "${BUILD_CONFIG}"
echo "manager_id=${MANAGER_ID}" >> "${BUILD_CONFIG}"
echo "cfg_id=${CFG_ID}" >> "${BUILD_CONFIG}"
echo "dma_bytes=${DMA_BYTES}" >> "${BUILD_CONFIG}"
echo "wait_spins=${WAIT_SPINS}" >> "${BUILD_CONFIG}"
echo "boundary_sweep=${BOUNDARY_SWEEP}" >> "${BUILD_CONFIG}"
echo "tracerv_markers=${TRACERV_MARKERS}" >> "${BUILD_CONFIG}"
echo "skip_gemmini_data_check=${SKIP_GEMMINI_DATA_CHECK}" >> "${BUILD_CONFIG}"
echo "binary=${OUT_BIN}" >> "${BUILD_CONFIG}"
