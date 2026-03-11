#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_TESTS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_TESTS_DIR}/.." && pwd)"
OVERLAY_ROOT_DIR="${SCRIPT_DIR}/overlay/root/rerocc-linux-tests"
PIPELINE_RUNTIME_OVERLAY_DIR="${OVERLAY_ROOT_DIR}/pipeline-runtime"
PIPELINE_RUNTIME_BERT_DIR="${PIPELINE_RUNTIME_OVERLAY_DIR}/bertmini"
HYBRIDMAPPER_BERT_DIR="$(cd "${GEMMINI_ROCC_TESTS_DIR}/../../../../tmp/HybridMapper/output/pipeline/bertmini" && pwd)"

HOST_INIT_CHECK_ONLY="${HOST_INIT_CHECK_ONLY:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"

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
  cp -f "${src}" "${dst}"
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
  require_file "${HYBRIDMAPPER_BERT_DIR}/layers_gemmini.yaml"
  require_file "${HYBRIDMAPPER_BERT_DIR}/dummy_weight/model.bin"
  require_file "${HYBRIDMAPPER_BERT_DIR}/dummy_input/input.bin"
  require_file "${HYBRIDMAPPER_BERT_DIR}/dummy_input/golden/golden.bin"
  for method in ours2 gemini2 tangram2; do
    require_file "${HYBRIDMAPPER_BERT_DIR}/entire_model/2_1024_16_19_64_${method}.yaml"
  done
  if ! find "${HYBRIDMAPPER_BERT_DIR}/mapping_gemmini" -maxdepth 1 -name '*.yaml' -type f | grep -q .; then
    echo "missing required artifact: ${HYBRIDMAPPER_BERT_DIR}/mapping_gemmini/*.yaml" >&2
    exit 1
  fi
}

check_built_linux_binaries() {
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_gemmini_conv_matrix-linux"
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_dma_matrix-linux"
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
}

build_linux_binaries() {
  local linux_cc
  linux_cc="$(find_linux_cc || true)"
  if [ -z "${linux_cc}" ]; then
    echo "missing RISC-V Linux compiler. Install riscv64-unknown-linux-gnu-gcc or riscv64-linux-gnu-gcc, or rerun with SKIP_BUILD=1 if build/rerocc-linux-tests already contains the Linux binaries." >&2
    exit 1
  fi

  echo "Building rerocc-linux-tests binaries with ${linux_cc}"
  pushd "${GEMMINI_ROCC_TESTS_DIR}" >/dev/null
  autoconf
  mkdir -p build
  pushd build >/dev/null
  ../configure
  make CC_LINUX="${linux_cc}" TARGET=riscv64-unknown-linux-gnu- -j rerocc-linux-tests
  popd >/dev/null
  popd >/dev/null
}

stage_overlay() {
  mkdir -p "${OVERLAY_ROOT_DIR}"
  mkdir -p "${PIPELINE_RUNTIME_BERT_DIR}/mapping_gemmini"
  mkdir -p "${PIPELINE_RUNTIME_BERT_DIR}/entire_model"

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

  copy_required_file "${HYBRIDMAPPER_BERT_DIR}/layers_gemmini.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/layers_gemmini.yaml"
  copy_required_file "${HYBRIDMAPPER_BERT_DIR}/dummy_weight/model.bin" "${PIPELINE_RUNTIME_BERT_DIR}/model.bin"
  copy_required_file "${HYBRIDMAPPER_BERT_DIR}/dummy_input/input.bin" "${PIPELINE_RUNTIME_BERT_DIR}/input.bin"
  copy_required_file "${HYBRIDMAPPER_BERT_DIR}/dummy_input/golden/golden.bin" "${PIPELINE_RUNTIME_BERT_DIR}/golden.bin"

  for method in ours2 gemini2 tangram2; do
    copy_required_file \
      "${HYBRIDMAPPER_BERT_DIR}/entire_model/2_1024_16_19_64_${method}.yaml" \
      "${PIPELINE_RUNTIME_BERT_DIR}/entire_model/2_1024_16_19_64_${method}.yaml"
  done

  find "${HYBRIDMAPPER_BERT_DIR}/mapping_gemmini" -maxdepth 1 -name '*.yaml' -type f | while read -r src; do
    copy_required_file "${src}" "${PIPELINE_RUNTIME_BERT_DIR}/mapping_gemmini/$(basename "${src}")"
  done
}

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
stage_overlay

echo "host-init overlay stage PASS"
