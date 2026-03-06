#!/bin/sh

print_usage() {
  echo "Usage: $0 [--target 2c2g2d|4c4g4d|custom] [--matrix full|diagonal|single]"
  echo "          [--num-cores N] [--num-gemmini G] [--num-dma D] [--bytes B]"
  echo "          [--gemmini-base-id B] [--dma-base-id B]"
}

TARGET_PROFILE="${REROCC_TARGET:-${HW_TARGET:-4c4g4d}}"
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
    [ -n "${NUM_CORES}" ] || NUM_CORES=4
    [ -n "${NUM_GEMMINI}" ] || NUM_GEMMINI=4
    [ -n "${NUM_DMA}" ] || NUM_DMA=4
    ;;
  *)
    echo "Unknown target profile: ${TARGET_PROFILE}"
    print_usage
    exit 1
    ;;
esac

[ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID="${NUM_GEMMINI}"

echo "[rerocc-lc] target=${TARGET_PROFILE} matrix=${MATRIX_MODE} num_cores=${NUM_CORES} num_gemmini=${NUM_GEMMINI} num_dma=${NUM_DMA} bytes=${BYTES} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID}"

gemmini_ret=0
dma_ret=0

/root/rerocc-linux-tests/rerocc_gemmini_conv_matrix-linux \
  --num-cores "${NUM_CORES}" \
  --num-gemmini "${NUM_GEMMINI}" \
  --gemmini-base-id "${GEMMINI_BASE_ID}" \
  --matrix "${MATRIX_MODE}" || gemmini_ret=$?

/root/rerocc-linux-tests/rerocc_dma_matrix-linux \
  --num-cores "${NUM_CORES}" \
  --num-gemmini "${NUM_GEMMINI}" \
  --num-dma "${NUM_DMA}" \
  --dma-base-id "${DMA_BASE_ID}" \
  --bytes "${BYTES}" \
  --matrix "${MATRIX_MODE}" || dma_ret=$?

if [ "${gemmini_ret}" -eq 0 ] && [ "${dma_ret}" -eq 0 ]; then
  echo "ALL_TESTS_PASS"
  poweroff -f
  exit 0
fi

echo "ALL_TESTS_FAIL gemmini_ret=${gemmini_ret} dma_ret=${dma_ret}"
poweroff -f
exit 1
