#!/bin/sh
set -eu

GUEST_RUNNER="/root/rerocc-linux-tests-coupleddma/run_rerocc_dma_export_alias_uartprobe.sh"
LOG_DIR="${UARTPROBE_LOG_DIR:-/root/pipeline-runtime-debug}"
SPARSE_LOG_PATH="${UARTPROBE_STDOUT_LOG_PATH:-${LOG_DIR}/uartprobe.sparse.log}"
DEEP_LOG_PATH="${UARTPROBE_DEEP_LOG_PATH:-${LOG_DIR}/uartprobe.deep.log}"
STATUS_PATH="${UARTPROBE_STATUS_PATH:-${LOG_DIR}/uartprobe.status}"
LOG_PIPE_PATH="${UARTPROBE_LOG_PIPE_PATH:-${LOG_DIR}/uartprobe.stdout.pipe}"
CAPTURE_AUTO_POWEROFF="${CAPTURE_AUTO_POWEROFF:-1}"
CAPTURE_PERIODIC_SYNC_ENABLE="${CAPTURE_PERIODIC_SYNC_ENABLE:-1}"
CAPTURE_PERIODIC_SYNC_SECONDS="${CAPTURE_PERIODIC_SYNC_SECONDS:-1}"
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

stop_log_tee() {
  if [ -n "${tee_pid}" ]; then
    wait "${tee_pid}" 2>/dev/null || true
    tee_pid=""
  fi
  rm -f "${LOG_PIPE_PATH}"
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
    echo "uartprobe_log_mode=${UARTPROBE_LOG_MODE:-file_write}"
    echo "uartprobe_log_phase=${UARTPROBE_LOG_PHASE:-post_submit_pre_wait}"
    echo "uartprobe_wait_mode=${UARTPROBE_WAIT_MODE:-fence}"
    echo "uartprobe_alias_mode=${UARTPROBE_ALIAS_MODE:-abab}"
    echo "uartprobe_bytes=${UARTPROBE_BYTES:-65536}"
    echo "uartprobe_page_bytes=${UARTPROBE_PAGE_BYTES:-1024}"
    echo "uartprobe_dst_offset=${UARTPROBE_DST_OFFSET:-3072}"
    echo "uartprobe_repeat_count=${UARTPROBE_REPEAT_COUNT:-8}"
    echo "sparse_log_size=$(file_size_bytes "${SPARSE_LOG_PATH}")"
    echo "deep_log_size=$(file_size_bytes "${DEEP_LOG_PATH}")"
    echo "auto_poweroff=${CAPTURE_AUTO_POWEROFF}"
  } > "${STATUS_PATH}"
}

mkdir -p "${LOG_DIR}"
rm -f "${SPARSE_LOG_PATH}" "${DEEP_LOG_PATH}" "${STATUS_PATH}" "${LOG_PIPE_PATH}"
mkfifo "${LOG_PIPE_PATH}"

echo "[firemarshal] uartprobe sparse log path: ${SPARSE_LOG_PATH}"
echo "[firemarshal] uartprobe deep log path: ${DEEP_LOG_PATH}"
echo "[firemarshal] uartprobe runner: ${GUEST_RUNNER}"

write_status "starting" ""
flush_capture_state

tee -a "${SPARSE_LOG_PATH}" < "${LOG_PIPE_PATH}" &
tee_pid=$!

AUTO_POWEROFF=0 \
  UARTPROBE_LOG_MODE="${UARTPROBE_LOG_MODE:-file_write}" \
  UARTPROBE_LOG_DIR="${LOG_DIR}" \
  UARTPROBE_LOG_PATH="${DEEP_LOG_PATH}" \
  "${GUEST_RUNNER}" "$@" > "${LOG_PIPE_PATH}" 2>&1 &
child_pid=$!

start_periodic_sync_loop
write_status "running" ""
flush_capture_state

if wait "${child_pid}"; then
  rc=0
else
  rc=$?
fi

stop_log_tee
stop_periodic_sync_loop
flush_capture_state
sleep 1

write_status "finished" "${rc}"
flush_capture_state
sleep 1

echo "[firemarshal] uartprobe exited rc=${rc}"
echo "[firemarshal] guest files are ready under ${LOG_DIR}"

if [ "${CAPTURE_AUTO_POWEROFF}" = "0" ]; then
  echo "[firemarshal] auto poweroff disabled; run 'sync; poweroff' in the guest to trigger FireSim copy-back"
  exit "${rc}"
fi

echo "[firemarshal] powering off guest to trigger FireSim copy-back"
poweroff -f
exit "${rc}"
