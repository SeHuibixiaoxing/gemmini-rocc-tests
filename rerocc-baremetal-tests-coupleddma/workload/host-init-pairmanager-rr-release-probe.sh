#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--num-cores N] [--num-gemmini G] [--num-dma D]"
  echo "          [--gemmini-base-id B] [--dma-base-id B]"
  echo "          [--cfg-id N] [--manager-id N] [--bytes B]"
  echo "          [--wait-spins N] [--mode 0|1|2|3]"
  echo "          [--tracerv-markers 0|1]"
}

NUM_CORES="${NUM_CORES:-4}"
NUM_GEMMINI="${NUM_GEMMINI:-12}"
NUM_DMA="${NUM_DMA:-12}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-0}"
CFG_ID="${REROCC_RR_PROBE_CFG_ID:-0}"
MANAGER_ID="${REROCC_RR_PROBE_MANAGER_ID:-0}"
BYTES="${REROCC_RR_PROBE_BYTES:-4096}"
WAIT_SPINS="${DMA_WAIT_SPINS:-20000000}"
MODE="${REROCC_RR_PROBE_MODE:-2}"
TRACERV_MARKERS="${REROCC_RR_PROBE_TRACERV_MARKERS:-1}"
OUTPUT_BASENAME="${REROCC_RR_PROBE_OUTPUT_BASENAME:-rerocc_lc_pairmanager_rr_release_probe_tracerv_inst.riscv}"
OUTPUT_TAG="${REROCC_RR_PROBE_OUTPUT_TAG:-pairmanager-rr-release-probe-tracerv-inst}"

while [ $# -gt 0 ]; do
  case "$1" in
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
    --cfg-id)
      CFG_ID="$2"
      shift 2
      ;;
    --manager-id)
      MANAGER_ID="$2"
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
    --mode)
      MODE="$2"
      shift 2
      ;;
    --tracerv-markers)
      TRACERV_MARKERS="$2"
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

echo "[rerocc-pairmanager-rr-release-probe] num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID} cfg_id=${CFG_ID} manager_id=${MANAGER_ID} bytes=${BYTES} wait_spins=${WAIT_SPINS} mode=${MODE} tracerv_markers=${TRACERV_MARKERS} output=${OUT_BIN}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_NUM_GEMMINI=${NUM_GEMMINI} -DREROCC_NUM_DMA=${NUM_DMA}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_GEMMINI_BASE_ID=${GEMMINI_BASE_ID} -DREROCC_DMA_BASE_ID=${DMA_BASE_ID}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_PAIR_MANAGER_MODE=1"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_RR_PROBE_CFG_ID=${CFG_ID} -DREROCC_RR_PROBE_MANAGER_ID=${MANAGER_ID}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_RR_PROBE_BYTES=${BYTES} -DDMA_WAIT_SPINS=${WAIT_SPINS}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_RR_PROBE_MODE=${MODE}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_RR_PROBE_TRACERV_MARKERS=${TRACERV_MARKERS}"

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_pairmanager_rr_release_probe-baremetal

cp -f "${BUILD_BAREMETAL_DIR}/rerocc_lc_pairmanager_rr_release_probe-baremetal" "${OUT_BIN}"

echo "num_cores=${NUM_CORES}" > "${BUILD_CONFIG}"
echo "num_gemmini=${NUM_GEMMINI}" >> "${BUILD_CONFIG}"
echo "num_dma=${NUM_DMA}" >> "${BUILD_CONFIG}"
echo "gemmini_base_id=${GEMMINI_BASE_ID}" >> "${BUILD_CONFIG}"
echo "dma_base_id=${DMA_BASE_ID}" >> "${BUILD_CONFIG}"
echo "cfg_id=${CFG_ID}" >> "${BUILD_CONFIG}"
echo "manager_id=${MANAGER_ID}" >> "${BUILD_CONFIG}"
echo "bytes=${BYTES}" >> "${BUILD_CONFIG}"
echo "wait_spins=${WAIT_SPINS}" >> "${BUILD_CONFIG}"
echo "mode=${MODE}" >> "${BUILD_CONFIG}"
echo "tracerv_markers=${TRACERV_MARKERS}" >> "${BUILD_CONFIG}"
echo "binary=${OUT_BIN}" >> "${BUILD_CONFIG}"
