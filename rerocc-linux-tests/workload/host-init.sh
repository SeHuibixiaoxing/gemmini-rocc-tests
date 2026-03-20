#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_TESTS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_TESTS_DIR}/.." && pwd)"
CHIPYARD_ROOT="$(cd "${GEMMINI_ROCC_TESTS_DIR}/../../../.." && pwd)"
PIPELINE_RUNTIME_SRC_DIR="${GEMMINI_ROCC_TESTS_DIR}/pipeline-runtime"
OVERLAY_ROOT_DIR="${SCRIPT_DIR}/overlay/root/rerocc-linux-tests"
PIPELINE_RUNTIME_OVERLAY_DIR="${OVERLAY_ROOT_DIR}/pipeline-runtime"
PIPELINE_RUNTIME_BERT_DIR="${PIPELINE_RUNTIME_OVERLAY_DIR}/bertmini"
DEFAULT_HYBRIDMAPPER_BERT_DIR="${CHIPYARD_ROOT}/conference/HybridMapper/output/pipeline_runtime/bertmini"
PIPELINE_RUNTIME_ARTIFACT_DIR="${PIPELINE_RUNTIME_ARTIFACT_DIR:-}"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024}"

HOST_INIT_CHECK_ONLY="${HOST_INIT_CHECK_ONLY:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
PIPELINE_RUNTIME_PROGRESS="${PIPELINE_RUNTIME_PROGRESS:-0}"
BUILD_DIR="${GEMMINI_ROCC_TESTS_DIR}/build"
REROCC_LINUX_BUILD_DIR="${BUILD_DIR}/rerocc-linux-tests"

require_file() {
  local path="$1"
  if [ ! -f "${path}" ]; then
    echo "missing required artifact: ${path}" >&2
    exit 1
  fi
}

copy_required_file() {
  local src="$1"
  local dst="$2"
  require_file "${src}"
  mkdir -p "$(dirname "${dst}")"
  if [ -e "${dst}" ] && [ "$(readlink -f "${src}")" = "$(readlink -f "${dst}")" ]; then
    return 0
  fi
  cp -f "${src}" "${dst}"
}

resolve_runtime_artifact_dir() {
  local candidate
  for candidate in \
    "${PIPELINE_RUNTIME_ARTIFACT_DIR}" \
    "${DEFAULT_HYBRIDMAPPER_BERT_DIR}" \
    "${PIPELINE_RUNTIME_BERT_DIR}"; do
    if [ -n "${candidate}" ] && [ -f "${candidate}/model.layers.yaml" ] && \
       [ -f "${candidate}/runtime_model.bin" ] && \
       [ -f "${candidate}/runtime_input.${TARGET_KEY}.bin" ] && \
       [ -f "${candidate}/gemmini_layer_mapping.${TARGET_KEY}.yaml" ]; then
      printf '%s\n' "$(cd "${candidate}" && pwd)"
      return 0
    fi
  done

  echo "missing bertmini runtime artifacts. Set PIPELINE_RUNTIME_ARTIFACT_DIR, regenerate ${DEFAULT_HYBRIDMAPPER_BERT_DIR}, or ensure the overlay cache exists at ${PIPELINE_RUNTIME_BERT_DIR}." >&2
  exit 1
}

find_linux_cc() {
  if command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
    command -v riscv64-unknown-linux-gnu-gcc
    return 0
  fi
  if command -v riscv64-linux-gnu-gcc >/dev/null 2>&1; then
    command -v riscv64-linux-gnu-gcc
    return 0
  fi
  return 1
}

check_runtime_artifacts() {
  require_file "${RUNTIME_BERT_DIR}/model.layers.yaml"
  require_file "${RUNTIME_BERT_DIR}/runtime_model.bin"
  require_file "${RUNTIME_BERT_DIR}/runtime_input.${TARGET_KEY}.bin"
  require_file "${RUNTIME_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
  for method in ours2 gemini2 tangram2; do
    require_file "${RUNTIME_BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
    require_file "${RUNTIME_BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
  done
}

check_built_linux_binaries() {
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_gemmini_conv_matrix-linux"
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_dma_matrix-linux"
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
}

verify_pipeline_runtime_binary() {
  local bin="$1"
  require_file "${bin}"
  if [ "${PIPELINE_RUNTIME_PROGRESS}" != "0" ]; then
    if ! LC_ALL=C grep -aFq "[prt-progress] runtime begin backend=%u batch=%u watchdog_ms=%u" "${bin}"; then
      echo "pipeline runtime binary missing expected early-init progress string: ${bin}" >&2
      exit 1
    fi
  fi
}

build_linux_binaries() {
  local linux_cc
  linux_cc="$(find_linux_cc || true)"
  if [ -z "${linux_cc}" ]; then
    echo "missing RISC-V Linux compiler. Install riscv64-unknown-linux-gnu-gcc or riscv64-linux-gnu-gcc, or rerun with SKIP_BUILD=1 if build/rerocc-linux-tests already contains the Linux binaries." >&2
    exit 1
  fi

  echo "Building rerocc-linux-tests binaries with ${linux_cc} (PIPELINE_RUNTIME_PROGRESS=${PIPELINE_RUNTIME_PROGRESS})"
  pushd "${GEMMINI_ROCC_TESTS_DIR}" >/dev/null
  autoconf
  mkdir -p "${BUILD_DIR}"
  pushd "${BUILD_DIR}" >/dev/null
  ../configure
  make \
    CC_LINUX="${linux_cc}" \
    TARGET=riscv64-unknown-linux-gnu- \
    PIPELINE_RUNTIME_PROGRESS="${PIPELINE_RUNTIME_PROGRESS}" \
    -j rerocc-linux-tests
  popd >/dev/null
  rebuild_pipeline_runtime_binary "${linux_cc}"
  popd >/dev/null
}

rebuild_pipeline_runtime_binary() {
  local linux_cc="$1"

  mkdir -p "${REROCC_LINUX_BUILD_DIR}"
  rm -f "${REROCC_LINUX_BUILD_DIR}/rerocc_pipeline_runtime-linux"
  rm -rf "${REROCC_LINUX_BUILD_DIR}/.pipeline_runtime_objs"
  make \
    -C "${REROCC_LINUX_BUILD_DIR}" \
    -f "${REROCC_TESTS_DIR}/Makefile" \
    abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
    src_dir="${REROCC_TESTS_DIR}" \
    XLEN=64 \
    CC_LINUX="${linux_cc}" \
    PIPELINE_RUNTIME_PROGRESS="${PIPELINE_RUNTIME_PROGRESS}" \
    rerocc_pipeline_runtime-linux
}

stage_overlay() {
  mkdir -p "${OVERLAY_ROOT_DIR}"
  rm -rf "${PIPELINE_RUNTIME_OVERLAY_DIR}"
  mkdir -p "${PIPELINE_RUNTIME_BERT_DIR}"

  cp -f "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/"*-linux "${OVERLAY_ROOT_DIR}/"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux" \
    "${PIPELINE_RUNTIME_OVERLAY_DIR}/rerocc_pipeline_runtime-linux"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests/run_rerocc_lc_linux.sh" \
    "${OVERLAY_ROOT_DIR}/run_rerocc_lc_linux.sh"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh" \
    "${OVERLAY_ROOT_DIR}/run_rerocc_pipeline_runtime_bertmini.sh"
  chmod +x "${OVERLAY_ROOT_DIR}/run_rerocc_lc_linux.sh"
  chmod +x "${OVERLAY_ROOT_DIR}/run_rerocc_pipeline_runtime_bertmini.sh"
  chmod +x "${PIPELINE_RUNTIME_OVERLAY_DIR}/rerocc_pipeline_runtime-linux"

  copy_required_file "${RUNTIME_BERT_DIR}/model.layers.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/model.layers.yaml"
  copy_required_file "${RUNTIME_BERT_DIR}/runtime_model.bin" "${PIPELINE_RUNTIME_BERT_DIR}/runtime_model.bin"
  copy_required_file "${RUNTIME_BERT_DIR}/runtime_input.${TARGET_KEY}.bin" "${PIPELINE_RUNTIME_BERT_DIR}/runtime_input.${TARGET_KEY}.bin"
  copy_required_file "${RUNTIME_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
  if [ -f "${RUNTIME_BERT_DIR}/manifest.yaml" ]; then
    copy_required_file "${RUNTIME_BERT_DIR}/manifest.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/manifest.yaml"
  fi
  if [ -f "${RUNTIME_BERT_DIR}/hardware_target.${TARGET_KEY}.yaml" ]; then
    copy_required_file \
      "${RUNTIME_BERT_DIR}/hardware_target.${TARGET_KEY}.yaml" \
      "${PIPELINE_RUNTIME_BERT_DIR}/hardware_target.${TARGET_KEY}.yaml"
  fi

  for method in ours2 gemini2 tangram2; do
    copy_required_file \
      "${RUNTIME_BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml" \
      "${PIPELINE_RUNTIME_BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
    copy_required_file \
      "${RUNTIME_BERT_DIR}/golden.${TARGET_KEY}.${method}.bin" \
      "${PIPELINE_RUNTIME_BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
  done
}

RUNTIME_BERT_DIR="$(resolve_runtime_artifact_dir)"
check_runtime_artifacts

if [ "${HOST_INIT_CHECK_ONLY}" = "1" ]; then
  echo "host-init check-only PASS"
  exit 0
fi

if [ "${SKIP_BUILD}" = "1" ]; then
  echo "Skipping rerocc-linux-tests build; using existing build/rerocc-linux-tests outputs"
else
  build_linux_binaries
fi

check_built_linux_binaries
verify_pipeline_runtime_binary "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
stage_overlay

echo "host-init overlay stage PASS"
