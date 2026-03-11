#!/bin/sh
set -eu

ROOT_DIR="/root/rerocc-linux-tests/pipeline-runtime"
BERT_DIR="${ROOT_DIR}/bertmini"
BIN="${ROOT_DIR}/rerocc_pipeline_runtime-linux"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
NUM_CORES="${NUM_CORES:-2}"
NUM_GEMMINI="${NUM_GEMMINI:-2}"
NUM_DMA="${NUM_DMA:-2}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
PAGES_PER_ACC="${PAGES_PER_ACC:-1024}"
WATCHDOG_MS="${WATCHDOG_MS:-60000}"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-traces}"
MODEL_YAML="${BERT_DIR}/layers_gemmini.yaml"
MODEL_BIN="${BERT_DIR}/model.bin"
INPUT_BIN="${BERT_DIR}/input.bin"
GOLDEN_BIN="${BERT_DIR}/golden.bin"
CANONICAL_PREFIX="2_1024_16_19_64"

if [ ! -x "${BIN}" ]; then
  echo "missing runtime binary: ${BIN}"
  exit 1
fi

[ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID=$((GEMMINI_BASE_ID + NUM_GEMMINI))
mkdir -p "${TRACE_DIR}"

fail=0
for method in ${METHODS}; do
  PIPELINE_YAML="${BERT_DIR}/entire_model/${CANONICAL_PREFIX}_${method}.yaml"
  TRACE_PATH="${TRACE_DIR}/${method}.trace"
  echo "[bertmini] method=${method} cores=${NUM_CORES} gemmini=${NUM_GEMMINI} dma=${NUM_DMA}"
  if ! "${BIN}" \
      --model-yaml "${MODEL_YAML}" \
      --model-bin "${MODEL_BIN}" \
      --pipeline-yaml "${PIPELINE_YAML}" \
      --input "${INPUT_BIN}" \
      --golden "${GOLDEN_BIN}" \
      --batch 16 \
      --num-cores "${NUM_CORES}" \
      --num-gemmini-mgrs "${NUM_GEMMINI}" \
      --num-dma-mgrs "${NUM_DMA}" \
      --pages-per-acc "${PAGES_PER_ACC}" \
      --gemmini-base-id "${GEMMINI_BASE_ID}" \
      --dma-base-id "${DMA_BASE_ID}" \
      --watchdog-ms "${WATCHDOG_MS}" \
      --trace "${TRACE_PATH}"; then
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
