#!/bin/sh
set -eu

ROOT_DIR="/root/rerocc-linux-tests/pipeline-runtime"
BERT_DIR="${ROOT_DIR}/bertmini"
BIN="${ROOT_DIR}/rerocc_pipeline_runtime-linux"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024}"
NUM_CORES="${NUM_CORES:-2}"
NUM_GEMMINI="${NUM_GEMMINI:-2}"
NUM_DMA="${NUM_DMA:-}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-0}"
PAGES_PER_ACC="${PAGES_PER_ACC:-1024}"
WATCHDOG_MS="${WATCHDOG_MS:-60000}"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-traces}"
GOLDEN_CHECK_ENABLE="${GOLDEN_CHECK_ENABLE:-1}"
HUGETLB_PAGES="${HUGETLB_PAGES:-1}"
HUGETLB_MOUNT="${HUGETLB_MOUNT:-/dev/hugepages}"
MODEL_YAML="${BERT_DIR}/model.layers.yaml"
LAYER_MAPPING_YAML="${BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
MODEL_BIN="${BERT_DIR}/runtime_model.bin"
INPUT_BIN="${BERT_DIR}/runtime_input.${TARGET_KEY}.bin"

meminfo_value() {
  awk -v key="$1" '$1 == key ":" { print $2; exit }' /proc/meminfo 2>/dev/null
}

prepare_hugetlb() {
  before_total=""
  before_free=""
  before_size=""
  after_total=""
  after_free=""
  after_size=""

  before_total="$(meminfo_value HugePages_Total || true)"
  before_free="$(meminfo_value HugePages_Free || true)"
  before_size="$(meminfo_value Hugepagesize || true)"
  echo "[bertmini] hugetlb before total=${before_total:-0} free=${before_free:-0} size_kb=${before_size:-0}"

  if [ -w /proc/sys/vm/nr_hugepages ]; then
    echo "${HUGETLB_PAGES}" > /proc/sys/vm/nr_hugepages || true
  fi

  mkdir -p "${HUGETLB_MOUNT}"
  if ! grep -qs " ${HUGETLB_MOUNT} " /proc/mounts; then
    mount -t hugetlbfs none "${HUGETLB_MOUNT}" || true
  fi

  after_total="$(meminfo_value HugePages_Total || true)"
  after_free="$(meminfo_value HugePages_Free || true)"
  after_size="$(meminfo_value Hugepagesize || true)"
  echo "[bertmini] hugetlb after total=${after_total:-0} free=${after_free:-0} size_kb=${after_size:-0} mount=${HUGETLB_MOUNT}"
}

resolve_manager_layout() {
  if [ "${PAIR_MANAGER_MODE}" = "1" ]; then
    [ -n "${NUM_DMA}" ] || NUM_DMA="${NUM_GEMMINI}"
    if [ "${NUM_DMA}" != "${NUM_GEMMINI}" ]; then
      echo "pair-manager mode requires NUM_DMA (${NUM_DMA}) to match NUM_GEMMINI (${NUM_GEMMINI})" >&2
      exit 2
    fi
    if [ -n "${DMA_BASE_ID}" ] && [ "${DMA_BASE_ID}" != "${GEMMINI_BASE_ID}" ]; then
      echo "pair-manager mode requires DMA_BASE_ID (${DMA_BASE_ID}) to match GEMMINI_BASE_ID (${GEMMINI_BASE_ID})" >&2
      exit 2
    fi
    DMA_BASE_ID="${GEMMINI_BASE_ID}"
    return
  fi

  [ -n "${NUM_DMA}" ] || NUM_DMA="${NUM_GEMMINI}"
  [ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID=$((GEMMINI_BASE_ID + NUM_GEMMINI))
}

if [ ! -x "${BIN}" ]; then
  echo "missing runtime binary: ${BIN}"
  exit 1
fi

resolve_manager_layout
mkdir -p "${TRACE_DIR}"
echo "[bertmini] golden-check enable=${GOLDEN_CHECK_ENABLE}"
prepare_hugetlb

fail=0
for method in ${METHODS}; do
  PIPELINE_YAML="${BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
  GOLDEN_BIN="${BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
  TRACE_PATH="${TRACE_DIR}/${method}.trace"
  echo "[bertmini] method=${method} cores=${NUM_CORES} gemmini=${NUM_GEMMINI} dma=${NUM_DMA} pair=${PAIR_MANAGER_MODE}"
  echo "[bertmini] golden-check enable=${GOLDEN_CHECK_ENABLE}"
  set -- \
    --backend fpga \
    --model-yaml "${MODEL_YAML}" \
    --layer-mapping-yaml "${LAYER_MAPPING_YAML}" \
    --model-bin "${MODEL_BIN}" \
    --pipeline-yaml "${PIPELINE_YAML}" \
    --input "${INPUT_BIN}" \
    --batch 16 \
    --num-cores "${NUM_CORES}" \
    --num-gemmini-mgrs "${NUM_GEMMINI}" \
    --num-dma-mgrs "${NUM_DMA}" \
    --pages-per-acc "${PAGES_PER_ACC}" \
    --gemmini-base-id "${GEMMINI_BASE_ID}" \
    --dma-base-id "${DMA_BASE_ID}" \
    --pair-manager-mode "${PAIR_MANAGER_MODE}" \
    --watchdog-ms "${WATCHDOG_MS}" \
    --trace "${TRACE_PATH}"
  if [ "${GOLDEN_CHECK_ENABLE}" != "0" ]; then
    set -- "$@" --golden "${GOLDEN_BIN}"
  fi
  if ! "${BIN}" "$@"; then
    fail=1
    echo "[bertmini] FAIL method=${method}"
    break
  fi
  echo "[bertmini] PASS method=${method}"
done

if [ "${fail}" -eq 0 ]; then
  echo "BERTMINI_PIPELINE_RUNTIME_PASS"
  poweroff -f
  exit 0
fi

echo "BERTMINI_PIPELINE_RUNTIME_FAIL"
poweroff -f
exit 1
