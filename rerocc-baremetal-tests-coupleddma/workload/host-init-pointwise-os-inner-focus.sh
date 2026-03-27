#!/usr/bin/env bash
set -euo pipefail

print_usage() {
  echo "Usage: $0 [--target 2c2g2d|4c4g4d|custom]"
  echo "          [--num-cores N] [--num-gemmini G]"
  echo "          [--gemmini-base-id B] [--local-gemmini-id L]"
}

TARGET_PROFILE="${REROCC_TARGET:-${HW_TARGET:-2c2g2d}}"
NUM_CORES="${NUM_CORES:-}"
NUM_GEMMINI="${NUM_GEMMINI:-}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
LOCAL_GEMMINI_ID="${LOCAL_GEMMINI_ID:-0}"
SHARED_SPAD_XLATE_RANGE_BASE="${SHARED_SPAD_XLATE_RANGE_BASE:-}"

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
    --gemmini-base-id)
      GEMMINI_BASE_ID="$2"
      shift 2
      ;;
    --local-gemmini-id)
      LOCAL_GEMMINI_ID="$2"
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
    ;;
  4c4g4d|default)
    [ -n "${NUM_CORES}" ] || NUM_CORES=4
    [ -n "${NUM_GEMMINI}" ] || NUM_GEMMINI=4
    ;;
  custom)
    [ -n "${NUM_CORES}" ] || NUM_CORES=2
    [ -n "${NUM_GEMMINI}" ] || NUM_GEMMINI=2
    ;;
  *)
    echo "Unknown target profile: ${TARGET_PROFILE}"
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
BUILD_CONFIG="${ARTIFACT_DIR}/build-config-pointwise-os-inner-focus.txt"
OUT_BIN="${SCRIPT_DIR}/rerocc_lc_pointwise_os_inner_runtime_alias_focus.riscv"

echo "[rerocc-coupleddma-pointwise-os-inner-focus] target=${TARGET_PROFILE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} gemmini_base_id=${GEMMINI_BASE_ID} local_gemmini_id=${LOCAL_GEMMINI_ID}"

mkdir -p "${BUILD_BAREMETAL_DIR}" "${ARTIFACT_DIR}"

EXTRA_DEFS="-DREROCC_NUM_GEMMINI=${NUM_GEMMINI} -DREROCC_GEMMINI_BASE_ID=${GEMMINI_BASE_ID} -DREROCC_TEST_LOCAL_GEMMINI_ID=${LOCAL_GEMMINI_ID}"
EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_STALL_DIAG_ONLY=${REROCC_STALL_DIAG_ONLY:-1}"
if [ -n "${SHARED_SPAD_XLATE_RANGE_BASE}" ]; then
  EXTRA_DEFS="${EXTRA_DEFS} -DREROCC_SHARED_SPAD_XLATE_RANGE_BASE=${SHARED_SPAD_XLATE_RANGE_BASE}"
fi

make -B -C "${BUILD_BAREMETAL_DIR}" \
  -f "${GEMMINI_ROCC_TESTS_DIR}/bareMetalC/Makefile" \
  abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
  src_dir="${GEMMINI_ROCC_TESTS_DIR}/bareMetalC" \
  XLEN=64 \
  PREFIX=examples-bareMetalC \
  RISCVTOOLS="${RISCVTOOLS:-}" \
  GEMMINI_NUM_CPU_CORES="${NUM_CORES}" \
  EXTRA_CFLAGS="${EXTRA_DEFS}" \
  rerocc_lc_pointwise_os_inner_runtime_alias_focus-baremetal

cp -f "${BUILD_BAREMETAL_DIR}/rerocc_lc_pointwise_os_inner_runtime_alias_focus-baremetal" "${OUT_BIN}"

echo "target=${TARGET_PROFILE}" > "${BUILD_CONFIG}"
echo "num_cores=${NUM_CORES}" >> "${BUILD_CONFIG}"
echo "num_gemmini=${NUM_GEMMINI}" >> "${BUILD_CONFIG}"
echo "gemmini_base_id=${GEMMINI_BASE_ID}" >> "${BUILD_CONFIG}"
echo "local_gemmini_id=${LOCAL_GEMMINI_ID}" >> "${BUILD_CONFIG}"
echo "shared_spad_xlate_range_base=${SHARED_SPAD_XLATE_RANGE_BASE:-default}" >> "${BUILD_CONFIG}"
