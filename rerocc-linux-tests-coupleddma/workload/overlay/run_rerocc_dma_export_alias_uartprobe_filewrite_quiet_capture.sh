#!/bin/sh
set -eu

GUEST_RUNNER="/root/rerocc-linux-tests-coupleddma/run_rerocc_dma_export_alias_uartprobe.sh"
LOG_DIR="${UARTPROBE_LOG_DIR:-/root/pipeline-runtime-debug}"
SPARSE_LOG_PATH="${UARTPROBE_STDOUT_LOG_PATH:-${LOG_DIR}/uartprobe.sparse.log}"
DEEP_LOG_PATH="${UARTPROBE_DEEP_LOG_PATH:-${LOG_DIR}/uartprobe.deep.log}"
STATUS_PATH="${UARTPROBE_STATUS_PATH:-${LOG_DIR}/uartprobe.status}"
RUNNER_STAGE_PATH="${UARTPROBE_RUNNER_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.stage}"
BINARY_STAGE_PATH="${UARTPROBE_BINARY_STAGE_PATH:-${LOG_DIR}/uartprobe.binary.stage}"
PROC_STAGE_PATH="${UARTPROBE_PROC_STAGE_PATH:-${LOG_DIR}/uartprobe.proc.stage}"
UARTPROBE_OBSERVABILITY="${UARTPROBE_OBSERVABILITY:-full}"
UARTPROBE_SEED_WAIT_MODE="${UARTPROBE_SEED_WAIT_MODE:-fence}"
UARTPROBE_PROC_DIAG_ENABLE="${UARTPROBE_PROC_DIAG_ENABLE:-1}"
UARTPROBE_PROC_DIAG_POLLS="${UARTPROBE_PROC_DIAG_POLLS:-20}"
UARTPROBE_PROC_DIAG_SLEEP_SECONDS="${UARTPROBE_PROC_DIAG_SLEEP_SECONDS:-2}"
UARTPROBE_PROC_DIAG_KILL_ON_STUCK="${UARTPROBE_PROC_DIAG_KILL_ON_STUCK:-1}"
CAPTURE_AUTO_POWEROFF="${CAPTURE_AUTO_POWEROFF:-1}"
CAPTURE_PERIODIC_SYNC_ENABLE="${CAPTURE_PERIODIC_SYNC_ENABLE:-1}"
CAPTURE_PERIODIC_SYNC_SECONDS="${CAPTURE_PERIODIC_SYNC_SECONDS:-1}"
sync_pid=""
periodic_sync_pid=""

file_size_bytes() {
  path="$1"
  if [ -f "${path}" ]; then
    wc -c < "${path}" | tr -d '[:space:]'
  else
    echo 0
  fi
}

tail_file_line() {
  path="$1"
  if [ -f "${path}" ]; then
    tail -n 1 "${path}"
  else
    echo ""
  fi
}

flush_capture_state() {
  if [ -n "${sync_pid}" ] && kill -0 "${sync_pid}" 2>/dev/null; then
    return 0
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

write_status() {
  state="$1"
  exit_code="$2"

  {
    echo "state=${state}"
    echo "exit_code=${exit_code}"
    echo "sparse_log_path=${SPARSE_LOG_PATH}"
    echo "deep_log_path=${DEEP_LOG_PATH}"
    echo "status_path=${STATUS_PATH}"
    echo "uartprobe_observability=${UARTPROBE_OBSERVABILITY}"
    echo "uartprobe_log_mode=${UARTPROBE_LOG_MODE:-file_write}"
    echo "uartprobe_log_phase=${UARTPROBE_LOG_PHASE:-post_submit_pre_wait}"
    echo "uartprobe_wait_mode=${UARTPROBE_WAIT_MODE:-fence}"
    echo "uartprobe_seed_wait_mode=${UARTPROBE_SEED_WAIT_MODE:-fence}"
    echo "uartprobe_proc_diag_enable=${UARTPROBE_PROC_DIAG_ENABLE}"
    echo "uartprobe_proc_diag_polls=${UARTPROBE_PROC_DIAG_POLLS}"
    echo "uartprobe_proc_diag_sleep_seconds=${UARTPROBE_PROC_DIAG_SLEEP_SECONDS}"
    echo "uartprobe_proc_diag_kill_on_stuck=${UARTPROBE_PROC_DIAG_KILL_ON_STUCK}"
    echo "uartprobe_alias_mode=${UARTPROBE_ALIAS_MODE:-abab}"
    echo "uartprobe_bytes=${UARTPROBE_BYTES:-65536}"
    echo "uartprobe_page_bytes=${UARTPROBE_PAGE_BYTES:-1024}"
    echo "uartprobe_dst_offset=${UARTPROBE_DST_OFFSET:-3072}"
    echo "uartprobe_repeat_count=${UARTPROBE_REPEAT_COUNT:-8}"
    echo "sparse_log_size=$(file_size_bytes "${SPARSE_LOG_PATH}")"
    echo "deep_log_size=$(file_size_bytes "${DEEP_LOG_PATH}")"
    echo "runner_stage_path=${RUNNER_STAGE_PATH}"
    echo "runner_stage_size=$(file_size_bytes "${RUNNER_STAGE_PATH}")"
    echo "runner_stage_last=$(tail_file_line "${RUNNER_STAGE_PATH}")"
    echo "binary_stage_path=${BINARY_STAGE_PATH}"
    echo "binary_stage_size=$(file_size_bytes "${BINARY_STAGE_PATH}")"
    echo "binary_stage_last=$(tail_file_line "${BINARY_STAGE_PATH}")"
    echo "proc_stage_path=${PROC_STAGE_PATH}"
    echo "proc_stage_size=$(file_size_bytes "${PROC_STAGE_PATH}")"
    echo "proc_stage_last=$(tail_file_line "${PROC_STAGE_PATH}")"
    echo "stdout_mode=guest_file_only_self_redirect"
    echo "auto_poweroff=${CAPTURE_AUTO_POWEROFF}"
  } > "${STATUS_PATH}"
}

mkdir -p "${LOG_DIR}"
rm -f "${SPARSE_LOG_PATH}" "${DEEP_LOG_PATH}" "${STATUS_PATH}" "${RUNNER_STAGE_PATH}" "${BINARY_STAGE_PATH}" "${PROC_STAGE_PATH}"
: > "${SPARSE_LOG_PATH}"
: > "${DEEP_LOG_PATH}"

echo "[firemarshal-uartprobe] wrapper-enter seed_wait=${UARTPROBE_SEED_WAIT_MODE} proc_diag=${UARTPROBE_PROC_DIAG_ENABLE} polls=${UARTPROBE_PROC_DIAG_POLLS} sleep=${UARTPROBE_PROC_DIAG_SLEEP_SECONDS} kill=${UARTPROBE_PROC_DIAG_KILL_ON_STUCK}"
write_status "starting" ""
flush_capture_state
echo "[firemarshal-uartprobe] status-starting-written"

AUTO_POWEROFF=0 \
  UARTPROBE_OBSERVABILITY="${UARTPROBE_OBSERVABILITY}" \
  UARTPROBE_RUNNER_LOG_ENABLE=0 \
  UARTPROBE_RUNNER_STAGE_PATH="${RUNNER_STAGE_PATH}" \
  UARTPROBE_BINARY_STAGE_PATH="${BINARY_STAGE_PATH}" \
  UARTPROBE_PROC_STAGE_PATH="${PROC_STAGE_PATH}" \
  UARTPROBE_SEED_WAIT_MODE="${UARTPROBE_SEED_WAIT_MODE}" \
  UARTPROBE_PROC_DIAG_ENABLE="${UARTPROBE_PROC_DIAG_ENABLE}" \
  UARTPROBE_PROC_DIAG_POLLS="${UARTPROBE_PROC_DIAG_POLLS}" \
  UARTPROBE_PROC_DIAG_SLEEP_SECONDS="${UARTPROBE_PROC_DIAG_SLEEP_SECONDS}" \
  UARTPROBE_PROC_DIAG_KILL_ON_STUCK="${UARTPROBE_PROC_DIAG_KILL_ON_STUCK}" \
  UARTPROBE_LOG_MODE="${UARTPROBE_LOG_MODE:-file_write}" \
  UARTPROBE_LOG_DIR="${LOG_DIR}" \
  UARTPROBE_LOG_PATH="${DEEP_LOG_PATH}" \
  "${GUEST_RUNNER}" "$@" >/dev/null 2>&1 &
child_pid=$!
echo "[firemarshal-uartprobe] child-start pid=${child_pid}"

start_periodic_sync_loop
write_status "running" ""
flush_capture_state

if wait "${child_pid}"; then
  rc=0
else
  rc=$?
fi

stop_periodic_sync_loop
flush_capture_state
sleep 1

write_status "finished" "${rc}"
flush_capture_state
sleep 1
echo "[firemarshal-uartprobe] child-exit rc=${rc}"

if [ "${CAPTURE_AUTO_POWEROFF}" = "0" ]; then
  exit "${rc}"
fi

poweroff -f
exit "${rc}"
