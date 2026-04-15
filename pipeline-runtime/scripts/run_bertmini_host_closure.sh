#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE_RUNTIME_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
CHIPYARD_ROOT="$(cd "${PIPELINE_RUNTIME_DIR}/../../../../.." && pwd)"
HYBRIDMAPPER_ROOT="${CHIPYARD_ROOT}/conference/HybridMapper"
PIPELINE_RUNTIME_ARTIFACT_DIR="${HYBRIDMAPPER_ROOT}/output/pipeline_runtime/bertmini"
PIPELINE_RUNTIME_EXPORTER="${HYBRIDMAPPER_ROOT}/scripts/create-pipeline-runtime-artifacts.py"
HARDWARE_TARGETS_YAML="${HARDWARE_TARGETS_YAML:-${HYBRIDMAPPER_ROOT}/config/pipeline_runtime_hardware_targets.yaml}"
ARTIFACT_AUDITOR="${PIPELINE_RUNTIME_DIR}/scripts/audit_pipeline_runtime_artifact.py"
PYTHON_BIN_DEFAULT="${CHIPYARD_ROOT}/.conda-env/bin/python"
if [ -x "${PYTHON_BIN_DEFAULT}" ]; then
  PYTHON_BIN_FALLBACK="${PYTHON_BIN_DEFAULT}"
else
  PYTHON_BIN_FALLBACK="$(command -v python3)"
fi
PYTHON_BIN="${PYTHON_BIN:-${PYTHON_BIN_FALLBACK}}"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128}"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-host-traces}"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
SKIP_BUILD="${SKIP_BUILD:-0}"
SKIP_EXPORT="${SKIP_EXPORT:-0}"
SKIP_AUDIT="${SKIP_AUDIT:-0}"
WATCHDOG_MS="${WATCHDOG_MS:-300000}"
BATCH="${BATCH:-8}"
NUM_CORES="${NUM_CORES:-4}"
NUM_GEMMINI="${NUM_GEMMINI:-12}"
NUM_DMA="${NUM_DMA:-}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-1}"

require_file() {
  if [ ! -f "$1" ]; then
    echo "missing required artifact: $1" >&2
    exit 1
  fi
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

resolve_manager_layout

if [ "${SKIP_BUILD}" != "1" ]; then
  make -C "${PIPELINE_RUNTIME_DIR}" clean all
fi

if [ "${SKIP_EXPORT}" != "1" ]; then
  "${PYTHON_BIN}" "${PIPELINE_RUNTIME_EXPORTER}" \
    --model bertmini \
    --target-keys "${TARGET_KEY}" \
    --hardware-targets-yaml "${HARDWARE_TARGETS_YAML}"
fi

require_file "${PIPELINE_RUNTIME_DIR}/pipeline_runtime"
require_file "${ARTIFACT_AUDITOR}"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/model.layers.yaml"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_model.bin"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/runtime_input.${TARGET_KEY}.bin"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/hardware_target.${TARGET_KEY}.yaml"

mkdir -p "${TRACE_DIR}"

for method in ${METHODS}; do
  require_file "${PIPELINE_RUNTIME_ARTIFACT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
  if [ "${SKIP_AUDIT}" != "1" ]; then
    "${PYTHON_BIN}" "${ARTIFACT_AUDITOR}" \
      --pipeline-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml" \
      --hardware-yaml "${PIPELINE_RUNTIME_ARTIFACT_DIR}/hardware_target.${TARGET_KEY}.yaml" \
      --expect-target-key "${TARGET_KEY}"
  fi
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
    --num-cores "${NUM_CORES}" \
    --num-gemmini-mgrs "${NUM_GEMMINI}" \
    --num-dma-mgrs "${NUM_DMA}" \
    --gemmini-base-id "${GEMMINI_BASE_ID}" \
    --dma-base-id "${DMA_BASE_ID}" \
    --pair-manager-mode "${PAIR_MANAGER_MODE}" \
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
    --num-cores "${NUM_CORES}" \
    --num-gemmini-mgrs "${NUM_GEMMINI}" \
    --num-dma-mgrs "${NUM_DMA}" \
    --gemmini-base-id "${GEMMINI_BASE_ID}" \
    --dma-base-id "${DMA_BASE_ID}" \
    --pair-manager-mode "${PAIR_MANAGER_MODE}" \
    --pages-per-acc 1024 \
    --watchdog-ms "${WATCHDOG_MS}" \
    --batch "${BATCH}" \
    --trace "${TRACE_DIR}/${method}.fpga.trace"
  echo "[bertmini-host] PASS method=${method}"
done

echo "BERTMINI_HOST_CLOSURE_PASS"
