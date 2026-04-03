#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--target 2c2g2d|4c4g4d|custom]"
  echo "          [--num-cores N] [--num-gemmini G] [--num-dma D]"
  echo "          [--gemmini-base-id B] [--dma-base-id B]"
  echo "          [--stage0-bytes B] [--stage1-bytes B]"
  echo "          [--repeat-count N] [--wait-spins N]"
  echo "          [--log-level 0|1|2] [--stage0-shared-gid G]"
}

TARGET_PROFILE="${REROCC_TARGET:-${HW_TARGET:-2c2g2d}}"
NUM_CORES="${NUM_CORES:-}"
NUM_GEMMINI="${NUM_GEMMINI:-}"
NUM_DMA="${NUM_DMA:-}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
STAGE0_BYTES="${REROCC_EXPORT_STAGE0_BYTES:-524288}"
STAGE1_BYTES="${REROCC_EXPORT_STAGE1_BYTES:-65536}"
REPEAT_COUNT="${REROCC_EXPORT_REPEAT_COUNT:-8}"
WAIT_SPINS="${DMA_WAIT_SPINS:-20000000}"
ACQUIRE_MAX_RETRIES="${REROCC_ACQUIRE_MAX_RETRIES:-1000000}"
LOG_LEVEL="${REROCC_SEG3_LOG_LEVEL:-}"
STAGE0_SHARED_GID="${REROCC_STAGE0_SHARED_GID:-}"

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
    --gemmini-base-id)
      GEMMINI_BASE_ID="$2"
      shift 2
      ;;
    --dma-base-id)
      DMA_BASE_ID="$2"
      shift 2
      ;;
    --stage0-bytes)
      STAGE0_BYTES="$2"
      shift 2
      ;;
    --stage1-bytes)
      STAGE1_BYTES="$2"
      shift 2
      ;;
    --repeat-count)
      REPEAT_COUNT="$2"
      shift 2
      ;;
    --wait-spins)
      WAIT_SPINS="$2"
      shift 2
      ;;
    --log-level)
      LOG_LEVEL="$2"
      shift 2
      ;;
    --stage0-shared-gid)
      STAGE0_SHARED_GID="$2"
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
[ -n "${LOG_LEVEL}" ] || LOG_LEVEL=0

if [ -z "${STAGE0_SHARED_GID}" ]; then
  if [ "${NUM_GEMMINI}" -gt 1 ]; then
    STAGE0_SHARED_GID=1
  else
    STAGE0_SHARED_GID=0
  fi
fi

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
BUILD_CONFIG="${ARTIFACT_DIR}/build-config-export-dma-bertmini-segment3-repro.txt"
OUT_BIN="${SCRIPT_DIR}/rerocc_lc_export_dma_bertmini_segment3_repro.riscv"

echo "[rerocc-coupleddma-export-dma-bertmini-segment3-repro] target=${TARGET_PROFILE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID} stage0_bytes=${STAGE0_BYTES} stage1_bytes=${STAGE1_BYTES} repeat_count=${REPEAT_COUNT} wait_spins=${WAIT_SPINS} log_level=${LOG_LEVEL} stage0_shared_gid=${STAGE0_SHARED_GID}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_NUM_GEMMINI=${NUM_GEMMINI} -DREROCC_NUM_DMA=${NUM_DMA}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_GEMMINI_BASE_ID=${GEMMINI_BASE_ID} -DREROCC_DMA_BASE_ID=${DMA_BASE_ID}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_EXPORT_STAGE0_BYTES=${STAGE0_BYTES} -DREROCC_EXPORT_STAGE1_BYTES=${STAGE1_BYTES}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_EXPORT_REPEAT_COUNT=${REPEAT_COUNT} -DDMA_WAIT_SPINS=${WAIT_SPINS}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_ACQUIRE_MAX_RETRIES=${ACQUIRE_MAX_RETRIES}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_SEG3_LOG_LEVEL=${LOG_LEVEL} -DREROCC_STAGE0_SHARED_GID=${STAGE0_SHARED_GID}"

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_export_dma_bertmini_segment3_repro-baremetal

cp -f "${BUILD_BAREMETAL_DIR}/rerocc_lc_export_dma_bertmini_segment3_repro-baremetal" "${OUT_BIN}"

echo "target=${TARGET_PROFILE}" > "${BUILD_CONFIG}"
echo "num_cores=${NUM_CORES}" >> "${BUILD_CONFIG}"
echo "num_gemmini=${NUM_GEMMINI}" >> "${BUILD_CONFIG}"
echo "num_dma=${NUM_DMA}" >> "${BUILD_CONFIG}"
echo "gemmini_base_id=${GEMMINI_BASE_ID}" >> "${BUILD_CONFIG}"
echo "dma_base_id=${DMA_BASE_ID}" >> "${BUILD_CONFIG}"
echo "stage0_bytes=${STAGE0_BYTES}" >> "${BUILD_CONFIG}"
echo "stage1_bytes=${STAGE1_BYTES}" >> "${BUILD_CONFIG}"
echo "repeat_count=${REPEAT_COUNT}" >> "${BUILD_CONFIG}"
echo "wait_spins=${WAIT_SPINS}" >> "${BUILD_CONFIG}"
echo "acquire_max_retries=${ACQUIRE_MAX_RETRIES}" >> "${BUILD_CONFIG}"
echo "log_level=${LOG_LEVEL}" >> "${BUILD_CONFIG}"
echo "stage0_shared_gid=${STAGE0_SHARED_GID}" >> "${BUILD_CONFIG}"
echo "binary=${OUT_BIN}" >> "${BUILD_CONFIG}"
