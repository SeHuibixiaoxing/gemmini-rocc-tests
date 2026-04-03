#!/bin/sh
set -eu

ROOT_DIR="/root/rerocc-linux-tests/pipeline-runtime"
BERT_DIR="${ROOT_DIR}/bertmini"
BIN="${ROOT_DIR}/rerocc_pipeline_runtime-linux"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024}"
TARGET_BATCH="${TARGET_BATCH:-16}"
NUM_CORES="${NUM_CORES:-2}"
NUM_GEMMINI="${NUM_GEMMINI:-2}"
NUM_DMA="${NUM_DMA:-2}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
PAGES_PER_ACC="${PAGES_PER_ACC:-1024}"
WATCHDOG_MS="${WATCHDOG_MS:-600000}"
EXPORT_DMA_TIMEOUT_MS="${EXPORT_DMA_TIMEOUT_MS:-0}"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-traces}"
TRACE_ENABLE="${TRACE_ENABLE:-1}"
AUTO_POWEROFF="${AUTO_POWEROFF:-1}"
HUGETLB_PAGES="${HUGETLB_PAGES:-1}"
HUGETLB_MOUNT="${HUGETLB_MOUNT:-/dev/hugepages}"
DEEP_LOG_ENABLE="${DEEP_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE="${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE:-32}"
DEEP_LOG_SEGMENT="${DEEP_LOG_SEGMENT:-31}"
DEEP_LOG_GLOBAL_STAGE="${DEEP_LOG_GLOBAL_STAGE:-39}"
DEEP_LOG_LOCAL_STAGE="${DEEP_LOG_LOCAL_STAGE:-0}"
DEEP_LOG_SUBBATCH="${DEEP_LOG_SUBBATCH-}"
DEEP_LOG_STAGE_RADIUS="${DEEP_LOG_STAGE_RADIUS:-}"
DEEP_LOG_SUBBATCH_RADIUS="${DEEP_LOG_SUBBATCH_RADIUS:-}"
MODEL_YAML="${BERT_DIR}/model.layers.yaml"
LAYER_MAPPING_YAML="${BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
MODEL_BIN="${BERT_DIR}/runtime_model.bin"
INPUT_BIN="${BERT_DIR}/runtime_input.${TARGET_KEY}.bin"

while [ "$#" -gt 0 ]; do
  case "$1" in
    --batch)
      if [ "$#" -lt 2 ]; then
        echo "missing value for --batch" >&2
        exit 2
      fi
      TARGET_BATCH="$2"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

meminfo_value() {
  awk -v key="$1" '$1 == key ":" { print $2; exit }' /proc/meminfo 2>/dev/null
}

runner_log() {
  echo "[bertmini] $*"
}

prepare_hugetlb() {
  before_total=""
  before_free=""
  before_size=""
  after_total=""
  after_free=""
  after_size=""

  runner_log "hugetlb-probe-begin mount=${HUGETLB_MOUNT} pages=${HUGETLB_PAGES}"
  before_total="$(meminfo_value HugePages_Total || true)"
  before_free="$(meminfo_value HugePages_Free || true)"
  before_size="$(meminfo_value Hugepagesize || true)"
  runner_log "hugetlb before total=${before_total:-0} free=${before_free:-0} size_kb=${before_size:-0}"

  if [ -w /proc/sys/vm/nr_hugepages ]; then
    runner_log "hugetlb-set-begin pages=${HUGETLB_PAGES}"
    echo "${HUGETLB_PAGES}" > /proc/sys/vm/nr_hugepages || true
    runner_log "hugetlb-set-end pages=${HUGETLB_PAGES}"
  fi

  mkdir -p "${HUGETLB_MOUNT}"
  if ! grep -qs " ${HUGETLB_MOUNT} " /proc/mounts; then
    runner_log "hugetlb-mount-begin mount=${HUGETLB_MOUNT}"
    mount -t hugetlbfs none "${HUGETLB_MOUNT}" || true
    runner_log "hugetlb-mount-end mount=${HUGETLB_MOUNT}"
  fi

  after_total="$(meminfo_value HugePages_Total || true)"
  after_free="$(meminfo_value HugePages_Free || true)"
  after_size="$(meminfo_value Hugepagesize || true)"
  runner_log "hugetlb after total=${after_total:-0} free=${after_free:-0} size_kb=${after_size:-0} mount=${HUGETLB_MOUNT}"
}

finish_run() {
  rc="$1"

  if [ "${rc}" -eq 0 ]; then
    echo "BERTMINI_PIPELINE_RUNTIME_PASS"
  else
    echo "BERTMINI_PIPELINE_RUNTIME_FAIL"
  fi

  sync || true

  if [ "${AUTO_POWEROFF}" = "0" ]; then
    echo "[bertmini] auto poweroff disabled; guest left running for manual inspection"
    echo "[bertmini] when ready, run: sync; poweroff"
    exit "${rc}"
  fi

  poweroff -f
  exit "${rc}"
}

if [ ! -x "${BIN}" ]; then
  echo "missing runtime binary: ${BIN}"
  exit 1
fi

runner_log "runner-enter batch=${TARGET_BATCH} methods=${METHODS}"
[ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID=$((GEMMINI_BASE_ID + NUM_GEMMINI))
runner_log "runner-config gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID} trace_dir=${TRACE_DIR} trace_enable=${TRACE_ENABLE} export_dma_timeout_ms=${EXPORT_DMA_TIMEOUT_MS}"
runner_log "runner-log-config deep_log_enable=${DEEP_LOG_ENABLE} dma_submit_trace_enable=${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE} dma_export_chunk_log_stride=${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}"
mkdir -p "${TRACE_DIR}"
if [ "${TRACE_ENABLE}" != "0" ]; then
  runner_log "trace-dir-ready path=${TRACE_DIR}"
else
  runner_log "trace-disabled path=${TRACE_DIR}"
fi
runner_log "before-prepare-hugetlb"
prepare_hugetlb
runner_log "after-prepare-hugetlb"

fail=0
for method in ${METHODS}; do
  PIPELINE_YAML="${BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
  GOLDEN_BIN="${BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
  TRACE_PATH="${TRACE_DIR}/${method}.trace"
  : > "${TRACE_PATH}"
  set -- \
    --backend fpga \
    --model-yaml "${MODEL_YAML}" \
    --layer-mapping-yaml "${LAYER_MAPPING_YAML}" \
    --model-bin "${MODEL_BIN}" \
    --pipeline-yaml "${PIPELINE_YAML}" \
    --input "${INPUT_BIN}" \
    --golden "${GOLDEN_BIN}" \
    --batch "${TARGET_BATCH}" \
    --num-cores "${NUM_CORES}" \
    --num-gemmini-mgrs "${NUM_GEMMINI}" \
    --num-dma-mgrs "${NUM_DMA}" \
    --pages-per-acc "${PAGES_PER_ACC}" \
    --gemmini-base-id "${GEMMINI_BASE_ID}" \
    --dma-base-id "${DMA_BASE_ID}" \
    --watchdog-ms "${WATCHDOG_MS}" \
    --export-dma-timeout-ms "${EXPORT_DMA_TIMEOUT_MS}"
  if [ "${TRACE_ENABLE}" != "0" ]; then
    set -- "$@" --trace "${TRACE_PATH}"
  fi
  if [ "${DEEP_LOG_ENABLE}" != "0" ]; then
    set -- "$@" --deep-log-enable 1
    [ -n "${DEEP_LOG_SEGMENT}" ] && set -- "$@" --deep-log-segment "${DEEP_LOG_SEGMENT}"
    [ -n "${DEEP_LOG_GLOBAL_STAGE}" ] && set -- "$@" --deep-log-global-stage "${DEEP_LOG_GLOBAL_STAGE}"
    [ -n "${DEEP_LOG_LOCAL_STAGE}" ] && set -- "$@" --deep-log-local-stage "${DEEP_LOG_LOCAL_STAGE}"
    [ -n "${DEEP_LOG_SUBBATCH}" ] && set -- "$@" --deep-log-subbatch "${DEEP_LOG_SUBBATCH}"
    [ -n "${DEEP_LOG_STAGE_RADIUS}" ] && set -- "$@" --deep-log-stage-radius "${DEEP_LOG_STAGE_RADIUS}"
    [ -n "${DEEP_LOG_SUBBATCH_RADIUS}" ] && set -- "$@" --deep-log-subbatch-radius "${DEEP_LOG_SUBBATCH_RADIUS}"
  fi
  echo "[bertmini] method=${method} cores=${NUM_CORES} gemmini=${NUM_GEMMINI} dma=${NUM_DMA}"
  echo "[bertmini] trace enable=${TRACE_ENABLE}"
  echo "[bertmini] dma submit trace enable=${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE} export chunk stride=${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}"
  echo "[bertmini] deep-log enable=${DEEP_LOG_ENABLE} segment=${DEEP_LOG_SEGMENT:-any} global_stage=${DEEP_LOG_GLOBAL_STAGE:-any} local_stage=${DEEP_LOG_LOCAL_STAGE:-any} subbatch=${DEEP_LOG_SUBBATCH:-any} stage_radius=${DEEP_LOG_STAGE_RADIUS:-0} subbatch_radius=${DEEP_LOG_SUBBATCH_RADIUS:-0}"
  if ! \
    PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE}" \
    PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE="${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}" \
    "${BIN}" "$@"; then
    fail=1
    echo "[bertmini] FAIL method=${method}"
    break
  fi
  echo "[bertmini] PASS method=${method}"
done

if [ "${fail}" -eq 0 ]; then
  finish_run 0
fi

finish_run 1
