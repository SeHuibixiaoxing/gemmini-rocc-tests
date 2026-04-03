#!/bin/sh
set -eu

GUEST_RUNNER="/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh"
LOG_DIR="${PIPELINE_RUNTIME_LOG_DIR:-/root/pipeline-runtime-debug}"
LOG_PATH="${PIPELINE_RUNTIME_LOG_PATH:-${LOG_DIR}/bertmini-batch8.log}"
DEEP_LOG_PATH="${PIPELINE_RUNTIME_DEEP_LOG_PATH:-${LOG_DIR}/bertmini-batch8.deep.log}"
STATUS_PATH="${PIPELINE_RUNTIME_STATUS_PATH:-${LOG_DIR}/bertmini-batch8.status}"
TRACE_DIR="${TRACE_DIR:-${LOG_DIR}/traces}"
WATCHDOG_MS="${WATCHDOG_MS:-600000}"
CAPTURE_WATCHDOG_IDLE_SECONDS="${CAPTURE_WATCHDOG_IDLE_SECONDS:-}"
CAPTURE_WATCHDOG_POLL_SECONDS="${CAPTURE_WATCHDOG_POLL_SECONDS:-5}"
CAPTURE_WATCHDOG_KILL_GRACE_SECONDS="${CAPTURE_WATCHDOG_KILL_GRACE_SECONDS:-10}"
CAPTURE_STATUS_SYNC_SECONDS="${CAPTURE_STATUS_SYNC_SECONDS:-30}"
CAPTURE_AUTO_POWEROFF="${CAPTURE_AUTO_POWEROFF:-1}"
CAPTURE_WATCHDOG_ARM_MARKER="${CAPTURE_WATCHDOG_ARM_MARKER:-[bertmini] method=}"
CAPTURE_PERIODIC_SYNC_ENABLE="${CAPTURE_PERIODIC_SYNC_ENABLE:-1}"
CAPTURE_PERIODIC_SYNC_SECONDS="${CAPTURE_PERIODIC_SYNC_SECONDS:-1}"
PIPELINE_RUNTIME_UART_LOG_ENABLE="${PIPELINE_RUNTIME_UART_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE:-1}"
LOG_PIPE_PATH="${PIPELINE_RUNTIME_LOG_PIPE_PATH:-${LOG_DIR}/bertmini-batch8.stdout.pipe}"
sync_pid=""
periodic_sync_pid=""
tee_pid=""

now_s() {
  if [ -r /proc/uptime ]; then
    awk '{ print int($1) }' /proc/uptime
  else
    date +%s
  fi
}

file_size_bytes() {
  path="$1"
  if [ -f "${path}" ]; then
    wc -c < "${path}" | tr -d '[:space:]'
  else
    echo 0
  fi
}

flush_capture_state() {
  # Never let guest-wide writeback block the watchdog loop.
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

stop_log_tee() {
  if [ -n "${tee_pid}" ]; then
    wait "${tee_pid}" 2>/dev/null || true
    tee_pid=""
  fi
  rm -f "${LOG_PIPE_PATH}"
}

kill_tree() {
  pid="$1"
  sig="$2"
  children_file="/proc/${pid}/task/${pid}/children"

  if [ -r "${children_file}" ]; then
    children="$(cat "${children_file}" 2>/dev/null || true)"
    for child in ${children}; do
      kill_tree "${child}" "${sig}"
    done
  fi

  kill "-${sig}" "${pid}" 2>/dev/null || true
}

write_status() {
  state="$1"
  exit_code="$2"
  watchdog_reason="$3"
  child_alive="$4"
  last_sparse_log_size="$5"
  last_deep_log_size="$6"
  idle_seconds="$7"
  timed_out="$8"
  watchdog_armed="$9"

  {
    echo "state=${state}"
    echo "exit_code=${exit_code}"
    echo "log_path=${LOG_PATH}"
    echo "deep_log_path=${DEEP_LOG_PATH}"
    echo "status_path=${STATUS_PATH}"
    echo "trace_dir=${TRACE_DIR}"
    echo "watchdog_ms=${WATCHDOG_MS}"
    echo "watchdog_idle_seconds=${CAPTURE_WATCHDOG_IDLE_SECONDS}"
    echo "watchdog_poll_seconds=${CAPTURE_WATCHDOG_POLL_SECONDS}"
    echo "watchdog_kill_grace_seconds=${CAPTURE_WATCHDOG_KILL_GRACE_SECONDS}"
    echo "status_sync_seconds=${CAPTURE_STATUS_SYNC_SECONDS}"
    echo "watchdog_arm_marker=${CAPTURE_WATCHDOG_ARM_MARKER}"
    echo "periodic_sync_enable=${CAPTURE_PERIODIC_SYNC_ENABLE}"
    echo "periodic_sync_seconds=${CAPTURE_PERIODIC_SYNC_SECONDS}"
    echo "uart_log_enable=${PIPELINE_RUNTIME_UART_LOG_ENABLE}"
    echo "guest_deep_log_enable=${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"
    echo "watchdog_armed=${watchdog_armed}"
    echo "watchdog_reason=${watchdog_reason}"
    echo "watchdog_timed_out=${timed_out}"
    echo "child_alive_after_watchdog=${child_alive}"
    echo "last_sparse_log_size=${last_sparse_log_size}"
    echo "last_deep_log_size=${last_deep_log_size}"
    echo "idle_seconds=${idle_seconds}"
    echo "auto_poweroff=${CAPTURE_AUTO_POWEROFF}"
    echo "shutdown_hint=sync; poweroff"
  } > "${STATUS_PATH}"
}

if [ -z "${CAPTURE_WATCHDOG_IDLE_SECONDS}" ]; then
  CAPTURE_WATCHDOG_IDLE_SECONDS=$((WATCHDOG_MS / 1000))
  if [ "${CAPTURE_WATCHDOG_IDLE_SECONDS}" -lt 600 ]; then
    CAPTURE_WATCHDOG_IDLE_SECONDS=600
  fi
fi

mkdir -p "${LOG_DIR}"
mkdir -p "${TRACE_DIR}"
rm -f "${LOG_PATH}" "${DEEP_LOG_PATH}" "${STATUS_PATH}" "${LOG_PIPE_PATH}"
rm -f "${TRACE_DIR}"/*.trace
mkfifo "${LOG_PIPE_PATH}"

echo "[firemarshal] pipeline-runtime sparse log path: ${LOG_PATH}"
echo "[firemarshal] pipeline-runtime deep log path: ${DEEP_LOG_PATH}"
echo "[firemarshal] pipeline-runtime trace dir: ${TRACE_DIR}"
echo "[firemarshal] watchdog idle timeout: ${CAPTURE_WATCHDOG_IDLE_SECONDS}s"
echo "[firemarshal] watchdog arm marker: ${CAPTURE_WATCHDOG_ARM_MARKER}"
echo "[firemarshal] auto poweroff on completion: ${CAPTURE_AUTO_POWEROFF}"
echo "[firemarshal] periodic sync enable: ${CAPTURE_PERIODIC_SYNC_ENABLE}"
echo "[firemarshal] periodic sync seconds: ${CAPTURE_PERIODIC_SYNC_SECONDS}"
echo "[firemarshal] uart sparse log enable: ${PIPELINE_RUNTIME_UART_LOG_ENABLE}"
echo "[firemarshal] guest deep log enable: ${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"

start_now="$(now_s)"
start_sparse_log_size="$(file_size_bytes "${LOG_PATH}")"
start_deep_log_size="$(file_size_bytes "${DEEP_LOG_PATH}")"
write_status "starting" "" "none" 0 "${start_sparse_log_size}" "${start_deep_log_size}" 0 0 0
flush_capture_state

tee -a "${LOG_PATH}" < "${LOG_PIPE_PATH}" &
tee_pid=$!
AUTO_POWEROFF=0 WATCHDOG_MS="${WATCHDOG_MS}" TRACE_DIR="${TRACE_DIR}" \
  PIPELINE_RUNTIME_UART_LOG_ENABLE="${PIPELINE_RUNTIME_UART_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}" \
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH="${DEEP_LOG_PATH}" \
  "${GUEST_RUNNER}" "$@" > "${LOG_PIPE_PATH}" 2>&1 &
child_pid=$!
start_periodic_sync_loop
watchdog_reason="none"
timed_out=0
child_alive_after_watchdog=0
watchdog_armed=1
watchdog_marker_seen=0
current_s="$(now_s)"
arm_msg="[firemarshal] watchdog armed at wrapper launch t=${current_s}"
echo "${arm_msg}" >> "${LOG_PATH}"
echo "${arm_msg}"
last_sparse_log_size="$(file_size_bytes "${LOG_PATH}")"
last_deep_log_size="$(file_size_bytes "${DEEP_LOG_PATH}")"
last_progress_s="${current_s}"
last_status_sync_s="${current_s}"
write_status "armed" "" "${watchdog_reason}" 1 "${last_sparse_log_size}" "${last_deep_log_size}" 0 0 1
flush_capture_state

while kill -0 "${child_pid}" 2>/dev/null; do
  sleep "${CAPTURE_WATCHDOG_POLL_SECONDS}"
  current_s="$(now_s)"
  current_sparse_log_size="$(file_size_bytes "${LOG_PATH}")"
  current_deep_log_size="$(file_size_bytes "${DEEP_LOG_PATH}")"

  if [ "${watchdog_marker_seen}" -eq 0 ] && [ -n "${CAPTURE_WATCHDOG_ARM_MARKER}" ]; then
    if grep -Fq "${CAPTURE_WATCHDOG_ARM_MARKER}" "${LOG_PATH}" 2>/dev/null; then
      watchdog_marker_seen=1
      last_progress_s="${current_s}"
      marker_msg="[firemarshal] watchdog marker observed '${CAPTURE_WATCHDOG_ARM_MARKER}' at t=${current_s}"
      echo "${marker_msg}" >> "${LOG_PATH}"
      echo "${marker_msg}"
      current_sparse_log_size="$(file_size_bytes "${LOG_PATH}")"
      current_deep_log_size="$(file_size_bytes "${DEEP_LOG_PATH}")"
      last_sparse_log_size="${current_sparse_log_size}"
      last_deep_log_size="${current_deep_log_size}"
      write_status "running" "" "${watchdog_reason}" 1 "${current_sparse_log_size}" "${current_deep_log_size}" 0 0 1
      flush_capture_state
      last_status_sync_s="${current_s}"
      continue
    fi
  fi

  if [ "${current_sparse_log_size}" != "${last_sparse_log_size}" ] || \
     [ "${current_deep_log_size}" != "${last_deep_log_size}" ]; then
    last_sparse_log_size="${current_sparse_log_size}"
    last_deep_log_size="${current_deep_log_size}"
    last_progress_s="${current_s}"
  fi

  idle_seconds=$((current_s - last_progress_s))
  if [ "${CAPTURE_STATUS_SYNC_SECONDS}" -gt 0 ] && [ $((current_s - last_status_sync_s)) -ge "${CAPTURE_STATUS_SYNC_SECONDS}" ]; then
    runtime_state="armed"
    if [ "${watchdog_marker_seen}" -eq 1 ]; then
      runtime_state="running"
    fi
    write_status "${runtime_state}" "" "${watchdog_reason}" 1 "${last_sparse_log_size}" "${last_deep_log_size}" "${idle_seconds}" 0 1
    flush_capture_state
    last_status_sync_s="${current_s}"
  fi
  if [ "${idle_seconds}" -ge "${CAPTURE_WATCHDOG_IDLE_SECONDS}" ]; then
    timed_out=1
    watchdog_reason="idle_log_timeout"
    timeout_msg="[firemarshal] watchdog timeout idle=${idle_seconds}s sparse_log_size=${current_sparse_log_size} deep_log_size=${current_deep_log_size}; sending TERM to pid=${child_pid}"
    echo "${timeout_msg}" >> "${LOG_PATH}"
    echo "${timeout_msg}"
    write_status "timing_out" "" "${watchdog_reason}" 1 "${current_sparse_log_size}" "${current_deep_log_size}" "${idle_seconds}" 1 1
    flush_capture_state
    kill_tree "${child_pid}" TERM
    sleep "${CAPTURE_WATCHDOG_KILL_GRACE_SECONDS}"
    if kill -0 "${child_pid}" 2>/dev/null; then
      escalation_msg="[firemarshal] watchdog escalation pid=${child_pid}; sending KILL"
      echo "${escalation_msg}" >> "${LOG_PATH}"
      echo "${escalation_msg}"
      kill_tree "${child_pid}" KILL
      sleep 2
    fi
    if kill -0 "${child_pid}" 2>/dev/null; then
      child_alive_after_watchdog=1
    fi
    break
  fi
done

if [ "${child_alive_after_watchdog}" -eq 0 ]; then
  if wait "${child_pid}"; then
    rc=0
  else
    rc=$?
  fi
else
  rc=124
fi

stop_log_tee
stop_periodic_sync_loop
flush_capture_state
sleep 2

end_sparse_log_size="$(file_size_bytes "${LOG_PATH}")"
end_deep_log_size="$(file_size_bytes "${DEEP_LOG_PATH}")"
end_idle_seconds=$(( $(now_s) - last_progress_s ))
if [ "${timed_out}" -eq 1 ]; then
  final_state="timeout"
else
  final_state="finished"
fi
write_status "${final_state}" "${rc}" "${watchdog_reason}" "${child_alive_after_watchdog}" "${end_sparse_log_size}" "${end_deep_log_size}" "${end_idle_seconds}" "${timed_out}" "${watchdog_armed}"

flush_capture_state
sleep 2

echo "[firemarshal] pipeline-runtime exited rc=${rc}"
echo "[firemarshal] guest files are ready under ${LOG_DIR}"

if [ "${CAPTURE_AUTO_POWEROFF}" = "0" ]; then
  echo "[firemarshal] auto poweroff disabled; run 'sync; poweroff' in the guest to trigger FireSim copy-back"
  exit "${rc}"
fi

echo "[firemarshal] powering off guest to trigger FireSim copy-back"
poweroff -f

exit "${rc}"
