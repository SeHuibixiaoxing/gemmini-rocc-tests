#!/bin/sh
set -eu

GUEST_RUNNER="/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh"
LOG_DIR="${PIPELINE_RUNTIME_LOG_DIR:-/root/pipeline-runtime-debug}"
LOG_PATH="${PIPELINE_RUNTIME_LOG_PATH:-${LOG_DIR}/bertmini-batch8.log}"
DEEP_LOG_PATH="${PIPELINE_RUNTIME_DEEP_LOG_PATH:-${LOG_DIR}/bertmini-batch8.deep.log}"
AUDIT_LOG_PATH="${PIPELINE_RUNTIME_AUDIT_LOG_PATH:-${LOG_DIR}/bertmini-batch8.audit.log}"
STATUS_PATH="${PIPELINE_RUNTIME_STATUS_PATH:-${LOG_DIR}/bertmini-batch8.status}"
TRACE_DIR="${TRACE_DIR:-${LOG_DIR}/traces}"
CAPTURE_AUTO_POWEROFF="${CAPTURE_AUTO_POWEROFF:-1}"
CAPTURE_PERIODIC_SYNC_ENABLE="${CAPTURE_PERIODIC_SYNC_ENABLE:-1}"
CAPTURE_PERIODIC_SYNC_SECONDS="${CAPTURE_PERIODIC_SYNC_SECONDS:-1}"
CAPTURE_PROGRESS_PING_ENABLE="${CAPTURE_PROGRESS_PING_ENABLE:-0}"
CAPTURE_PROGRESS_PING_SECONDS="${CAPTURE_PROGRESS_PING_SECONDS:-30}"
PIPELINE_RUNTIME_UART_LOG_ENABLE="${PIPELINE_RUNTIME_UART_LOG_ENABLE:-0}"
PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE:-1}"
DEEP_LOG_ENABLE="${DEEP_LOG_ENABLE:-1}"
TRACE_ENABLE="${TRACE_ENABLE:-0}"
sync_pid=""
periodic_sync_pid=""
progress_ping_pid=""

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

write_status() {
  state="$1"
  exit_code="$2"

  {
    echo "state=${state}"
    echo "exit_code=${exit_code}"
    echo "log_path=${LOG_PATH}"
    echo "deep_log_path=${DEEP_LOG_PATH}"
    echo "audit_log_path=${AUDIT_LOG_PATH}"
    echo "status_path=${STATUS_PATH}"
    echo "trace_dir=${TRACE_DIR}"
    echo "periodic_sync_enable=${CAPTURE_PERIODIC_SYNC_ENABLE}"
    echo "periodic_sync_seconds=${CAPTURE_PERIODIC_SYNC_SECONDS}"
    echo "progress_ping_enable=${CAPTURE_PROGRESS_PING_ENABLE}"
    echo "progress_ping_seconds=${CAPTURE_PROGRESS_PING_SECONDS}"
    echo "uart_log_enable=${PIPELINE_RUNTIME_UART_LOG_ENABLE}"
    echo "guest_log_enable=${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}"
    echo "guest_deep_log_enable=${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"
    echo "audit_log_enable=${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE}"
    echo "trace_enable=${TRACE_ENABLE}"
    echo "auto_poweroff=${CAPTURE_AUTO_POWEROFF}"
    echo "shutdown_hint=sync; poweroff"
  } > "${STATUS_PATH}"
}

mkdir -p "${LOG_DIR}" "${TRACE_DIR}"
rm -f "${LOG_PATH}" "${DEEP_LOG_PATH}" "${AUDIT_LOG_PATH}" "${STATUS_PATH}"
rm -f "${TRACE_DIR}"/*.trace
: > "${LOG_PATH}"
: > "${DEEP_LOG_PATH}"
: > "${AUDIT_LOG_PATH}"
exec >> "${LOG_PATH}" 2>&1

echo "[firemarshal] pipeline-runtime sparse log path: ${LOG_PATH}"
echo "[firemarshal] pipeline-runtime deep log path: ${DEEP_LOG_PATH}"
echo "[firemarshal] pipeline-runtime audit log path: ${AUDIT_LOG_PATH}"
echo "[firemarshal] pipeline-runtime trace dir: ${TRACE_DIR}"
echo "[firemarshal] auto poweroff on completion: ${CAPTURE_AUTO_POWEROFF}"
echo "[firemarshal] periodic sync enable: ${CAPTURE_PERIODIC_SYNC_ENABLE}"
echo "[firemarshal] periodic sync seconds: ${CAPTURE_PERIODIC_SYNC_SECONDS}"
echo "[firemarshal] progress ping enable: ${CAPTURE_PROGRESS_PING_ENABLE}"
echo "[firemarshal] progress ping seconds: ${CAPTURE_PROGRESS_PING_SECONDS}"
echo "[firemarshal] uart log enable: ${PIPELINE_RUNTIME_UART_LOG_ENABLE}"
echo "[firemarshal] guest sparse log enable: ${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}"
echo "[firemarshal] guest deep log enable: ${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"
echo "[firemarshal] audit log enable: ${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE}"
echo "[firemarshal] trace enable: ${TRACE_ENABLE}"

write_status "starting" ""
flush_capture_state

AUTO_POWEROFF=0 \
  TRACE_DIR="${TRACE_DIR}" \
  TRACE_ENABLE="${TRACE_ENABLE}" \
  DEEP_LOG_ENABLE="${DEEP_LOG_ENABLE}" \
  PIPELINE_RUNTIME_UART_LOG_ENABLE="${PIPELINE_RUNTIME_UART_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_LOG_PATH="${LOG_PATH}" \
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH="${DEEP_LOG_PATH}" \
  PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE}" \
  PIPELINE_RUNTIME_AUDIT_LOG_PATH="${AUDIT_LOG_PATH}" \
  "${GUEST_RUNNER}" "$@" &
child_pid=$!

write_status "running" ""
flush_capture_state
start_periodic_sync_loop
start_progress_ping_loop

if wait "${child_pid}"; then
  rc=0
else
  rc=$?
fi

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
