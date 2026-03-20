#!/bin/sh
set -eu

print_usage() {
  echo "Usage: $0 [--target 2c2g2d|4c4g4d|custom] [--matrix full|diagonal|single]"
  echo "          [--num-cores N] [--num-gemmini G] [--num-dma D]"
  echo "          [--gemmini-base-id B] [--dma-base-id B] [--local-gemmini-id L]"
  echo "          [--matrix-bytes B] [--coverage-bytes B] [--nonblocking-bytes B]"
}

TARGET_PROFILE="${REROCC_TARGET:-${HW_TARGET:-2c2g2d}}"
MATRIX_MODE="${MATRIX_MODE:-${TEST_MATRIX:-full}}"
NUM_CORES="${NUM_CORES:-}"
NUM_GEMMINI="${NUM_GEMMINI:-}"
NUM_DMA="${NUM_DMA:-}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
LOCAL_GEMMINI_ID="${LOCAL_GEMMINI_ID:-0}"
MATRIX_BYTES="${MATRIX_BYTES:-1024}"
COVERAGE_BYTES="${COVERAGE_BYTES:-1024}"
NONBLOCKING_BYTES="${NONBLOCKING_BYTES:-512}"

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
    --gemmini-base-id)
      GEMMINI_BASE_ID="$2"
      shift 2
      ;;
    --dma-base-id)
      DMA_BASE_ID="$2"
      shift 2
      ;;
    --local-gemmini-id)
      LOCAL_GEMMINI_ID="$2"
      shift 2
      ;;
    --matrix-bytes)
      MATRIX_BYTES="$2"
      shift 2
      ;;
    --coverage-bytes)
      COVERAGE_BYTES="$2"
      shift 2
      ;;
    --nonblocking-bytes)
      NONBLOCKING_BYTES="$2"
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

echo "[rerocc-lc-linux-regression] target=${TARGET_PROFILE} matrix=${MATRIX_MODE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID} local_gemmini_id=${LOCAL_GEMMINI_ID}"
echo "[rerocc-lc-linux-regression] matrix_bytes=${MATRIX_BYTES} coverage_bytes=${COVERAGE_BYTES} nonblocking_bytes=${NONBLOCKING_BYTES}"

if [ -f "/root/rerocc-linux-tests-coupleddma/.pipeline_runtime_only" ]; then
  echo "[rerocc-lc-linux-regression] switching to bertmini pipeline runtime"
  exec /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh
fi

matrix_ret=0
coverage_ret=0
nonblocking_ret=0

/root/rerocc-linux-tests-coupleddma/rerocc_lc_matrix_linux_coupleddma_verify-linux \
  --target "${TARGET_PROFILE}" \
  --matrix "${MATRIX_MODE}" \
  --num-cores "${NUM_CORES}" \
  --num-gemmini "${NUM_GEMMINI}" \
  --num-dma "${NUM_DMA}" \
  --bytes "${MATRIX_BYTES}" \
  --gemmini-base-id "${GEMMINI_BASE_ID}" \
  --dma-base-id "${DMA_BASE_ID}" || matrix_ret=$?

/root/rerocc-linux-tests-coupleddma/rerocc_lc_coverage_linux_coupleddma-linux \
  --target "${TARGET_PROFILE}" \
  --num-cores "${NUM_CORES}" \
  --num-gemmini "${NUM_GEMMINI}" \
  --num-dma "${NUM_DMA}" \
  --bytes "${COVERAGE_BYTES}" \
  --gemmini-base-id "${GEMMINI_BASE_ID}" \
  --dma-base-id "${DMA_BASE_ID}" \
  --local-gemmini-id "${LOCAL_GEMMINI_ID}" || coverage_ret=$?

/root/rerocc-linux-tests-coupleddma/rerocc_lc_nonblocking_linux_coupleddma-linux \
  --target "${TARGET_PROFILE}" \
  --num-cores "${NUM_CORES}" \
  --num-gemmini "${NUM_GEMMINI}" \
  --num-dma "${NUM_DMA}" \
  --bytes "${NONBLOCKING_BYTES}" \
  --gemmini-base-id "${GEMMINI_BASE_ID}" \
  --dma-base-id "${DMA_BASE_ID}" || nonblocking_ret=$?

if [ "${matrix_ret}" -eq 0 ] && [ "${coverage_ret}" -eq 0 ] && [ "${nonblocking_ret}" -eq 0 ]; then
  echo "ALL_TESTS_PASS"
  sync
  poweroff -f
  exit 0
fi

echo "ALL_TESTS_FAIL matrix_ret=${matrix_ret} coverage_ret=${coverage_ret} nonblocking_ret=${nonblocking_ret}"
sync
poweroff -f
exit 1
