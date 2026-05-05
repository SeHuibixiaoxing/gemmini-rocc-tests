#!/bin/sh
set -eu

GUEST_ENV_PATH="${PIPELINE_RUNTIME_GUEST_ENV_PATH:-/firemarshal.env}"
if [ -r "${GUEST_ENV_PATH}" ]; then
  # shellcheck disable=SC1090
  . "${GUEST_ENV_PATH}"
fi

GUEST_RUNNER="/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh"
LOG_DIR="${PIPELINE_RUNTIME_LOG_DIR:-/root/pipeline-runtime-debug}"
LOG_PATH="${PIPELINE_RUNTIME_LOG_PATH:-${LOG_DIR}/bertmini-batch8.log}"
DEEP_LOG_PATH="${PIPELINE_RUNTIME_DEEP_LOG_PATH:-${LOG_DIR}/bertmini-batch8.deep.log}"
AUDIT_LOG_PATH="${PIPELINE_RUNTIME_AUDIT_LOG_PATH:-${LOG_DIR}/bertmini-batch8.audit.log}"
CHECKPOINT_LOG_PATH="${PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH:-${LOG_DIR}/bertmini-batch8.checkpoint.log}"
BREADCRUMB_PATH="${PIPELINE_RUNTIME_BREADCRUMB_PATH:-${LOG_DIR}/bertmini-batch8.breadcrumb.bin}"
STATUS_PATH="${PIPELINE_RUNTIME_STATUS_PATH:-${LOG_DIR}/bertmini-batch8.status}"
RUNNER_EARLY_STAGE_PATH="${PIPELINE_RUNTIME_RUNNER_EARLY_STAGE_PATH:-${LOG_DIR}/bertmini-batch8.runner-early.stage}"
RUNNER_STAGE_PATH="${PIPELINE_RUNTIME_RUNNER_STAGE_PATH:-${LOG_DIR}/bertmini-batch8.runner.stage}"
RUNNER_PROC_STAGE_PATH="${PIPELINE_RUNTIME_RUNNER_PROC_STAGE_PATH:-${LOG_DIR}/bertmini-batch8.runner-proc.stage}"
WRAPPER_STAGE_PATH="${PIPELINE_RUNTIME_WRAPPER_STAGE_PATH:-${LOG_DIR}/bertmini-batch8.wrapper.stage}"
WRAPPER_PROC_STAGE_PATH="${PIPELINE_RUNTIME_WRAPPER_PROC_STAGE_PATH:-${LOG_DIR}/bertmini-batch8.wrapper-proc.stage}"
TRACE_DIR="${TRACE_DIR:-${LOG_DIR}/traces}"
CAPTURE_AUTO_POWEROFF="${CAPTURE_AUTO_POWEROFF:-1}"
CAPTURE_PERIODIC_SYNC_ENABLE="${CAPTURE_PERIODIC_SYNC_ENABLE:-1}"
CAPTURE_PERIODIC_SYNC_SECONDS="${CAPTURE_PERIODIC_SYNC_SECONDS:-1}"
CAPTURE_PROGRESS_PING_ENABLE="${CAPTURE_PROGRESS_PING_ENABLE:-0}"
CAPTURE_PROGRESS_PING_SECONDS="${CAPTURE_PROGRESS_PING_SECONDS:-30}"
CHILD_PROC_POLL_SECONDS="${PIPELINE_RUNTIME_CHILD_PROC_POLL_SECONDS:-5}"
GOLDEN_CHECK_ENABLE="${GOLDEN_CHECK_ENABLE:-0}"
DUMMY_GEMMINI_MODE="${DUMMY_GEMMINI_MODE:-0}"
PIPELINE_RUNTIME_PROFILE_ID="${PIPELINE_RUNTIME_PROFILE_ID:-unset}"
PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD="${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_INPUT_LOAD="${PIPELINE_RUNTIME_SKIP_INPUT_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK="${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_UART_LOG_ENABLE="${PIPELINE_RUNTIME_UART_LOG_ENABLE:-0}"
PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE="${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE:-0}"
PIPELINE_RUNTIME_STDIO_CAPTURE_MODE="${PIPELINE_RUNTIME_STDIO_CAPTURE_MODE:-log}"
PIPELINE_RUNTIME_LOG_PROFILE="${PIPELINE_RUNTIME_LOG_PROFILE:-manual}"
PIPELINE_RUNTIME_GDBSERVER_ENABLE="${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}"
PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR="${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR:-0.0.0.0}"
PIPELINE_RUNTIME_GDBSERVER_PORT="${PIPELINE_RUNTIME_GDBSERVER_PORT:-2345}"
PIPELINE_RUNTIME_GDBSERVER_INFO_PATH="${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH:-${LOG_DIR}/bertmini-batch8.gdbserver.info}"
PIPELINE_RUNTIME_GDBSERVER_LOG_PATH="${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH:-${LOG_DIR}/bertmini-batch8.gdbserver.log}"
PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE="${PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE:-1}"
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE:-0}"
DEEP_LOG_ENABLE="${DEEP_LOG_ENABLE:-1}"
TRACE_ENABLE="${TRACE_ENABLE:-0}"
sync_pid=""
periodic_sync_pid=""
progress_ping_pid=""
child_proc_diag_pid=""

apply_log_profile() {
  case "${PIPELINE_RUNTIME_LOG_PROFILE}" in
    manual|'')
      ;;
    coarse)
      PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=0
      DEEP_LOG_ENABLE=0
      ;;
    fine_segment)
      PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE=1
      DEEP_LOG_ENABLE=1
      : "${DEEP_LOG_SEGMENT:=0}"
      ;;
    *)
      echo "[firemarshal] unknown PIPELINE_RUNTIME_LOG_PROFILE: ${PIPELINE_RUNTIME_LOG_PROFILE}" >&2
      exit 2
      ;;
  esac
}

apply_log_profile

flush_capture_state() {
  if [ -n "${sync_pid}" ]; then
    if kill -0 "${sync_pid}" 2>/dev/null; then
      return 0
    fi
    wait "${sync_pid}" 2>/dev/null || true
  fi

  (
    sync >/dev/null 2>&1 || true
  ) &
  sync_pid=$!
}

start_periodic_sync_loop() {
  if [ "${CAPTURE_PERIODIC_SYNC_ENABLE}" = "0" ]; then
    return 0
  fi
  if [ -n "${periodic_sync_pid}" ] && kill -0 "${periodic_sync_pid}" 2>/dev/null; then
    return 0
  fi

  (
    while kill -0 "${child_pid}" 2>/dev/null; do
      sync >/dev/null 2>&1 || true
      sleep "${CAPTURE_PERIODIC_SYNC_SECONDS}"
    done
    sync >/dev/null 2>&1 || true
  ) &
  periodic_sync_pid=$!
}

stop_periodic_sync_loop() {
  if [ -n "${periodic_sync_pid}" ]; then
    kill "${periodic_sync_pid}" 2>/dev/null || true
    wait "${periodic_sync_pid}" 2>/dev/null || true
    periodic_sync_pid=""
  fi
}

start_progress_ping_loop() {
  if [ "${CAPTURE_PROGRESS_PING_ENABLE}" = "0" ]; then
    return 0
  fi
  if [ "${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}" != "0" ]; then
    return 0
  fi
  if [ -n "${progress_ping_pid}" ] && kill -0 "${progress_ping_pid}" 2>/dev/null; then
    return 0
  fi

  : > "${DEEP_LOG_PATH}"
  (
    ping_idx=0
    while kill -0 "${child_pid}" 2>/dev/null; do
      ping_idx=$((ping_idx + 1))
      printf '[firemarshal] progress-ping idx=%s ts=%s\n' \
        "${ping_idx}" \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "${DEEP_LOG_PATH}"
      sync >/dev/null 2>&1 || true
      sleep "${CAPTURE_PROGRESS_PING_SECONDS}"
    done
    sync >/dev/null 2>&1 || true
  ) &
  progress_ping_pid=$!
}

stop_progress_ping_loop() {
  if [ -n "${progress_ping_pid}" ]; then
    kill "${progress_ping_pid}" 2>/dev/null || true
    wait "${progress_ping_pid}" 2>/dev/null || true
    progress_ping_pid=""
  fi
}

write_wrapper_stage() {
  printf '[firemarshal-stage] %s ts=%s\n' \
    "$*" \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" >> "${WRAPPER_STAGE_PATH}"
}

append_child_proc_diag() {
  proc_pid="$1"
  label="$2"
  {
    printf 'label=%s ts=%s pid=%s\n' \
      "${label}" \
      "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
      "${proc_pid}"
    if kill -0 "${proc_pid}" 2>/dev/null; then
      printf 'alive=1\n'
      printf 'cmdline='
      tr '\0' ' ' < "/proc/${proc_pid}/cmdline" 2>/dev/null || true
      printf '\n'
      printf 'wchan='
      cat "/proc/${proc_pid}/wchan" 2>/dev/null || true
      printf '\n'
      printf 'status:\n'
      cat "/proc/${proc_pid}/status" 2>/dev/null || true
    else
      printf 'alive=0\n'
    fi
    printf -- '---\n'
  } >> "${WRAPPER_PROC_STAGE_PATH}"
}

start_child_proc_diag_loop() {
  if [ -n "${child_proc_diag_pid}" ] && kill -0 "${child_proc_diag_pid}" 2>/dev/null; then
    return 0
  fi

  (
    poll_idx=0
    while kill -0 "${child_pid}" 2>/dev/null; do
      poll_idx=$((poll_idx + 1))
      append_child_proc_diag "${child_pid}" "poll-${poll_idx}"
      sleep "${CHILD_PROC_POLL_SECONDS}"
    done
    append_child_proc_diag "${child_pid}" "after-exit"
  ) &
  child_proc_diag_pid=$!
}

stop_child_proc_diag_loop() {
  if [ -n "${child_proc_diag_pid}" ]; then
    kill "${child_proc_diag_pid}" 2>/dev/null || true
    wait "${child_proc_diag_pid}" 2>/dev/null || true
    child_proc_diag_pid=""
  fi
}

write_status() {
  state="$1"
  exit_code="$2"

  {
    echo "state=${state}"
    echo "exit_code=${exit_code}"
    echo "log_path=${LOG_PATH}"
    echo "deep_log_path=${DEEP_LOG_PATH}"
    echo "audit_log_path=${AUDIT_LOG_PATH}"
    echo "checkpoint_log_path=${CHECKPOINT_LOG_PATH}"
    echo "breadcrumb_path=${BREADCRUMB_PATH}"
    echo "status_path=${STATUS_PATH}"
    echo "runner_early_stage_path=${RUNNER_EARLY_STAGE_PATH}"
    echo "runner_stage_path=${RUNNER_STAGE_PATH}"
    echo "runner_proc_stage_path=${RUNNER_PROC_STAGE_PATH}"
    echo "wrapper_stage_path=${WRAPPER_STAGE_PATH}"
    echo "wrapper_proc_stage_path=${WRAPPER_PROC_STAGE_PATH}"
    echo "trace_dir=${TRACE_DIR}"
    echo "profile_id=${PIPELINE_RUNTIME_PROFILE_ID}"
    echo "periodic_sync_enable=${CAPTURE_PERIODIC_SYNC_ENABLE}"
    echo "periodic_sync_seconds=${CAPTURE_PERIODIC_SYNC_SECONDS}"
    echo "progress_ping_enable=${CAPTURE_PROGRESS_PING_ENABLE}"
    echo "progress_ping_seconds=${CAPTURE_PROGRESS_PING_SECONDS}"
    echo "child_proc_poll_seconds=${CHILD_PROC_POLL_SECONDS}"
    echo "uart_log_enable=${PIPELINE_RUNTIME_UART_LOG_ENABLE}"
    echo "guest_log_enable=${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}"
    echo "guest_deep_log_enable=${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"
    echo "audit_log_enable=${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE}"
    echo "checkpoint_log_enable=${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE}"
    echo "breadcrumb_enable=${PIPELINE_RUNTIME_BREADCRUMB_ENABLE:-0}"
    echo "log_profile=${PIPELINE_RUNTIME_LOG_PROFILE}"
    echo "stdio_capture_mode=${PIPELINE_RUNTIME_STDIO_CAPTURE_MODE}"
    echo "dummy_gemmini_mode=${DUMMY_GEMMINI_MODE}"
    echo "mlockall_mode=${PIPELINE_RUNTIME_MLOCKALL_MODE:-0}"
    echo "skip_model_bin_load=${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}"
    echo "skip_input_load=${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}"
    echo "skip_golden_check=${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}"
    echo "trace_enable=${TRACE_ENABLE}"
    echo "dma_force_direct_enable=${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}"
    echo "gdbserver_enable=${PIPELINE_RUNTIME_GDBSERVER_ENABLE}"
    echo "gdbserver_bind_addr=${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR}"
    echo "gdbserver_port=${PIPELINE_RUNTIME_GDBSERVER_PORT}"
    echo "gdbserver_info_path=${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH}"
    echo "gdbserver_log_path=${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH}"
    echo "auto_poweroff=${CAPTURE_AUTO_POWEROFF}"
    echo "shutdown_hint=sync; poweroff"
  } > "${STATUS_PATH}"
}

mkdir -p \
  "${LOG_DIR}" \
  "${TRACE_DIR}" \
  "$(dirname "${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH}")" \
  "$(dirname "${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH}")"
rm -f "${LOG_PATH}" "${DEEP_LOG_PATH}" "${AUDIT_LOG_PATH}" "${STATUS_PATH}"
rm -f "${CHECKPOINT_LOG_PATH}"
rm -f "${BREADCRUMB_PATH}"
rm -f "${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH}" "${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH}"
rm -f "${RUNNER_EARLY_STAGE_PATH}" "${RUNNER_STAGE_PATH}" "${RUNNER_PROC_STAGE_PATH}"
rm -f "${WRAPPER_STAGE_PATH}" "${WRAPPER_PROC_STAGE_PATH}"
rm -f "${TRACE_DIR}"/*.trace
: > "${LOG_PATH}"
: > "${DEEP_LOG_PATH}"
: > "${AUDIT_LOG_PATH}"
: > "${CHECKPOINT_LOG_PATH}"
: > "${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH}"
: > "${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH}"
: > "${RUNNER_EARLY_STAGE_PATH}"
: > "${RUNNER_STAGE_PATH}"
: > "${RUNNER_PROC_STAGE_PATH}"
: > "${WRAPPER_STAGE_PATH}"
: > "${WRAPPER_PROC_STAGE_PATH}"
write_wrapper_stage "wrapper-enter log_dir=${LOG_DIR}"

case "${PIPELINE_RUNTIME_STDIO_CAPTURE_MODE}" in
  log)
    exec >> "${LOG_PATH}" 2>&1
    ;;
  uart)
    ;;
  *)
    PIPELINE_RUNTIME_STDIO_CAPTURE_MODE="log"
    exec >> "${LOG_PATH}" 2>&1
    ;;
esac

echo "[firemarshal] pipeline-runtime sparse log path: ${LOG_PATH}"
echo "[firemarshal] pipeline-runtime deep log path: ${DEEP_LOG_PATH}"
echo "[firemarshal] pipeline-runtime audit log path: ${AUDIT_LOG_PATH}"
echo "[firemarshal] pipeline-runtime trace dir: ${TRACE_DIR}"
echo "[firemarshal] pipeline-runtime profile id: ${PIPELINE_RUNTIME_PROFILE_ID}"
echo "[firemarshal] auto poweroff on completion: ${CAPTURE_AUTO_POWEROFF}"
echo "[firemarshal] log profile: ${PIPELINE_RUNTIME_LOG_PROFILE}"
echo "[firemarshal] periodic sync enable: ${CAPTURE_PERIODIC_SYNC_ENABLE}"
echo "[firemarshal] periodic sync seconds: ${CAPTURE_PERIODIC_SYNC_SECONDS}"
echo "[firemarshal] progress ping enable: ${CAPTURE_PROGRESS_PING_ENABLE}"
echo "[firemarshal] progress ping seconds: ${CAPTURE_PROGRESS_PING_SECONDS}"
echo "[firemarshal] uart log enable: ${PIPELINE_RUNTIME_UART_LOG_ENABLE}"
echo "[firemarshal] guest sparse log enable: ${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}"
echo "[firemarshal] guest deep log enable: ${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"
echo "[firemarshal] audit log enable: ${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE}"
echo "[firemarshal] checkpoint log enable: ${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE}"
echo "[firemarshal] stdio capture mode: ${PIPELINE_RUNTIME_STDIO_CAPTURE_MODE}"
echo "[firemarshal] dummy mode: ${DUMMY_GEMMINI_MODE}"
echo "[firemarshal] skip model/input/golden: ${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}/${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}/${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}"
echo "[firemarshal] trace enable: ${TRACE_ENABLE}"
echo "[firemarshal] dma force direct enable: ${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}"
echo "[firemarshal] gdbserver enable: ${PIPELINE_RUNTIME_GDBSERVER_ENABLE}"
echo "[firemarshal] gdbserver endpoint: ${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR}:${PIPELINE_RUNTIME_GDBSERVER_PORT}"
echo "[firemarshal] gdbserver info/log: ${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH} / ${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH}"
write_wrapper_stage "after-log-preamble"

write_status "starting" ""
flush_capture_state
write_wrapper_stage "before-child-spawn runner=${GUEST_RUNNER}"

AUTO_POWEROFF=0 \
  TRACE_DIR="${TRACE_DIR}" \
  TRACE_ENABLE="${TRACE_ENABLE}" \
  DEEP_LOG_ENABLE="${DEEP_LOG_ENABLE}" \
  GOLDEN_CHECK_ENABLE="${GOLDEN_CHECK_ENABLE}" \
  DUMMY_GEMMINI_MODE="${DUMMY_GEMMINI_MODE}" \
  PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD="${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}" \
  PIPELINE_RUNTIME_SKIP_INPUT_LOAD="${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}" \
  PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK="${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}" \
  PIPELINE_RUNTIME_UART_LOG_ENABLE="${PIPELINE_RUNTIME_UART_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_LOG_PATH="${LOG_PATH}" \
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH="${DEEP_LOG_PATH}" \
  PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE}" \
  PIPELINE_RUNTIME_AUDIT_LOG_PATH="${AUDIT_LOG_PATH}" \
  PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE="${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE}" \
  PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH="${CHECKPOINT_LOG_PATH}" \
  PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}" \
  PIPELINE_RUNTIME_GDBSERVER_ENABLE="${PIPELINE_RUNTIME_GDBSERVER_ENABLE}" \
  PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR="${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR}" \
  PIPELINE_RUNTIME_GDBSERVER_PORT="${PIPELINE_RUNTIME_GDBSERVER_PORT}" \
  PIPELINE_RUNTIME_GDBSERVER_INFO_PATH="${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH}" \
  PIPELINE_RUNTIME_GDBSERVER_LOG_PATH="${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH}" \
  PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE="${PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE}" \
  PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE=1 \
  PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE="${PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE:-0}" \
  PIPELINE_RUNTIME_RUNNER_EARLY_STAGE_PATH="${RUNNER_EARLY_STAGE_PATH}" \
  PIPELINE_RUNTIME_RUNNER_STAGE_PATH="${RUNNER_STAGE_PATH}" \
  PIPELINE_RUNTIME_RUNNER_PROC_STAGE_PATH="${RUNNER_PROC_STAGE_PATH}" \
  "${GUEST_RUNNER}" "$@" &
child_pid=$!
write_wrapper_stage "after-child-spawn pid=${child_pid}"
append_child_proc_diag "${child_pid}" "after-spawn"

write_status "running" ""
flush_capture_state
start_periodic_sync_loop
start_progress_ping_loop
start_child_proc_diag_loop
write_wrapper_stage "before-child-wait pid=${child_pid}"

if wait "${child_pid}"; then
  rc=0
else
  rc=$?
fi

write_wrapper_stage "after-child-wait rc=${rc} pid=${child_pid}"
append_child_proc_diag "${child_pid}" "after-wait"
stop_child_proc_diag_loop
stop_progress_ping_loop
stop_periodic_sync_loop
flush_capture_state
sleep 1

write_status "finished" "${rc}"
flush_capture_state
sleep 1

echo "[firemarshal] pipeline-runtime exited rc=${rc}"
echo "[firemarshal] guest files are ready under ${LOG_DIR}"

if [ "${CAPTURE_AUTO_POWEROFF}" = "0" ]; then
  echo "[firemarshal] auto poweroff disabled; run 'sync; poweroff' in the guest to trigger FireSim copy-back"
  exit "${rc}"
fi

echo "[firemarshal] powering off guest to trigger FireSim copy-back"
poweroff -f
exit "${rc}"
