#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_TESTS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_TESTS_DIR}/.." && pwd)"
CHIPYARD_ROOT="$(cd "${GEMMINI_ROCC_TESTS_DIR}/../../../.." && pwd)"

OVERLAY_COUPLEDDMA_DIR="${SCRIPT_DIR}/overlay/root/rerocc-linux-tests-coupleddma"
OVERLAY_PIPELINE_ROOT_DIR="${SCRIPT_DIR}/overlay/root/rerocc-linux-tests"
OVERLAY_FIREMARSHAL_ENV="${SCRIPT_DIR}/overlay/firemarshal.env"
PIPELINE_RUNTIME_OVERLAY_DIR="${OVERLAY_PIPELINE_ROOT_DIR}/pipeline-runtime"
PIPELINE_RUNTIME_BERT_DIR="${PIPELINE_RUNTIME_OVERLAY_DIR}/bertmini"
HYBRIDMAPPER_BERT_DIR="${CHIPYARD_ROOT}/conference/HybridMapper/output/pipeline_runtime/bertmini"
PIPELINE_RUNTIME_SCRIPTS_DIR="${GEMMINI_ROCC_TESTS_DIR}/pipeline-runtime/scripts"
MAPPING_CACHE_GENERATOR="${PIPELINE_RUNTIME_SCRIPTS_DIR}/generate_gemmini_mapping_cache.py"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024}"

HOST_INIT_CHECK_ONLY="${HOST_INIT_CHECK_ONLY:-0}"
SKIP_BUILD="${SKIP_BUILD:-0}"
ENABLE_PIPELINE_RUNTIME="${ENABLE_PIPELINE_RUNTIME:-auto}"
DUMMY_GEMMINI_MODE="${DUMMY_GEMMINI_MODE:-0}"
PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD="${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_INPUT_LOAD="${PIPELINE_RUNTIME_SKIP_INPUT_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK="${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_PROGRESS="${PIPELINE_RUNTIME_PROGRESS:-0}"
PIPELINE_RUNTIME_PROGRESS_RAW="${PIPELINE_RUNTIME_PROGRESS_RAW:-0}"
PIPELINE_RUNTIME_PROGRESS_HOT="${PIPELINE_RUNTIME_PROGRESS_HOT:-${PIPELINE_RUNTIME_PROGRESS_RAW}}"
PIPELINE_RUNTIME_GEMMINI_PHASE="${PIPELINE_RUNTIME_GEMMINI_PHASE:-0}"
PIPELINE_RUNTIME_ONLY_MARKER="${PIPELINE_RUNTIME_ONLY_MARKER:-1}"
PIPELINE_RUNTIME_CRITICAL_UART_PROBE="${PIPELINE_RUNTIME_CRITICAL_UART_PROBE:-1}"
PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST="${PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST:-0}"
PIPELINE_RUNTIME_PROGRESS_PAD_BURST="${PIPELINE_RUNTIME_PROGRESS_PAD_BURST:-0}"
PIPELINE_RUNTIME_MLOCKALL_MODE="${PIPELINE_RUNTIME_MLOCKALL_MODE:-0}"
PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE="${PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE:-0}"
PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_START="${PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_START:-}"
PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_END="${PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_END:-}"
PIPELINE_RUNTIME_MAPPING_PARSE_PROGRESS_INTERVAL="${PIPELINE_RUNTIME_MAPPING_PARSE_PROGRESS_INTERVAL:-}"
PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_START="${PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_START:-}"
PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_END="${PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_END:-}"
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
  cp -f "${src}" "${dst}"
}

ensure_generated_gemmini_params_header() {
  local dst="${GEMMINI_ROCC_TESTS_DIR}/include/gemmini_params.h"
  local fallback="${GEMMINI_ROCC_TESTS_DIR}/../libgemmini/gemmini_params.h"

  if [ -f "${dst}" ]; then
    return 0
  fi

  echo "Restoring missing generated Gemmini header ${dst} from ${fallback}"
  copy_required_file "${fallback}" "${dst}"
}

sh_single_quote() {
  local value="${1-}"
  value="${value//\'/\'\\\'\'}"
  printf "'%s'" "${value}"
}

write_pipeline_runtime_guest_env() {
  local dst="$1"
  local env_name
  local env_value
  local env_names=(
    PIPELINE_RUNTIME_PROFILE_ID
    TARGET_KEY
    METHODS
    TARGET_BATCH
    NUM_CORES
    NUM_GEMMINI
    NUM_DMA
    GEMMINI_BASE_ID
    DMA_BASE_ID
    PAIR_MANAGER_MODE
    PAGES_PER_ACC
    WATCHDOG_MS
    EXPORT_DMA_TIMEOUT_MS
    TRACE_DIR
    TRACE_ENABLE
    GOLDEN_CHECK_ENABLE
    DUMMY_GEMMINI_MODE
    PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD
    PIPELINE_RUNTIME_SKIP_INPUT_LOAD
    PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK
    PIPELINE_RUNTIME_UART_LOG_ENABLE
    PIPELINE_RUNTIME_GUEST_LOG_ENABLE
    PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE
    PIPELINE_RUNTIME_AUDIT_LOG_ENABLE
    PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE
    PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH
    PIPELINE_RUNTIME_GDBSERVER_ENABLE
    PIPELINE_RUNTIME_GDBSERVER_TOOL
    PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR
    PIPELINE_RUNTIME_GDBSERVER_PORT
    PIPELINE_RUNTIME_GDBSERVER_INFO_PATH
    PIPELINE_RUNTIME_GDBSERVER_LOG_PATH
    PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE
    PIPELINE_RUNTIME_GDBSERVER_NET_DEV
    PIPELINE_RUNTIME_GDBSERVER_ONCE
    PIPELINE_RUNTIME_GDBSERVER_CONSOLE_PATH
    PIPELINE_RUNTIME_GDB_MARKER_ENABLE
    PIPELINE_RUNTIME_GDB_MARKER_SITE
    PIPELINE_RUNTIME_GDB_MARKER_SEGMENT
    PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE
    PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE
    PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH
    PIPELINE_RUNTIME_GDB_MARKER_MANAGER
    PIPELINE_RUNTIME_GDB_MARKER_TENSOR
    PIPELINE_RUNTIME_GDB_MARKER_PAGE
    PIPELINE_RUNTIME_GDB_MARKER_TOKEN
    PIPELINE_RUNTIME_LOCAL_GDB_ENABLE
    PIPELINE_RUNTIME_LOCAL_GDB_TOOL
    PIPELINE_RUNTIME_LOCAL_GDB_BIN
    PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH
    PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH
    PIPELINE_RUNTIME_LOCAL_GDB_CMDS
    PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS
    PIPELINE_RUNTIME_LOCAL_GDB_STATUS_INTERVAL_SECS
    PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN
    PIPELINE_RUNTIME_LOCAL_GDB_EXTRA_BREAKPOINTS
    PIPELINE_RUNTIME_LOCAL_GDB_TRACE_BREAKPOINTS
    PIPELINE_RUNTIME_LOCAL_GDB_TRACE_IGNORE_COUNTS
    PIPELINE_RUNTIME_LOCAL_GDB_TRACE_CONDITIONS
    PIPELINE_RUNTIME_LOCAL_GDB_TRACE_STATE
    PIPELINE_RUNTIME_LOCAL_GDB_STREAM_CONSOLE
    PIPELINE_RUNTIME_LOCAL_GDB_METHOD
    CAPTURE_AUTO_POWEROFF
    CAPTURE_PERIODIC_SYNC_ENABLE
    CAPTURE_PERIODIC_SYNC_SECONDS
    CAPTURE_PROGRESS_PING_ENABLE
    CAPTURE_PROGRESS_PING_SECONDS
    PIPELINE_RUNTIME_CHILD_PROC_POLL_SECONDS
    PIPELINE_RUNTIME_RUNNER_EARLY_STAGE_PATH
    PIPELINE_RUNTIME_RUNNER_STAGE_PATH
    PIPELINE_RUNTIME_RUNNER_PROC_STAGE_PATH
    PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE
    PIPELINE_RUNTIME_LOG_PROFILE
    PIPELINE_RUNTIME_STDIO_CAPTURE_MODE
    PIPELINE_RUNTIME_WRAPPER_STAGE_PATH
    PIPELINE_RUNTIME_WRAPPER_PROC_STAGE_PATH
    PIPELINE_RUNTIME_MLOCKALL_MODE
    PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE
    PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_START
    PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_END
    PIPELINE_RUNTIME_MAPPING_PARSE_PROGRESS_INTERVAL
    PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_START
    PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_END
    DEEP_LOG_ENABLE
    PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE
    PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE
    PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE
    PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE
    PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START
    PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END
    PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START
    PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END
    PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PRE_SRC_NOPS
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_STAGE_ID
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TENSOR_ID
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_START
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_END
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END
    PIPELINE_RUNTIME_DMA_FIXED_LOAD_CHECKPOINT_ENABLE
    PIPELINE_RUNTIME_BREADCRUMB_ENABLE
    PIPELINE_RUNTIME_BREADCRUMB_PATH
    PIPELINE_RUNTIME_BREADCRUMB_SEGMENT
    PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE
    PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE
    PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH
    PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS
    PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS
    PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE
    PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND
    PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT
    PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE
    PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE
    PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH
    PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER
    PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID
    PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE
    PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN
    PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING
    PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET
    PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE
    PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH
    PIPELINE_RUNTIME_DEBUG_FILTER_SEGMENT
    PIPELINE_RUNTIME_DEBUG_FILTER_GLOBAL_STAGE
    PIPELINE_RUNTIME_DEBUG_FILTER_LOCAL_STAGE
    PIPELINE_RUNTIME_DEBUG_FILTER_SUBBATCH
    PIPELINE_RUNTIME_DEBUG_FILTER_PHASE
    PIPELINE_RUNTIME_DEBUG_FILTER_WAIT_PHASE
    PIPELINE_RUNTIME_DEBUG_FILTER_TENSOR
    PIPELINE_RUNTIME_DEBUG_FILTER_MANAGER
    PIPELINE_RUNTIME_DEBUG_FILTER_OPCODE
    PIPELINE_RUNTIME_DEBUG_FILTER_RR_STAGE
    PIPELINE_RUNTIME_DEBUG_FILTER_RR_MANAGER
    PIPELINE_RUNTIME_DEBUG_FILTER_RR_OPCODE
    PIPELINE_RUNTIME_DEBUG_FILTER_RR_CFG
    DEEP_LOG_SEGMENT
    DEEP_LOG_GLOBAL_STAGE
    DEEP_LOG_LOCAL_STAGE
    DEEP_LOG_SUBBATCH
    DEEP_LOG_STAGE_RADIUS
    DEEP_LOG_SUBBATCH_RADIUS
    HUGETLB_PAGES
    HUGETLB_MOUNT
  )

  mkdir -p "$(dirname "${dst}")"
  {
    echo "#!/bin/sh"
    echo "# Generated by workload host-init.sh. Rebuild the FireMarshal image after editing host-side runtime defaults."
    for env_name in "${env_names[@]}"; do
      env_value="${!env_name-}"
      if [ -z "${env_value}" ]; then
        continue
      fi
      printf 'export %s=%s\n' "${env_name}" "$(sh_single_quote "${env_value}")"
    done
  } > "${dst}"
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

pipeline_runtime_enabled() {
  case "${ENABLE_PIPELINE_RUNTIME}" in
    1|true|TRUE|yes|YES)
      return 0
      ;;
    auto|AUTO)
      [ -d "${HYBRIDMAPPER_BERT_DIR}" ]
      return $?
      ;;
    *)
      return 1
      ;;
  esac
}

pipeline_runtime_need_model_bin() {
  [ "${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}" = "0" ]
}

pipeline_runtime_need_input_bin() {
  [ "${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}" = "0" ]
}

pipeline_runtime_need_golden() {
  [ "${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}" = "0" ]
}

check_runtime_artifacts() {
  if ! pipeline_runtime_enabled; then
    return 0
  fi
  require_file "${MAPPING_CACHE_GENERATOR}"
  require_file "${HYBRIDMAPPER_BERT_DIR}/model.layers.yaml"
  require_file "${HYBRIDMAPPER_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
  if pipeline_runtime_need_model_bin; then
    require_file "${HYBRIDMAPPER_BERT_DIR}/runtime_model.bin"
  fi
  if pipeline_runtime_need_input_bin; then
    require_file "${HYBRIDMAPPER_BERT_DIR}/runtime_input.${TARGET_KEY}.bin"
  fi
  for method in ours2 gemini2 tangram2; do
    require_file "${HYBRIDMAPPER_BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
    if pipeline_runtime_need_golden; then
      require_file "${HYBRIDMAPPER_BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
    fi
  done
}

check_built_linux_binaries() {
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_gemmini_conv_matrix-linux"
  require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_dma_matrix-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_matrix_coupleddma-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_gemmini_matrix_linux_coupleddma-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_matrix_linux_coupleddma_verify-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_coverage_linux_coupleddma-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_nonblocking_linux_coupleddma-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_export_alias_uartprobe-linux"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/uartprobe_exec_stub-linux"
  require_file "${REROCC_TESTS_DIR}/workload/run_rerocc_lc_linux_regression.sh"
  require_file "${OVERLAY_COUPLEDDMA_DIR}/run_rerocc_dma_export_alias_uartprobe.sh"
  if pipeline_runtime_enabled; then
    require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
    require_file "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_ptrace_peek-linux"
  fi
}

generate_layer_mapping_cache() {
  local yaml_path="$1"
  local cache_path="${yaml_path}.cache.bin"

  require_file "${yaml_path}"
  require_file "${MAPPING_CACHE_GENERATOR}"
  if ! command -v python3 >/dev/null 2>&1; then
    echo "missing python3 for mapping cache generation" >&2
    exit 1
  fi

  python3 "${MAPPING_CACHE_GENERATOR}" "${yaml_path}" "${cache_path}"
}

verify_pipeline_runtime_binary() {
  local bin="$1"
  require_file "${bin}"
  if [ "${PIPELINE_RUNTIME_PROGRESS}" != "0" ]; then
    if ! LC_ALL=C grep -aFq "[prt-early] enter main" "${bin}"; then
      echo "pipeline runtime binary missing expected early-init progress string: ${bin}" >&2
      exit 1
    fi
  fi
  if [ "${PIPELINE_RUNTIME_PROGRESS_RAW}" != "0" ]; then
    if ! LC_ALL=C grep -aFq "[prt-raw] gg-sc-b" "${bin}"; then
      echo "pipeline runtime binary missing grouped-conv raw boundary marker: ${bin}" >&2
      exit 1
    fi
    if ! LC_ALL=C grep -aFq "[prt-raw] gis-b" "${bin}"; then
      echo "pipeline runtime binary missing grouped single-dispatch raw marker: ${bin}" >&2
      exit 1
    fi
    if ! LC_ALL=C grep -aFq "[prt-raw] cnb-b" "${bin}"; then
      echo "pipeline runtime binary missing conv-nb acquire raw marker: ${bin}" >&2
      exit 1
    fi
  fi
  if [ "${PIPELINE_RUNTIME_ONLY_MARKER}" != "0" ]; then
    if ! LC_ALL=C grep -aFq "[prt-marker]" "${bin}"; then
      echo "pipeline runtime binary missing expected marker strings: ${bin}" >&2
      exit 1
    fi
  fi
  if [ "${PIPELINE_RUNTIME_CRITICAL_UART_PROBE}" != "0" ]; then
    if ! LC_ALL=C grep -aFq "main build-config crit_probe=" "${bin}"; then
      echo "pipeline runtime binary missing runtime build-config fingerprint: ${bin}" >&2
      exit 1
    fi
    if ! LC_ALL=C grep -aFq "[prt-crit]" "${bin}"; then
      echo "pipeline runtime binary missing runtime critical probe strings: ${bin}" >&2
      exit 1
    fi
    if ! LC_ALL=C grep -aFq "conv-sync-strided acquire-snapshot" "${bin}" && \
       ! LC_ALL=C grep -aFq "conv-sync acquire-end" "${bin}"; then
      echo "pipeline runtime binary missing runtime RR acquire marker strings: ${bin}" >&2
      exit 1
    fi
  else
    if LC_ALL=C grep -aFq "[prt-crit]" "${bin}"; then
      echo "pipeline runtime binary still contains critical probe strings with PIPELINE_RUNTIME_CRITICAL_UART_PROBE=0: ${bin}" >&2
      exit 1
    fi
  fi
  if [ "${PIPELINE_RUNTIME_GEMMINI_PHASE}" = "0" ]; then
    if LC_ALL=C grep -aFq "[gemmini-phase]" "${bin}"; then
      echo "pipeline runtime binary unexpectedly contains gemmini-phase strings with PIPELINE_RUNTIME_GEMMINI_PHASE=0: ${bin}" >&2
      exit 1
    fi
  else
    if ! LC_ALL=C grep -aFq "[gemmini-phase]" "${bin}"; then
      echo "pipeline runtime binary missing gemmini-phase strings with PIPELINE_RUNTIME_GEMMINI_PHASE=${PIPELINE_RUNTIME_GEMMINI_PHASE}: ${bin}" >&2
      exit 1
    fi
  fi
  if [ "${PIPELINE_RUNTIME_CRITICAL_UART_PROBE}" != "0" ]; then
    if ! LC_ALL=C grep -aFq "[gcrit]" "${bin}"; then
      echo "pipeline runtime binary missing Gemmini critical probe strings: ${bin}" >&2
      exit 1
    fi
  else
    if LC_ALL=C grep -aFq "[gcrit]" "${bin}"; then
      echo "pipeline runtime binary still contains Gemmini critical probe strings with PIPELINE_RUNTIME_CRITICAL_UART_PROBE=0: ${bin}" >&2
      exit 1
    fi
  fi
}

build_linux_binaries() {
  local linux_cc
  linux_cc="$(find_linux_cc || true)"
  if [ -z "${linux_cc}" ]; then
    echo "missing RISC-V Linux compiler. Install riscv64-unknown-linux-gnu-gcc or riscv64-linux-gnu-gcc." >&2
    exit 1
  fi

  ensure_generated_gemmini_params_header

  echo "Building rerocc-linux-tests binaries for coupled DMA + pipeline runtime with ${linux_cc} (PIPELINE_RUNTIME_PROGRESS=${PIPELINE_RUNTIME_PROGRESS}, PIPELINE_RUNTIME_PROGRESS_RAW=${PIPELINE_RUNTIME_PROGRESS_RAW}, PIPELINE_RUNTIME_PROGRESS_HOT=${PIPELINE_RUNTIME_PROGRESS_HOT}, PIPELINE_RUNTIME_GEMMINI_PHASE=${PIPELINE_RUNTIME_GEMMINI_PHASE}, PIPELINE_RUNTIME_ONLY_MARKER=${PIPELINE_RUNTIME_ONLY_MARKER}, PIPELINE_RUNTIME_CRITICAL_UART_PROBE=${PIPELINE_RUNTIME_CRITICAL_UART_PROBE}, PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST=${PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST}, PIPELINE_RUNTIME_PROGRESS_PAD_BURST=${PIPELINE_RUNTIME_PROGRESS_PAD_BURST}, PIPELINE_RUNTIME_MLOCKALL_MODE=${PIPELINE_RUNTIME_MLOCKALL_MODE}, PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=${PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE}, PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_START=${PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_START:-unset}, PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_END=${PIPELINE_RUNTIME_MAPPING_CACHE_PROBE_END:-unset}, PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_START=${PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_START:-unset}, PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_END=${PIPELINE_RUNTIME_MAPPING_PARSE_PROBE_END:-unset})"
  pushd "${GEMMINI_ROCC_TESTS_DIR}" >/dev/null
  autoconf
  mkdir -p "${BUILD_DIR}"
  pushd "${BUILD_DIR}" >/dev/null
  ../configure
  make \
    CC_LINUX="${linux_cc}" \
    TARGET=riscv64-unknown-linux-gnu- \
    PIPELINE_RUNTIME_PROGRESS="${PIPELINE_RUNTIME_PROGRESS}" \
    PIPELINE_RUNTIME_PROGRESS_RAW="${PIPELINE_RUNTIME_PROGRESS_RAW}" \
    PIPELINE_RUNTIME_PROGRESS_HOT="${PIPELINE_RUNTIME_PROGRESS_HOT}" \
    PIPELINE_RUNTIME_GEMMINI_PHASE="${PIPELINE_RUNTIME_GEMMINI_PHASE}" \
    PIPELINE_RUNTIME_ONLY_MARKER="${PIPELINE_RUNTIME_ONLY_MARKER}" \
    PIPELINE_RUNTIME_CRITICAL_UART_PROBE="${PIPELINE_RUNTIME_CRITICAL_UART_PROBE}" \
    PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST="${PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST}" \
    PIPELINE_RUNTIME_PROGRESS_PAD_BURST="${PIPELINE_RUNTIME_PROGRESS_PAD_BURST}" \
    PIPELINE_RUNTIME_MLOCKALL_MODE="${PIPELINE_RUNTIME_MLOCKALL_MODE}" \
    -j rerocc-linux-tests
  popd >/dev/null

  if pipeline_runtime_enabled; then
    rebuild_pipeline_runtime_binary "${linux_cc}"
  fi

  mkdir -p "${OVERLAY_COUPLEDDMA_DIR}"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -pthread \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    -I"${GEMMINI_ROCC_TESTS_DIR}" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    "${REROCC_TESTS_DIR}/rerocc_dma_matrix_linux_coupleddma.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_matrix_coupleddma-linux"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -pthread \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    -I"${GEMMINI_ROCC_TESTS_DIR}" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    -I"${REROCC_TESTS_DIR}" \
    "${REROCC_TESTS_DIR}/rerocc_lc_gemmini_matrix_linux_coupleddma.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_gemmini_matrix_linux_coupleddma-linux"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    "${REROCC_TESTS_DIR}/rerocc_lc_matrix_linux_coupleddma_verify.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_matrix_linux_coupleddma_verify-linux"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -pthread \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    -I"${GEMMINI_ROCC_TESTS_DIR}" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    -I"${REROCC_TESTS_DIR}" \
    "${REROCC_TESTS_DIR}/rerocc_lc_coverage_linux_coupleddma.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_coverage_linux_coupleddma-linux"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -pthread \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    -I"${GEMMINI_ROCC_TESTS_DIR}" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    -I"${REROCC_TESTS_DIR}" \
    "${REROCC_TESTS_DIR}/rerocc_lc_nonblocking_linux_coupleddma.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_nonblocking_linux_coupleddma-linux"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    -I"${GEMMINI_ROCC_TESTS_DIR}" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    -I"${REROCC_TESTS_DIR}" \
    "${REROCC_TESTS_DIR}/rerocc_dma_export_alias_uartprobe_linux.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_export_alias_uartprobe-linux"

  "${linux_cc}" \
    -mcmodel=medany \
    -std=gnu99 \
    -O2 \
    -march=rv64gc -Wa,-march=rv64gc \
    -ffast-math \
    -fno-common \
    -fno-tree-loop-distribute-patterns \
    -I"${GEMMINI_ROCC_TESTS_DIR}" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
    -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    -I"${REROCC_TESTS_DIR}" \
    "${REROCC_TESTS_DIR}/uartprobe_exec_stub_linux.c" \
    -o "${OVERLAY_COUPLEDDMA_DIR}/uartprobe_exec_stub-linux"

  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_matrix_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_gemmini_matrix_linux_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_matrix_linux_coupleddma_verify-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_coverage_linux_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_nonblocking_linux_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_export_alias_uartprobe-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/uartprobe_exec_stub-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/run_rerocc_dma_export_alias_uartprobe.sh"
  popd >/dev/null
}

rebuild_pipeline_runtime_binary() {
  local linux_cc="$1"

  mkdir -p "${REROCC_LINUX_BUILD_DIR}"
  rm -f "${REROCC_LINUX_BUILD_DIR}/rerocc_pipeline_runtime-linux"
  rm -rf "${REROCC_LINUX_BUILD_DIR}/.pipeline_runtime_objs"
  make \
    -C "${REROCC_LINUX_BUILD_DIR}" \
    -f "${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests/Makefile" \
    abs_top_srcdir="${GEMMINI_ROCC_TESTS_DIR}" \
    src_dir="${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
    XLEN=64 \
    CC_LINUX="${linux_cc}" \
    PIPELINE_RUNTIME_PROGRESS="${PIPELINE_RUNTIME_PROGRESS}" \
    PIPELINE_RUNTIME_PROGRESS_RAW="${PIPELINE_RUNTIME_PROGRESS_RAW}" \
    PIPELINE_RUNTIME_PROGRESS_HOT="${PIPELINE_RUNTIME_PROGRESS_HOT}" \
    PIPELINE_RUNTIME_GEMMINI_PHASE="${PIPELINE_RUNTIME_GEMMINI_PHASE}" \
    PIPELINE_RUNTIME_ONLY_MARKER="${PIPELINE_RUNTIME_ONLY_MARKER}" \
    PIPELINE_RUNTIME_CRITICAL_UART_PROBE="${PIPELINE_RUNTIME_CRITICAL_UART_PROBE}" \
    PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST="${PIPELINE_RUNTIME_CRITICAL_UART_PAD_BURST}" \
    PIPELINE_RUNTIME_PROGRESS_PAD_BURST="${PIPELINE_RUNTIME_PROGRESS_PAD_BURST}" \
    PIPELINE_RUNTIME_MLOCKALL_MODE="${PIPELINE_RUNTIME_MLOCKALL_MODE}" \
    rerocc_pipeline_runtime-linux rerocc_ptrace_peek-linux
}

stage_coupleddma_overlay() {
  mkdir -p "${OVERLAY_COUPLEDDMA_DIR}"
  rm -f "${OVERLAY_COUPLEDDMA_DIR}/.pipeline_runtime_only"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_gemmini_conv_matrix-linux" \
    "${OVERLAY_COUPLEDDMA_DIR}/rerocc_gemmini_conv_matrix-linux"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_dma_matrix-linux" \
    "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_matrix-linux"
  copy_required_file \
    "${REROCC_TESTS_DIR}/workload/run_rerocc_lc_linux_regression.sh" \
    "${OVERLAY_COUPLEDDMA_DIR}/run_rerocc_lc_linux_regression.sh"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_gemmini_conv_matrix-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_matrix-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_matrix_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_gemmini_matrix_linux_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_matrix_linux_coupleddma_verify-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_coverage_linux_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_lc_nonblocking_linux_coupleddma-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/rerocc_dma_export_alias_uartprobe-linux"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/run_rerocc_dma_export_alias_uartprobe.sh"
  chmod +x "${OVERLAY_COUPLEDDMA_DIR}/run_rerocc_lc_linux_regression.sh"
}

reset_pipeline_runtime_overlay() {
  rm -rf "${OVERLAY_PIPELINE_ROOT_DIR}"
  rm -f "${OVERLAY_FIREMARSHAL_ENV}"
}

stage_pipeline_runtime_overlay() {
  rm -rf "${PIPELINE_RUNTIME_OVERLAY_DIR}"
  mkdir -p "${PIPELINE_RUNTIME_BERT_DIR}"

  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux" \
    "${PIPELINE_RUNTIME_OVERLAY_DIR}/rerocc_pipeline_runtime-linux"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_ptrace_peek-linux" \
    "${OVERLAY_PIPELINE_ROOT_DIR}/rerocc_ptrace_peek-linux"
  copy_required_file \
    "${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh" \
    "${OVERLAY_PIPELINE_ROOT_DIR}/run_rerocc_pipeline_runtime_bertmini.sh"
  copy_required_file \
    "${REROCC_TESTS_DIR}/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh" \
    "${OVERLAY_PIPELINE_ROOT_DIR}/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh"

  chmod +x "${PIPELINE_RUNTIME_OVERLAY_DIR}/rerocc_pipeline_runtime-linux"
  chmod +x "${OVERLAY_PIPELINE_ROOT_DIR}/rerocc_ptrace_peek-linux"
  chmod +x "${OVERLAY_PIPELINE_ROOT_DIR}/run_rerocc_pipeline_runtime_bertmini.sh"
  chmod +x "${OVERLAY_PIPELINE_ROOT_DIR}/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh"
  write_pipeline_runtime_guest_env "${OVERLAY_FIREMARSHAL_ENV}"

  copy_required_file "${HYBRIDMAPPER_BERT_DIR}/model.layers.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/model.layers.yaml"
  if pipeline_runtime_need_model_bin; then
    copy_required_file "${HYBRIDMAPPER_BERT_DIR}/runtime_model.bin" "${PIPELINE_RUNTIME_BERT_DIR}/runtime_model.bin"
  fi
  if pipeline_runtime_need_input_bin; then
    copy_required_file "${HYBRIDMAPPER_BERT_DIR}/runtime_input.${TARGET_KEY}.bin" "${PIPELINE_RUNTIME_BERT_DIR}/runtime_input.${TARGET_KEY}.bin"
  fi
  copy_required_file "${HYBRIDMAPPER_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
  generate_layer_mapping_cache "${PIPELINE_RUNTIME_BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
  if [ -f "${HYBRIDMAPPER_BERT_DIR}/manifest.yaml" ]; then
    copy_required_file "${HYBRIDMAPPER_BERT_DIR}/manifest.yaml" "${PIPELINE_RUNTIME_BERT_DIR}/manifest.yaml"
  fi
  if [ -f "${HYBRIDMAPPER_BERT_DIR}/hardware_target.${TARGET_KEY}.yaml" ]; then
    copy_required_file \
      "${HYBRIDMAPPER_BERT_DIR}/hardware_target.${TARGET_KEY}.yaml" \
      "${PIPELINE_RUNTIME_BERT_DIR}/hardware_target.${TARGET_KEY}.yaml"
  fi

  for method in ours2 gemini2 tangram2; do
    copy_required_file \
      "${HYBRIDMAPPER_BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml" \
      "${PIPELINE_RUNTIME_BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
    if pipeline_runtime_need_golden; then
      copy_required_file \
        "${HYBRIDMAPPER_BERT_DIR}/golden.${TARGET_KEY}.${method}.bin" \
        "${PIPELINE_RUNTIME_BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
    fi
  done
}

check_runtime_artifacts

if [ "${HOST_INIT_CHECK_ONLY}" = "1" ]; then
  echo "host-init check-only PASS"
  exit 0
fi

if [ "${SKIP_BUILD}" = "1" ]; then
  echo "Skipping rerocc-linux-tests build; using existing outputs"
else
  build_linux_binaries
fi

check_built_linux_binaries
if pipeline_runtime_enabled; then
  verify_pipeline_runtime_binary "${GEMMINI_ROCC_TESTS_DIR}/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
fi
stage_coupleddma_overlay
reset_pipeline_runtime_overlay
if pipeline_runtime_enabled; then
  stage_pipeline_runtime_overlay
  if [ "${PIPELINE_RUNTIME_ONLY_MARKER}" != "0" ]; then
    printf 'pipeline_runtime_only=1\n' > "${OVERLAY_COUPLEDDMA_DIR}/.pipeline_runtime_only"
  fi
fi

echo "host-init overlay stage PASS"
