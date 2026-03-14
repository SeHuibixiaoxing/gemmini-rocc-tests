#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--target 2c2g2d|4c4g4d|custom] [--matrix full|diagonal|single]"
  echo "          [--num-cores N] [--num-gemmini G] [--num-dma D] [--bytes B]"
  echo "          [--gemmini-base-id B] [--dma-base-id B]"
}

TARGET_PROFILE="${REROCC_TARGET:-${HW_TARGET:-2c2g2d}}"
MATRIX_MODE="${MATRIX_MODE:-${TEST_MATRIX:-full}}"

NUM_CORES="${NUM_CORES:-}"
NUM_GEMMINI="${NUM_GEMMINI:-}"
NUM_DMA="${NUM_DMA:-}"
BYTES="${BYTES:-65536}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"

while [ $# -gt 0 ]; do
  case "$1" in
    --target)
      TARGET_PROFILE="$2"
      shift 2
      ;;
    --matrix)
      MATRIX_MODE="$2"
      shift 2
      ;;
    --num-cores)
      NUM_CORES="$2"
      shift 2
      ;;
    --num-gemmini)
      NUM_GEMMINI="$2"
      shift 2
      ;;
    --num-dma)
      NUM_DMA="$2"
      shift 2
      ;;
    --bytes)
      BYTES="$2"
      shift 2
      ;;
    --gemmini-base-id)
      GEMMINI_BASE_ID="$2"
      shift 2
      ;;
    --dma-base-id)
      DMA_BASE_ID="$2"
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

case "${TARGET_PROFILE}" in
  2c2g2d|small)
    [ -n "${NUM_CORES}" ] || NUM_CORES=2
    [ -n "${NUM_GEMMINI}" ] || NUM_GEMMINI=2
    [ -n "${NUM_DMA}" ] || NUM_DMA=2
    ;;
  4c4g4d|default)
    [ -n "${NUM_CORES}" ] || NUM_CORES=4
    [ -n "${NUM_GEMMINI}" ] || NUM_GEMMINI=4
    [ -n "${NUM_DMA}" ] || NUM_DMA=4
    ;;
  custom)
    [ -n "${NUM_CORES}" ] || NUM_CORES=2
    [ -n "${NUM_GEMMINI}" ] || NUM_GEMMINI=2
    [ -n "${NUM_DMA}" ] || NUM_DMA=2
    ;;
  *)
    echo "Unknown target profile: ${TARGET_PROFILE}"
    print_usage
    exit 1
    ;;
esac

[ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID="${NUM_GEMMINI}"

case "${MATRIX_MODE}" in
  full) MATRIX_MODE_ID=0 ;;
  diagonal) MATRIX_MODE_ID=1 ;;
  single) MATRIX_MODE_ID=2 ;;
  *)
    echo "Unknown matrix mode: ${MATRIX_MODE}"
    print_usage
    exit 1
    ;;
esac

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
BUILD_CONFIG="${ARTIFACT_DIR}/build-config.txt"

echo "[rerocc-coupleddma-baremetal] target=${TARGET_PROFILE} matrix=${MATRIX_MODE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} bytes=${BYTES} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_MAX_CORES=${NUM_CORES} -DREROCC_NUM_GEMMINI=${NUM_GEMMINI} -DREROCC_NUM_DMA=${NUM_DMA} -DREROCC_GEMMINI_BASE_ID=${GEMMINI_BASE_ID} -DREROCC_DMA_BASE_ID=${DMA_BASE_ID} -DREROCC_DMA_BYTES=${BYTES} -DREROCC_MATRIX_MODE=${MATRIX_MODE_ID}"

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_matrix_baremetal_coupleddma-baremetal

echo "target=${TARGET_PROFILE}" > "${BUILD_CONFIG}"
echo "matrix=${MATRIX_MODE}" >> "${BUILD_CONFIG}"
echo "num_cores=${NUM_CORES}" >> "${BUILD_CONFIG}"
echo "num_gemmini=${NUM_GEMMINI}" >> "${BUILD_CONFIG}"
echo "num_dma=${NUM_DMA}" >> "${BUILD_CONFIG}"
echo "bytes=${BYTES}" >> "${BUILD_CONFIG}"
echo "gemmini_base_id=${GEMMINI_BASE_ID}" >> "${BUILD_CONFIG}"
echo "dma_base_id=${DMA_BASE_ID}" >> "${BUILD_CONFIG}"
