#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--target 2c2g2d|4c4g4d|custom]"
  echo "          [--num-cores N] [--num-gemmini G] [--num-dma D] [--bytes B]"
  echo "          [--gemmini-base-id B] [--dma-base-id B]"
  echo "          [--long-conv-iters N] [--short-conv-iters N]"
  echo "          [--long-resadd-iters N] [--long-dma-iters N] [--short-dma-iters N]"
}

TARGET_PROFILE="${REROCC_TARGET:-${HW_TARGET:-2c2g2d}}"
NUM_CORES="${NUM_CORES:-}"
NUM_GEMMINI="${NUM_GEMMINI:-}"
NUM_DMA="${NUM_DMA:-}"
BYTES="${BYTES:-2048}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"

LONG_CONV_ITERS="${LONG_CONV_ITERS:-64}"
SHORT_CONV_ITERS="${SHORT_CONV_ITERS:-4}"
LONG_RESADD_ITERS="${LONG_RESADD_ITERS:-512}"
LONG_DMA_ITERS="${LONG_DMA_ITERS:-64}"
SHORT_DMA_ITERS="${SHORT_DMA_ITERS:-4}"

while [ $# -gt 0 ]; do
  case "$1" in
    --target)
      TARGET_PROFILE="$2"
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
    --long-conv-iters)
      LONG_CONV_ITERS="$2"
      shift 2
      ;;
    --short-conv-iters)
      SHORT_CONV_ITERS="$2"
      shift 2
      ;;
    --long-resadd-iters)
      LONG_RESADD_ITERS="$2"
      shift 2
      ;;
    --long-dma-iters)
      LONG_DMA_ITERS="$2"
      shift 2
      ;;
    --short-dma-iters)
      SHORT_DMA_ITERS="$2"
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

if ! command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
  echo "riscv64-unknown-elf-gcc not found in PATH"
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_BAREMETAL_DIR="$(cd "${SCRIPT_DIR}/../.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_BAREMETAL_DIR}" && pwd)"
BUILD_DIR="${GEMMINI_ROCC_TESTS_DIR}/build"
BUILD_BAREMETAL_DIR="${BUILD_DIR}/bareMetalC"
OUT_BIN="${SCRIPT_DIR}/rerocc_lc_nonblocking_baremetal_coupleddma.riscv"

echo "[rerocc-coupleddma-nonblocking] target=${TARGET_PROFILE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} bytes=${BYTES} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID}"
echo "[rerocc-coupleddma-nonblocking] long_conv=${LONG_CONV_ITERS} short_conv=${SHORT_CONV_ITERS} long_resadd=${LONG_RESADD_ITERS} long_dma=${LONG_DMA_ITERS} short_dma=${SHORT_DMA_ITERS}"

mkdir -p "${BUILD_BAREMETAL_DIR}"

EXTRA_DEFS="-DREROCC_NUM_GEMMINI=${NUM_GEMMINI} -DREROCC_NUM_DMA=${NUM_DMA} -DREROCC_GEMMINI_BASE_ID=${GEMMINI_BASE_ID} -DREROCC_DMA_BASE_ID=${DMA_BASE_ID} -DREROCC_DMA_BYTES=${BYTES} -DREROCC_LONG_CONV_ITERS=${LONG_CONV_ITERS} -DREROCC_SHORT_CONV_ITERS=${SHORT_CONV_ITERS} -DREROCC_LONG_RESADD_ITERS=${LONG_RESADD_ITERS} -DREROCC_LONG_DMA_ITERS=${LONG_DMA_ITERS} -DREROCC_SHORT_DMA_ITERS=${SHORT_DMA_ITERS}"

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_nonblocking_baremetal_coupleddma-baremetal

cp -f "${BUILD_BAREMETAL_DIR}/rerocc_lc_nonblocking_baremetal_coupleddma-baremetal" "${OUT_BIN}"

echo "target=${TARGET_PROFILE}" > "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "num_cores=${NUM_CORES}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "num_gemmini=${NUM_GEMMINI}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "num_dma=${NUM_DMA}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "bytes=${BYTES}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "gemmini_base_id=${GEMMINI_BASE_ID}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "dma_base_id=${DMA_BASE_ID}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "long_conv_iters=${LONG_CONV_ITERS}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "short_conv_iters=${SHORT_CONV_ITERS}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "long_resadd_iters=${LONG_RESADD_ITERS}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "long_dma_iters=${LONG_DMA_ITERS}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
echo "short_dma_iters=${SHORT_DMA_ITERS}" >> "${SCRIPT_DIR}/build-config-nonblocking.txt"
