#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE_RUNTIME_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CHIPYARD_ROOT="$(cd "${PIPELINE_RUNTIME_DIR}/../../../../.." && pwd)"
HYBRIDMAPPER_BERT_DIR="${CHIPYARD_ROOT}/tmp/HybridMapper/output/pipeline/bertmini"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-host-traces}"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
SKIP_BUILD="${SKIP_BUILD:-0}"

require_file() {
  if [ ! -f "$1" ]; then
    echo "missing required artifact: $1" >&2
    exit 1
  fi
}

if [ "${SKIP_BUILD}" != "1" ]; then
  make -C "${PIPELINE_RUNTIME_DIR}" clean all
fi

require_file "${PIPELINE_RUNTIME_DIR}/pipeline_runtime"
require_file "${HYBRIDMAPPER_BERT_DIR}/layers_gemmini.yaml"
require_file "${HYBRIDMAPPER_BERT_DIR}/dummy_weight/model.bin"
require_file "${HYBRIDMAPPER_BERT_DIR}/dummy_input/input.bin"
require_file "${HYBRIDMAPPER_BERT_DIR}/dummy_input/golden/golden.bin"

mkdir -p "${TRACE_DIR}"

for method in ${METHODS}; do
  require_file "${HYBRIDMAPPER_BERT_DIR}/entire_model/2_1024_16_19_64_${method}.yaml"
  echo "[bertmini-host] method=${method}"
  "${PIPELINE_RUNTIME_DIR}/pipeline_runtime" \
    --model-yaml "${HYBRIDMAPPER_BERT_DIR}/layers_gemmini.yaml" \
    --model-bin "${HYBRIDMAPPER_BERT_DIR}/dummy_weight/model.bin" \
    --pipeline-yaml "${HYBRIDMAPPER_BERT_DIR}/entire_model/2_1024_16_19_64_${method}.yaml" \
    --input "${HYBRIDMAPPER_BERT_DIR}/dummy_input/input.bin" \
    --golden "${HYBRIDMAPPER_BERT_DIR}/dummy_input/golden/golden.bin" \
    --num-cores 2 \
    --num-gemmini-mgrs 2 \
    --num-dma-mgrs 2 \
    --pages-per-acc 1024 \
    --batch 16 \
    --trace "${TRACE_DIR}/${method}.trace"
  echo "[bertmini-host] PASS method=${method}"
done

echo "BERTMINI_HOST_CLOSURE_PASS"
