#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--num-cores N] [--cfg-id N] [--manager0 N] [--manager1 N]"
  echo "          [--bytes B] [--wait-spins N] [--case-first N] [--case-count N]"
}

NUM_CORES="${NUM_CORES:-2}"
CFG_ID="${REROCC_CFG_ID:-16}"
MANAGER0_ID="${REROCC_MANAGER0_ID:-0}"
MANAGER1_ID="${REROCC_MANAGER1_ID:-1}"
BYTES="${BYTES:-1024}"
WAIT_SPINS="${REROCC_DMA_WAIT_SPINS:-20000}"
VERIFY_DATA="${REROCC_DMA_VERIFY_DATA:-1}"
CASE_FIRST="${REROCC_DMA_CASE_FIRST:-0}"
CASE_COUNT="${REROCC_DMA_CASE_COUNT:-0}"
OUTPUT_BASENAME="${DMA_MISALIGNED_PERF_OUTPUT_BASENAME:-rerocc_lc_dma_misaligned_perf_pairmanager.riscv}"
OUTPUT_TAG="${DMA_MISALIGNED_PERF_OUTPUT_TAG:-dma-misaligned-perf-pairmanager}"

while [ $# -gt 0 ]; do
  case "$1" in
    --num-cores)
      NUM_CORES="$2"
      shift 2
      ;;
    --cfg-id)
      CFG_ID="$2"
      shift 2
      ;;
    --manager0)
      MANAGER0_ID="$2"
      shift 2
      ;;
    --manager1)
      MANAGER1_ID="$2"
      shift 2
      ;;
    --bytes)
      BYTES="$2"
      shift 2
      ;;
    --wait-spins)
      WAIT_SPINS="$2"
      shift 2
      ;;
    --case-first)
      CASE_FIRST="$2"
      shift 2
      ;;
    --case-count)
      CASE_COUNT="$2"
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

echo "[rerocc-dma-misaligned-perf] num_cores=${NUM_CORES} cfg_id=${CFG_ID} manager0=${MANAGER0_ID} manager1=${MANAGER1_ID} bytes=${BYTES} wait_spins=${WAIT_SPINS} case_first=${CASE_FIRST} case_count=${CASE_COUNT} output=${OUT_BIN}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_PAIR_MANAGER_MODE=1 -DREROCC_CFG_ID=${CFG_ID} -DREROCC_MANAGER0_ID=${MANAGER0_ID} -DREROCC_MANAGER1_ID=${MANAGER1_ID} -DREROCC_DMA_BYTES=${BYTES} -DREROCC_DMA_WAIT_SPINS=${WAIT_SPINS} -DREROCC_DMA_CASE_FIRST=${CASE_FIRST} -DREROCC_DMA_CASE_COUNT=${CASE_COUNT} -DREROCC_DMA_VERIFY_DATA=${VERIFY_DATA}"

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_dma_misaligned_perf_pairmanager-baremetal

cp -f "${BUILD_BAREMETAL_DIR}/rerocc_lc_dma_misaligned_perf_pairmanager-baremetal" "${OUT_BIN}"

echo "num_cores=${NUM_CORES}" > "${BUILD_CONFIG}"
echo "cfg_id=${CFG_ID}" >> "${BUILD_CONFIG}"
echo "manager0=${MANAGER0_ID}" >> "${BUILD_CONFIG}"
echo "manager1=${MANAGER1_ID}" >> "${BUILD_CONFIG}"
echo "bytes=${BYTES}" >> "${BUILD_CONFIG}"
echo "wait_spins=${WAIT_SPINS}" >> "${BUILD_CONFIG}"
echo "case_first=${CASE_FIRST}" >> "${BUILD_CONFIG}"
echo "case_count=${CASE_COUNT}" >> "${BUILD_CONFIG}"
echo "binary=${OUT_BIN}" >> "${BUILD_CONFIG}"
