#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE_RUNTIME_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CHIPYARD_ROOT="$(cd "${PIPELINE_RUNTIME_DIR}/../../../../.." && pwd)"
HYBRIDMAPPER_ROOT="${CHIPYARD_ROOT}/conference/HybridMapper"
PIPELINE_RUNTIME_ARTIFACT_DIR="${HYBRIDMAPPER_ROOT}/output/pipeline_runtime/bertmini"
PIPELINE_RUNTIME_EXPORTER="${HYBRIDMAPPER_ROOT}/scripts/create-pipeline-runtime-artifacts.py"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024}"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-host-traces}"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_EXPORT="${SKIP_EXPORT:-0}"
WATCHDOG_MS="${WATCHDOG_MS:-300000}"
BATCH="${BATCH:-1}"

require_file() {
  if [ ! -f "$1" ]; then
    echo "missing required artifact: $1" >&2
    exit 1
  fi
}

if [ "${SKIP_BUILD}" != "1" ]; then
  make -C "${PIPELINE_RUNTIME_DIR}" clean all
fi

if [ "${SKIP_EXPORT}" != "1" ]; then
  python3 "${PIPELINE_RUNTIME_EXPORTER}" --model bertmini
fi

require_file "${PIPELINE_RUNTIME_DIR}/pipeline_runtime"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/model.layers.yaml"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_model.bin"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_input.${TARGET_KEY}.bin"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"

mkdir -p "${TRACE_DIR}"

for method in ${METHODS}; do
  require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
  GOLDEN_PATH="${PIPELINE_RUNTIME_ARTIFACT_DIR}/golden.${TARGET_KEY}.${method}.bin"
  echo "[bertmini-host] method=${method}"
  "${PIPELINE_RUNTIME_DIR}/pipeline_runtime" \
    --backend cpu \
    --model-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/model.layers.yaml" \
    --layer-mapping-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml" \
    --model-bin "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_model.bin" \
    --pipeline-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml" \
    --input "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_input.${TARGET_KEY}.bin" \
    --golden-out "${GOLDEN_PATH}" \
    --num-cores 2 \
    --num-gemmini-mgrs 2 \
    --num-dma-mgrs 2 \
    --pages-per-acc 1024 \
    --watchdog-ms "${WATCHDOG_MS}" \
    --batch "${BATCH}" \
    --trace "${TRACE_DIR}/${method}.cpu.trace"
  require_file "${GOLDEN_PATH}"
  "${PIPELINE_RUNTIME_DIR}/pipeline_runtime" \
    --backend fpga \
    --model-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/model.layers.yaml" \
    --layer-mapping-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml" \
    --model-bin "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_model.bin" \
    --pipeline-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml" \
    --input "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_input.${TARGET_KEY}.bin" \
    --golden "${GOLDEN_PATH}" \
    --num-cores 2 \
    --num-gemmini-mgrs 2 \
    --num-dma-mgrs 2 \
    --pages-per-acc 1024 \
    --watchdog-ms "${WATCHDOG_MS}" \
    --batch "${BATCH}" \
    --trace "${TRACE_DIR}/${method}.fpga.trace"
  echo "[bertmini-host] PASS method=${method}"
done

echo "BERTMINI_HOST_CLOSURE_PASS"
