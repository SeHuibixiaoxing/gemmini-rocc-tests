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
BYTES="${BYTES:-512}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"

LONG_CONV_ITERS="${LONG_CONV_ITERS:-4}"
SHORT_CONV_ITERS="${SHORT_CONV_ITERS:-1}"
LONG_RESADD_ITERS="${LONG_RESADD_ITERS:-32}"
LONG_DMA_ITERS="${LONG_DMA_ITERS:-4}"
SHORT_DMA_ITERS="${SHORT_DMA_ITERS:-1}"
NONBLOCKING_RUN_S1="${NONBLOCKING_RUN_S1:-1}"
NONBLOCKING_RUN_S2="${NONBLOCKING_RUN_S2:-1}"
NONBLOCKING_RUN_S3="${NONBLOCKING_RUN_S3:-1}"
NONBLOCKING_RUN_S4="${NONBLOCKING_RUN_S4:-1}"
PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-0}"
DMA_MISALIGNED_PROFILE="${DMA_MISALIGNED_PROFILE:-0}"
DMA_SRC_HEAD_OFFSET="${DMA_SRC_HEAD_OFFSET:-16}"
DMA_SHARED_B_OFFSET="${DMA_SHARED_B_OFFSET:-48}"
DMA_DST_TAIL_OFFSET="${DMA_DST_TAIL_OFFSET:-16}"
ACQUIRE_MAX_RETRIES="${ACQUIRE_MAX_RETRIES:-1000000}"
DMA_WAIT_SPINS="${DMA_WAIT_SPINS:-2000000}"
CORE1_DELAY_SPINS="${CORE1_DELAY_SPINS:-2000}"
TRACE_PROGRESS="${TRACE_PROGRESS:-0}"
NONBLOCKING_VERBOSE="${NONBLOCKING_VERBOSE:-1}"
DEBUG_FORCE_LOGICAL_CORES="${DEBUG_FORCE_LOGICAL_CORES:-0}"
OUTPUT_BASENAME="${NONBLOCKING_OUTPUT_BASENAME:-rerocc_lc_nonblocking_baremetal_coupleddma.riscv}"
OUTPUT_TAG="${NONBLOCKING_OUTPUT_TAG:-nonblocking}"

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

[ -n "${DMA_BASE_ID}" ] || {
  if [ "${PAIR_MANAGER_MODE}" = "1" ]; then
    DMA_BASE_ID="${GEMMINI_BASE_ID}"
  else
    DMA_BASE_ID="${NUM_GEMMINI}"
  fi
}

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
BUILD_CONFIG="${ARTIFACT_DIR}/build-config-nonblocking.txt"
OUT_BIN="${SCRIPT_DIR}/${OUTPUT_BASENAME}"

echo "[rerocc-coupleddma-nonblocking] target=${TARGET_PROFILE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} bytes=${BYTES} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID}"
echo "[rerocc-coupleddma-nonblocking] long_conv=${LONG_CONV_ITERS} short_conv=${SHORT_CONV_ITERS} long_resadd=${LONG_RESADD_ITERS} long_dma=${LONG_DMA_ITERS} short_dma=${SHORT_DMA_ITERS}"
echo "[rerocc-coupleddma-nonblocking] pair_mode=${PAIR_MANAGER_MODE} misaligned_profile=${DMA_MISALIGNED_PROFILE} output=${OUT_BIN}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_NUM_GEMMINI=${NUM_GEMMINI} -DREROCC_NUM_DMA=${NUM_DMA} -DREROCC_GEMMINI_BASE_ID=${GEMMINI_BASE_ID} -DREROCC_DMA_BASE_ID=${DMA_BASE_ID} -DREROCC_DMA_BYTES=${BYTES} -DREROCC_LONG_CONV_ITERS=${LONG_CONV_ITERS} -DREROCC_SHORT_CONV_ITERS=${SHORT_CONV_ITERS} -DREROCC_LONG_RESADD_ITERS=${LONG_RESADD_ITERS} -DREROCC_LONG_DMA_ITERS=${LONG_DMA_ITERS} -DREROCC_SHORT_DMA_ITERS=${SHORT_DMA_ITERS} -DREROCC_NONBLOCKING_RUN_S1=${NONBLOCKING_RUN_S1} -DREROCC_NONBLOCKING_RUN_S2=${NONBLOCKING_RUN_S2} -DREROCC_NONBLOCKING_RUN_S3=${NONBLOCKING_RUN_S3} -DREROCC_NONBLOCKING_RUN_S4=${NONBLOCKING_RUN_S4} -DREROCC_PAIR_MANAGER_MODE=${PAIR_MANAGER_MODE} -DREROCC_DMA_MISALIGNED_PROFILE=${DMA_MISALIGNED_PROFILE} -DREROCC_DMA_SRC_HEAD_OFFSET=${DMA_SRC_HEAD_OFFSET} -DREROCC_DMA_SHARED_B_OFFSET=${DMA_SHARED_B_OFFSET} -DREROCC_DMA_DST_TAIL_OFFSET=${DMA_DST_TAIL_OFFSET} -DREROCC_ACQUIRE_MAX_RETRIES=${ACQUIRE_MAX_RETRIES} -DDMA_WAIT_SPINS=${DMA_WAIT_SPINS} -DREROCC_CORE1_DELAY_SPINS=${CORE1_DELAY_SPINS} -DREROCC_TRACE_PROGRESS=${TRACE_PROGRESS} -DREROCC_NONBLOCKING_VERBOSE=${NONBLOCKING_VERBOSE} -DREROCC_DEBUG_FORCE_LOGICAL_CORES=${DEBUG_FORCE_LOGICAL_CORES}"

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

echo "target=${TARGET_PROFILE}" > "${BUILD_CONFIG}"
echo "num_cores=${NUM_CORES}" >> "${BUILD_CONFIG}"
echo "num_gemmini=${NUM_GEMMINI}" >> "${BUILD_CONFIG}"
echo "num_dma=${NUM_DMA}" >> "${BUILD_CONFIG}"
echo "bytes=${BYTES}" >> "${BUILD_CONFIG}"
echo "gemmini_base_id=${GEMMINI_BASE_ID}" >> "${BUILD_CONFIG}"
echo "dma_base_id=${DMA_BASE_ID}" >> "${BUILD_CONFIG}"
echo "long_conv_iters=${LONG_CONV_ITERS}" >> "${BUILD_CONFIG}"
echo "short_conv_iters=${SHORT_CONV_ITERS}" >> "${BUILD_CONFIG}"
echo "long_resadd_iters=${LONG_RESADD_ITERS}" >> "${BUILD_CONFIG}"
echo "long_dma_iters=${LONG_DMA_ITERS}" >> "${BUILD_CONFIG}"
echo "short_dma_iters=${SHORT_DMA_ITERS}" >> "${BUILD_CONFIG}"
echo "nonblocking_run_s1=${NONBLOCKING_RUN_S1}" >> "${BUILD_CONFIG}"
echo "nonblocking_run_s2=${NONBLOCKING_RUN_S2}" >> "${BUILD_CONFIG}"
echo "nonblocking_run_s3=${NONBLOCKING_RUN_S3}" >> "${BUILD_CONFIG}"
echo "nonblocking_run_s4=${NONBLOCKING_RUN_S4}" >> "${BUILD_CONFIG}"
echo "pair_manager_mode=${PAIR_MANAGER_MODE}" >> "${BUILD_CONFIG}"
echo "dma_misaligned_profile=${DMA_MISALIGNED_PROFILE}" >> "${BUILD_CONFIG}"
echo "acquire_max_retries=${ACQUIRE_MAX_RETRIES}" >> "${BUILD_CONFIG}"
echo "dma_wait_spins=${DMA_WAIT_SPINS}" >> "${BUILD_CONFIG}"
echo "core1_delay_spins=${CORE1_DELAY_SPINS}" >> "${BUILD_CONFIG}"
echo "trace_progress=${TRACE_PROGRESS}" >> "${BUILD_CONFIG}"
echo "nonblocking_verbose=${NONBLOCKING_VERBOSE}" >> "${BUILD_CONFIG}"
echo "debug_force_logical_cores=${DEBUG_FORCE_LOGICAL_CORES}" >> "${BUILD_CONFIG}"
echo "output=${OUT_BIN}" >> "${BUILD_CONFIG}"
