#!/bin/sh
set -eu

GUEST_ENV_PATH="${PIPELINE_RUNTIME_GUEST_ENV_PATH:-/firemarshal.env}"
if [ -r "${GUEST_ENV_PATH}" ]; then
  # shellcheck disable=SC1090
  . "${GUEST_ENV_PATH}"
fi

LOG_DIR="${PIPELINE_RUNTIME_LOG_DIR:-/root/pipeline-runtime-debug}"
INFO_PATH="${PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH:-${LOG_DIR}/local-gdb.info}"
LOG_PATH="${PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH:-${LOG_DIR}/local-gdb.log}"
GDB_TOOL="${PIPELINE_RUNTIME_LOCAL_GDB_TOOL:-/usr/bin/gdb}"
GDB_CMDS="${PIPELINE_RUNTIME_LOCAL_GDB_CMDS:-/tmp/pipeline-runtime-local-gdb.gdb}"
GDB_TIMEOUT_SECS="${PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS:-900}"
GDB_STATUS_INTERVAL_SECS="${PIPELINE_RUNTIME_LOCAL_GDB_STATUS_INTERVAL_SECS:-30}"
GDB_CONTINUE_AFTER_MAIN="${PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN:-1}"
GDB_EXTRA_BREAKPOINTS="${PIPELINE_RUNTIME_LOCAL_GDB_EXTRA_BREAKPOINTS:-}"
GDB_TRACE_BREAKPOINTS="${PIPELINE_RUNTIME_LOCAL_GDB_TRACE_BREAKPOINTS:-}"
GDB_TRACE_IGNORE_COUNTS="${PIPELINE_RUNTIME_LOCAL_GDB_TRACE_IGNORE_COUNTS:-}"
GDB_TRACE_CONDITIONS="${PIPELINE_RUNTIME_LOCAL_GDB_TRACE_CONDITIONS:-}"
GDB_TRACE_STATE="${PIPELINE_RUNTIME_LOCAL_GDB_TRACE_STATE:-1}"
GDB_STREAM_CONSOLE="${PIPELINE_RUNTIME_LOCAL_GDB_STREAM_CONSOLE:-0}"
METHOD="${PIPELINE_RUNTIME_LOCAL_GDB_METHOD:-ours2}"

ROOT_DIR="/root/rerocc-linux-tests/pipeline-runtime"
BERT_DIR="${ROOT_DIR}/bertmini"
TARGET_BIN="${PIPELINE_RUNTIME_LOCAL_GDB_BIN:-${ROOT_DIR}/rerocc_pipeline_runtime-linux}"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128}"
TARGET_BATCH="${TARGET_BATCH:-8}"
NUM_CORES="${NUM_CORES:-4}"
NUM_GEMMINI="${NUM_GEMMINI:-12}"
NUM_DMA="${NUM_DMA:-12}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-0}"
PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-1}"
PAGES_PER_ACC="${PAGES_PER_ACC:-1024}"
WATCHDOG_MS="${WATCHDOG_MS:-600000}"
EXPORT_DMA_TIMEOUT_MS="${EXPORT_DMA_TIMEOUT_MS:-0}"
DUMMY_GEMMINI_MODE="${DUMMY_GEMMINI_MODE:-1}"
PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD="${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_INPUT_LOAD="${PIPELINE_RUNTIME_SKIP_INPUT_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK="${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE:-1}"
PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE="${PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START:-}"
PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END:-}"
PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START:-4294967295}"
PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END:-4294967295}"
PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE="${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE:-32}"
PIPELINE_RUNTIME_BREADCRUMB_ENABLE="${PIPELINE_RUNTIME_BREADCRUMB_ENABLE:-1}"
PIPELINE_RUNTIME_BREADCRUMB_PATH="${PIPELINE_RUNTIME_BREADCRUMB_PATH:-${LOG_DIR}/bertmini-batch8.breadcrumb.bin}"
PIPELINE_RUNTIME_BREADCRUMB_SEGMENT="${PIPELINE_RUNTIME_BREADCRUMB_SEGMENT:-0}"
PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE:-0}"
PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE:-0}"
PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH:-}"
PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS:-0}"
PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS:-0}"
PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_GUEST_LOG_PATH="${PIPELINE_RUNTIME_GUEST_LOG_PATH:-${LOG_DIR}/bertmini-batch8.log}"
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE:-0}"
PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH:-${LOG_DIR}/bertmini-batch8.deep.log}"
PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE:-0}"
PIPELINE_RUNTIME_AUDIT_LOG_PATH="${PIPELINE_RUNTIME_AUDIT_LOG_PATH:-${LOG_DIR}/bertmini-batch8.audit.log}"
PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE="${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE:-0}"
PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH="${PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH:-${LOG_DIR}/bertmini-batch8.checkpoint.log}"
TRACE_ENABLE="${TRACE_ENABLE:-0}"
TRACE_DIR="${TRACE_DIR:-${LOG_DIR}/traces}"
HUGETLB_PAGES="${HUGETLB_PAGES:-1}"
HUGETLB_MOUNT="${HUGETLB_MOUNT:-/dev/hugepages}"

MODEL_YAML="${BERT_DIR}/model.layers.yaml"
LAYER_MAPPING_YAML="${BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
MODEL_BIN="${BERT_DIR}/runtime_model.bin"
INPUT_BIN="${BERT_DIR}/runtime_input.${TARGET_KEY}.bin"
PIPELINE_YAML="${BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${METHOD}.yaml"
GOLDEN_BIN="${BERT_DIR}/golden.${TARGET_KEY}.${METHOD}.bin"
TRACE_PATH="${TRACE_DIR}/${METHOD}.trace"

mkdir -p "${LOG_DIR}" "${TRACE_DIR}" "$(dirname "${INFO_PATH}")" "$(dirname "${LOG_PATH}")"
: > "${LOG_PATH}"
: > "${PIPELINE_RUNTIME_GUEST_LOG_PATH}"
: > "${PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH}"
: > "${PIPELINE_RUNTIME_AUDIT_LOG_PATH}"
: > "${PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH}"

write_info() {
  phase="$1"
  extra="${2:-}"
  {
    printf 'phase=%s\n' "${phase}"
    printf 'gdb_tool=%s\n' "${GDB_TOOL}"
    printf 'target_bin=%s\n' "${TARGET_BIN}"
    printf 'method=%s\n' "${METHOD}"
    printf 'dma_force_direct_enable=%s\n' "${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}"
    printf 'dma_bounce_bypass_enable=%s\n' "${PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE}"
    printf 'trace_state=%s\n' "${GDB_TRACE_STATE}"
    if [ -n "${extra}" ]; then
      printf 'extra=%s\n' "${extra}"
    fi
  } > "${INFO_PATH}"
}

announce_console() {
  phase="$1"
  printf '[pipeline-local-gdb] phase=%s method=%s gdb_tool=%s target_bin=%s\n' \
    "${phase}" "${METHOD}" "${GDB_TOOL}" "${TARGET_BIN}" >/dev/console
}

announce_status() {
  phase="$1"
  extra="${2:-}"
  if [ -n "${extra}" ]; then
    printf '[pipeline-local-gdb] phase=%s %s method=%s\n' \
      "${phase}" "${extra}" "${METHOD}" >/dev/console
  else
    announce_console "${phase}"
  fi
}

log() {
  printf '[pipeline-local-gdb] %s\n' "$*" >> "${LOG_PATH}"
}

meminfo_value() {
  awk -v key="$1" '$1 == key ":" { print $2; exit }' /proc/meminfo 2>/dev/null
}

prepare_hugetlb() {
  log "hugetlb before total=$(meminfo_value HugePages_Total || true) free=$(meminfo_value HugePages_Free || true)"
  if [ -w /proc/sys/vm/nr_hugepages ]; then
    echo "${HUGETLB_PAGES}" > /proc/sys/vm/nr_hugepages || true
  fi
  mkdir -p "${HUGETLB_MOUNT}"
  if ! grep -qs " ${HUGETLB_MOUNT} " /proc/mounts; then
    mount -t hugetlbfs none "${HUGETLB_MOUNT}" || true
  fi
  log "hugetlb after total=$(meminfo_value HugePages_Total || true) free=$(meminfo_value HugePages_Free || true)"
}

resolve_manager_layout() {
  if [ "${PAIR_MANAGER_MODE}" = "1" ]; then
    [ -n "${NUM_DMA}" ] || NUM_DMA="${NUM_GEMMINI}"
    if [ "${NUM_DMA}" != "${NUM_GEMMINI}" ]; then
      log "pair-manager mode requires NUM_DMA=${NUM_DMA} to match NUM_GEMMINI=${NUM_GEMMINI}"
      exit 2
    fi
    if [ -n "${DMA_BASE_ID}" ] && [ "${DMA_BASE_ID}" != "${GEMMINI_BASE_ID}" ]; then
      log "pair-manager mode requires DMA_BASE_ID=${DMA_BASE_ID} to match GEMMINI_BASE_ID=${GEMMINI_BASE_ID}"
      exit 2
    fi
    DMA_BASE_ID="${GEMMINI_BASE_ID}"
    return
  fi

  [ -n "${NUM_DMA}" ] || NUM_DMA="${NUM_GEMMINI}"
  [ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID=$((GEMMINI_BASE_ID + NUM_GEMMINI))
}

require_executable() {
  path="$1"
  label="$2"
  if [ ! -x "${path}" ]; then
    log "missing executable ${label}: ${path}"
    write_info "missing-${label}" "path=${path}"
    announce_console "missing-${label}"
    sync
    poweroff -f || poweroff || true
    exit 1
  fi
}

require_file() {
  path="$1"
  label="$2"
  if [ ! -f "${path}" ]; then
    log "missing file ${label}: ${path}"
    write_info "missing-${label}" "path=${path}"
    announce_console "missing-${label}"
    sync
    poweroff -f || poweroff || true
    exit 1
  fi
}

resolve_manager_layout
prepare_hugetlb
require_executable "${GDB_TOOL}" "gdb"
require_executable "${TARGET_BIN}" "target-bin"
require_file "${MODEL_YAML}" "model-yaml"
require_file "${LAYER_MAPPING_YAML}" "layer-mapping-yaml"
require_file "${PIPELINE_YAML}" "pipeline-yaml"

trace_ignore_count_at() {
  want_idx="$1"
  cur_idx=1
  for count in ${GDB_TRACE_IGNORE_COUNTS}; do
    if [ "${cur_idx}" -eq "${want_idx}" ]; then
      printf '%s' "${count}"
      return
    fi
    cur_idx=$((cur_idx + 1))
  done
}

trace_condition_at() {
  want_idx="$1"
  cur_idx=1
  rest="${GDB_TRACE_CONDITIONS}"
  while :; do
    case "${rest}" in
      *';;'*)
        condition="${rest%%;;*}"
        rest="${rest#*;;}"
        ;;
      *)
        condition="${rest}"
        rest=""
        ;;
    esac
    if [ "${cur_idx}" -eq "${want_idx}" ]; then
      printf '%s' "${condition}"
      return
    fi
    if [ -z "${rest}" ]; then
      break
    fi
    cur_idx=$((cur_idx + 1))
  done
}

set -- \
  --backend fpga \
  --model-yaml "${MODEL_YAML}" \
  --layer-mapping-yaml "${LAYER_MAPPING_YAML}" \
  --pipeline-yaml "${PIPELINE_YAML}" \
  --batch "${TARGET_BATCH}" \
  --num-cores "${NUM_CORES}" \
  --num-gemmini-mgrs "${NUM_GEMMINI}" \
  --num-dma-mgrs "${NUM_DMA}" \
  --pages-per-acc "${PAGES_PER_ACC}" \
  --gemmini-base-id "${GEMMINI_BASE_ID}" \
  --dma-base-id "${DMA_BASE_ID}" \
  --pair-manager-mode "${PAIR_MANAGER_MODE}" \
  --spm-pt-require-hugetlb 1 \
  --watchdog-ms "${WATCHDOG_MS}" \
  --export-dma-timeout-ms "${EXPORT_DMA_TIMEOUT_MS}"

if [ "${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}" != "0" ]; then
  set -- "$@" --skip-model-bin-load
else
  require_file "${MODEL_BIN}" "model-bin"
  set -- "$@" --model-bin "${MODEL_BIN}"
fi
if [ "${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}" != "0" ]; then
  set -- "$@" --skip-input-load
else
  require_file "${INPUT_BIN}" "input-bin"
  set -- "$@" --input "${INPUT_BIN}"
fi
if [ "${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}" != "0" ]; then
  set -- "$@" --skip-golden-check
elif [ "${GOLDEN_CHECK_ENABLE:-0}" != "0" ]; then
  require_file "${GOLDEN_BIN}" "golden-bin"
  set -- "$@" --golden "${GOLDEN_BIN}"
fi
if [ "${TRACE_ENABLE}" != "0" ]; then
  : > "${TRACE_PATH}"
  set -- "$@" --trace "${TRACE_PATH}"
fi

{
  printf 'set pagination off\n'
  printf 'set confirm off\n'
  printf 'set print thread-events off\n'
  printf 'set debuginfod enabled off\n'
  printf 'set auto-load safe-path /\n'
  printf 'set environment PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE %s\n' "${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}"
  printf 'set environment PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE %s\n' "${PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE}"
  printf 'break main\n'
  for bp in ${GDB_EXTRA_BREAKPOINTS}; do
    printf 'break %s\n' "${bp}"
  done
  trace_idx=1
  for bp in ${GDB_TRACE_BREAKPOINTS}; do
    ignore_count="$(trace_ignore_count_at "${trace_idx}")"
    condition_expr="$(trace_condition_at "${trace_idx}")"
    printf 'break %s\n' "${bp}"
    if [ -n "${condition_expr}" ]; then
      printf 'condition $bpnum %s\n' "${condition_expr}"
    fi
    case "${ignore_count}" in
      ''|0) ;;
      *[!0-9]*)
        printf '# ignored invalid trace ignore count for %s: %s\n' "${bp}" "${ignore_count}"
        ;;
      *)
        printf 'ignore $bpnum %s\n' "${ignore_count}"
        ;;
    esac
    printf 'commands\n'
    printf 'silent\n'
    printf 'printf "[pipeline-local-gdb] trace-hit bp=%s\\n"\n' "${bp}"
    if [ "${GDB_TRACE_STATE}" != "0" ]; then
      printf 'p g_prt_debug_tls_state\n'
      printf 'p g_prt_debug_state\n'
      printf 'p g_prt_debug_filter\n'
    fi
    printf 'bt 8\n'
    printf 'info registers pc ra sp a0 a1 a2 a3 s0 s1\n'
    printf 'x/8i $pc\n'
    printf 'shell sync\n'
    printf 'continue\n'
    printf 'end\n'
    trace_idx=$((trace_idx + 1))
  done
  printf 'run\n'
  printf 'bt\n'
  if [ "${GDB_TRACE_STATE}" != "0" ]; then
    printf 'p g_prt_debug_tls_state\n'
    printf 'p g_prt_debug_state\n'
    printf 'p g_prt_debug_filter\n'
  fi
  printf 'info registers\n'
  printf 'x/16i $pc\n'
  if [ "${GDB_CONTINUE_AFTER_MAIN}" != "0" ]; then
    printf 'continue\n'
    printf 'thread apply all bt\n'
    if [ "${GDB_TRACE_STATE}" != "0" ]; then
      printf 'p g_prt_debug_tls_state\n'
      printf 'p g_prt_debug_state\n'
      printf 'p g_prt_debug_filter\n'
    fi
    printf 'info registers\n'
    printf 'x/16i $pc\n'
  fi
  printf 'quit\n'
} > "${GDB_CMDS}"

export PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE
export PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE
export PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END
export PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START
export PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END
export PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE
export PIPELINE_RUNTIME_BREADCRUMB_ENABLE
export PIPELINE_RUNTIME_BREADCRUMB_PATH
export PIPELINE_RUNTIME_BREADCRUMB_SEGMENT
export PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE
export PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE
export PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH
export PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS
export PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS
export PIPELINE_RUNTIME_GUEST_LOG_ENABLE
export PIPELINE_RUNTIME_GUEST_LOG_PATH
export PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE
export PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH
export PIPELINE_RUNTIME_AUDIT_LOG_ENABLE
export PIPELINE_RUNTIME_AUDIT_LOG_PATH
export PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE
export PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH

{
  printf '[pipeline-local-gdb] target_bin=%s\n' "${TARGET_BIN}"
  printf '[pipeline-local-gdb] method=%s\n' "${METHOD}"
  printf '[pipeline-local-gdb] timeout_secs=%s\n' "${GDB_TIMEOUT_SECS}"
  printf '[pipeline-local-gdb] continue_after_main=%s\n' "${GDB_CONTINUE_AFTER_MAIN}"
  printf '[pipeline-local-gdb] extra_breakpoints=%s\n' "${GDB_EXTRA_BREAKPOINTS}"
  printf '[pipeline-local-gdb] trace_breakpoints=%s\n' "${GDB_TRACE_BREAKPOINTS}"
  printf '[pipeline-local-gdb] trace_ignore_counts=%s\n' "${GDB_TRACE_IGNORE_COUNTS}"
  printf '[pipeline-local-gdb] stream_console=%s\n' "${GDB_STREAM_CONSOLE}"
  printf '[pipeline-local-gdb] dma_force_direct_enable=%s\n' "${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}"
  printf '[pipeline-local-gdb] argv:'
  printf ' %s' "$@"
  printf '\n'
} >> "${LOG_PATH}"

write_info "running"
announce_status "running" "timeout_secs=${GDB_TIMEOUT_SECS}"

set +e
tee_pid=""
gdb_fifo=""
if [ "${GDB_STREAM_CONSOLE}" != "0" ]; then
  gdb_fifo="${PIPELINE_RUNTIME_LOCAL_GDB_FIFO:-/tmp/pipeline-runtime-local-gdb.out}"
  rm -f "${gdb_fifo}"
  mkfifo "${gdb_fifo}"
  tee -a "${LOG_PATH}" < "${gdb_fifo}" >/dev/console &
  tee_pid=$!
  "${GDB_TOOL}" -q -batch -x "${GDB_CMDS}" --args "${TARGET_BIN}" "$@" > "${gdb_fifo}" 2>&1 &
  gdb_pid=$!
else
  "${GDB_TOOL}" -q -batch -x "${GDB_CMDS}" --args "${TARGET_BIN}" "$@" >> "${LOG_PATH}" 2>&1 &
  gdb_pid=$!
fi
elapsed=0
timed_out=0

while kill -0 "${gdb_pid}" 2>/dev/null; do
  sleep "${GDB_STATUS_INTERVAL_SECS}"
  elapsed=$((elapsed + GDB_STATUS_INTERVAL_SECS))
  if ! kill -0 "${gdb_pid}" 2>/dev/null; then
    break
  fi
  announce_status "still-running" "elapsed_secs=${elapsed}"
  if [ "${GDB_TIMEOUT_SECS}" -gt 0 ] && [ "${elapsed}" -ge "${GDB_TIMEOUT_SECS}" ]; then
    timed_out=1
    log "timeout elapsed_secs=${elapsed}; interrupting gdb pid=${gdb_pid}"
    announce_status "timeout" "elapsed_secs=${elapsed}"
    kill -INT "${gdb_pid}" 2>/dev/null || true
    sleep 10
    if kill -0 "${gdb_pid}" 2>/dev/null; then
      kill -TERM "${gdb_pid}" 2>/dev/null || true
      sleep 5
    fi
    if kill -0 "${gdb_pid}" 2>/dev/null; then
      kill -KILL "${gdb_pid}" 2>/dev/null || true
    fi
    break
  fi
done

wait "${gdb_pid}"
gdb_rc=$?
if [ -n "${tee_pid}" ]; then
  wait "${tee_pid}" 2>/dev/null || true
fi
if [ -n "${gdb_fifo}" ]; then
  rm -f "${gdb_fifo}"
fi
if [ "${timed_out}" -ne 0 ]; then
  gdb_rc=124
fi
set -e

log "gdb exited rc=${gdb_rc}"
{
  printf '[pipeline-local-gdb] gdb log tail begin\n'
  tail -120 "${LOG_PATH}" 2>/dev/null || true
  printf '[pipeline-local-gdb] gdb log tail end\n'
} >/dev/console

if [ "${timed_out}" -ne 0 ]; then
  write_info "timeout" "gdb_rc=${gdb_rc},elapsed_secs=${elapsed}"
elif grep -q "Inferior .* exited normally" "${LOG_PATH}" 2>/dev/null; then
  write_info "exit" "gdb_rc=${gdb_rc},inferior=exited-normally"
elif grep -q "Inferior .* exited with code 0" "${LOG_PATH}" 2>/dev/null; then
  write_info "exit" "gdb_rc=${gdb_rc},inferior=exited-code-0"
else
  write_info "exit" "gdb_rc=${gdb_rc},inferior=unknown-or-nonzero"
fi

announce_console "exit"
sync
poweroff -f || poweroff || true
exit "${gdb_rc}"
