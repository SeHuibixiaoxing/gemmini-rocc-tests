#!/bin/sh
set -eu

GUEST_RUNNER="/root/rerocc-linux-tests-coupleddma/run_rerocc_dma_export_alias_uartprobe.sh"
LOG_DIR="${UARTPROBE_LOG_DIR:-/root/pipeline-runtime-debug}"
SPARSE_LOG_PATH="${UARTPROBE_STDOUT_LOG_PATH:-${LOG_DIR}/uartprobe.sparse.log}"
DEEP_LOG_PATH="${UARTPROBE_DEEP_LOG_PATH:-${LOG_DIR}/uartprobe.deep.log}"
STATUS_PATH="${UARTPROBE_STATUS_PATH:-${LOG_DIR}/uartprobe.status}"
RUNNER_STAGE_PATH="${UARTPROBE_RUNNER_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.stage}"
BINARY_STAGE_PATH="${UARTPROBE_BINARY_STAGE_PATH:-${LOG_DIR}/uartprobe.binary.stage}"
PROC_STAGE_PATH="${UARTPROBE_WRAPPER_PROC_STAGE_PATH:-${UARTPROBE_PROC_STAGE_PATH:-${LOG_DIR}/uartprobe.proc.stage}}"
RUNNER_PROC_STAGE_PATH="${UARTPROBE_RUNNER_PROC_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.proc.stage}"
RUNNER_EARLY_STAGE_PATH="${UARTPROBE_RUNNER_EARLY_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.early.stage}"
LOG_PIPE_PATH="${UARTPROBE_LOG_PIPE_PATH:-${LOG_DIR}/uartprobe.stdout.pipe}"
UARTPROBE_RUNNER_LOG_ENABLE="${UARTPROBE_RUNNER_LOG_ENABLE:-0}"
UARTPROBE_CAPTURE_ECHO_STDOUT="${UARTPROBE_CAPTURE_ECHO_STDOUT:-tee_devnull}"
UARTPROBE_LOG_MODE="${UARTPROBE_LOG_MODE:-stdout_write}"
UARTPROBE_LOG_PHASE="${UARTPROBE_LOG_PHASE:-post_submit_pre_wait}"
UARTPROBE_WAIT_MODE="${UARTPROBE_WAIT_MODE:-fence}"
UARTPROBE_SEED_WAIT_MODE="${UARTPROBE_SEED_WAIT_MODE:-fence}"
UARTPROBE_ALIAS_MODE="${UARTPROBE_ALIAS_MODE:-abab}"
UARTPROBE_RUNNER_PROC_DIAG_ENABLE="${UARTPROBE_RUNNER_PROC_DIAG_ENABLE:-0}"
UARTPROBE_PROC_DIAG_POLLS="${UARTPROBE_PROC_DIAG_POLLS:-20}"
UARTPROBE_PROC_DIAG_SLEEP_SECONDS="${UARTPROBE_PROC_DIAG_SLEEP_SECONDS:-2}"
UARTPROBE_PROC_DIAG_KILL_ON_STUCK="${UARTPROBE_PROC_DIAG_KILL_ON_STUCK:-1}"
UARTPROBE_WRAPPER_PROC_DIAG_ENABLE="${UARTPROBE_WRAPPER_PROC_DIAG_ENABLE:-1}"
UARTPROBE_WRAPPER_SYNC_CHILD_SNAPSHOT_ENABLE="${UARTPROBE_WRAPPER_SYNC_CHILD_SNAPSHOT_ENABLE:-1}"
UARTPROBE_WRAPPER_PROC_DIAG_SLEEP_SECONDS="${UARTPROBE_WRAPPER_PROC_DIAG_SLEEP_SECONDS:-2}"
UARTPROBE_WRAPPER_PROC_DIAG_MAX_CHILDREN="${UARTPROBE_WRAPPER_PROC_DIAG_MAX_CHILDREN:-2}"
CAPTURE_AUTO_POWEROFF="${CAPTURE_AUTO_POWEROFF:-1}"
CAPTURE_PERIODIC_SYNC_ENABLE="${CAPTURE_PERIODIC_SYNC_ENABLE:-1}"
CAPTURE_PERIODIC_SYNC_SECONDS="${CAPTURE_PERIODIC_SYNC_SECONDS:-1}"
sync_pid=""
periodic_sync_pid=""
tee_pid=""
wrapper_proc_diag_pid=""

compact_proc_file() {
  path="$1"
  if [ -r "${path}" ]; then
    tr '\n' ' ' < "${path}" 2>/dev/null | tr '\t' ' ' | sed 's/[[:space:]]\+/ /g; s/[[:space:]]$//'
  else
    echo ""
  fi
}

proc_status_field() {
  pid="$1"
  field="$2"
  if [ -r "/proc/${pid}/status" ]; then
    awk -F ':' -v key="${field}" '$1 == key {sub(/^[ \t]+/, "", $2); print $2; exit}' "/proc/${pid}/status" 2>/dev/null || true
  else
    echo ""
  fi
}

append_proc_snapshot() {
  sample="$1"
  role="$2"
  pid="$3"

  if [ -z "${pid}" ]; then
    echo "sample=${sample} role=${role} pid=" >> "${PROC_STAGE_PATH}"
    return 0
  fi

  {
    echo "sample=${sample} role=${role} pid=${pid}"
    if [ ! -d "/proc/${pid}" ]; then
      echo "alive=0"
      echo "===="
    else
      echo "alive=1"
      echo "ppid=$(proc_status_field "${pid}" "PPid")"
      echo "state=$(proc_status_field "${pid}" "State")"
      echo "comm=$(compact_proc_file "/proc/${pid}/comm")"
      echo "cmdline=$(tr '\000' ' ' < "/proc/${pid}/cmdline" 2>/dev/null | sed 's/[[:space:]]\+$//')"
      echo "exe=$(readlink "/proc/${pid}/exe" 2>/dev/null || true)"
      echo "fd1=$(readlink "/proc/${pid}/fd/1" 2>/dev/null || true)"
      echo "fdinfo1=$(compact_proc_file "/proc/${pid}/fdinfo/1")"
      echo "fd2=$(readlink "/proc/${pid}/fd/2" 2>/dev/null || true)"
      echo "fdinfo2=$(compact_proc_file "/proc/${pid}/fdinfo/2")"
      echo "wchan=$(compact_proc_file "/proc/${pid}/wchan")"
      echo "syscall=$(compact_proc_file "/proc/${pid}/syscall")"
      echo "children=$(compact_proc_file "/proc/${pid}/task/${pid}/children")"
    fi
    echo "===="
  } >> "${PROC_STAGE_PATH}"
}

append_wrapper_proc_stage() {
  sample="$1"
  child_children=""
  child_idx=1

  if [ -n "${child_pid:-}" ] && [ -r "/proc/${child_pid}/task/${child_pid}/children" ]; then
    child_children="$(cat "/proc/${child_pid}/task/${child_pid}/children" 2>/dev/null || true)"
  fi

  append_proc_snapshot "${sample}" "runner-shell" "${child_pid:-}"
  for proc_child_pid in ${child_children}; do
    append_proc_snapshot "${sample}" "runner-child-${child_idx}" "${proc_child_pid}"
    child_idx=$((child_idx + 1))
    if [ "${child_idx}" -gt "${UARTPROBE_WRAPPER_PROC_DIAG_MAX_CHILDREN}" ]; then
      break
    fi
  done
}

append_wrapper_stage_line() {
  stage="$1"
  {
    echo "stage=${stage} wrapper_pid=$$ child_pid=${child_pid:-}"
    echo "===="
  } >> "${PROC_STAGE_PATH}"
}

append_wrapper_sync_child_snapshot() {
  sample="$1"
  pid="${child_pid:-}"

  if [ "${UARTPROBE_WRAPPER_SYNC_CHILD_SNAPSHOT_ENABLE}" = "0" ]; then
    return 0
  fi

  {
    echo "sample=${sample} role=runner-shell-sync pid=${pid}"
    if [ -z "${pid}" ] || [ ! -d "/proc/${pid}" ]; then
      echo "alive=0"
      echo "===="
      return 0
    fi

    echo "alive=1"
    if [ -r "/proc/${pid}/status" ]; then
      while IFS=':' read -r key value; do
        case "${key}" in
          Name|State|PPid|Threads)
            echo "${key}=${value}"
            ;;
        esac
      done < "/proc/${pid}/status"
    fi

    if [ -r "/proc/${pid}/wchan" ]; then
      wchan_line=""
      IFS= read -r wchan_line < "/proc/${pid}/wchan" || true
      echo "wchan=${wchan_line}"
    fi

    if [ -r "/proc/${pid}/syscall" ]; then
      syscall_line=""
      IFS= read -r syscall_line < "/proc/${pid}/syscall" || true
      echo "syscall=${syscall_line}"
    fi

    if [ -r "/proc/${pid}/fdinfo/1" ]; then
      while IFS= read -r fdinfo_line; do
        echo "fdinfo1=${fdinfo_line}"
      done < "/proc/${pid}/fdinfo/1"
    fi

    if [ -r "/proc/${pid}/task/${pid}/children" ]; then
      children_line=""
      IFS= read -r children_line < "/proc/${pid}/task/${pid}/children" || true
      echo "children=${children_line}"
    fi
    echo "===="
  } >> "${PROC_STAGE_PATH}"
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

start_wrapper_proc_diag_loop() {
  if [ "${UARTPROBE_WRAPPER_PROC_DIAG_ENABLE}" = "0" ]; then
    return 0
  fi

  (
    proc_poll_idx=0
    append_wrapper_proc_stage "launch"
    while kill -0 "${child_pid}" 2>/dev/null; do
      sleep "${UARTPROBE_WRAPPER_PROC_DIAG_SLEEP_SECONDS}"
      proc_poll_idx=$((proc_poll_idx + 1))
      append_wrapper_proc_stage "poll-${proc_poll_idx}"
    done
    append_wrapper_proc_stage "after-exit"
  ) &
  wrapper_proc_diag_pid=$!
}

stop_wrapper_proc_diag_loop() {
  if [ -n "${wrapper_proc_diag_pid}" ]; then
    wait "${wrapper_proc_diag_pid}" 2>/dev/null || true
    wrapper_proc_diag_pid=""
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

  # Keep the live status path builtin-only so wrapper progress is not gated on
  # a burst of fork/exec-heavy file probes right after launching the child.
  {
    echo "state=${state}"
    echo "status_format=minimal"
    echo "exit_code=${exit_code}"
    echo "wrapper_pid=$$"
    echo "wrapper_child_pid=${child_pid:-}"
    echo "sparse_log_path=${SPARSE_LOG_PATH}"
    echo "deep_log_path=${DEEP_LOG_PATH}"
    echo "status_path=${STATUS_PATH}"
    echo "runner_stage_path=${RUNNER_STAGE_PATH}"
    echo "binary_stage_path=${BINARY_STAGE_PATH}"
    echo "proc_stage_path=${PROC_STAGE_PATH}"
    echo "runner_proc_stage_path=${RUNNER_PROC_STAGE_PATH}"
    echo "runner_early_stage_path=${RUNNER_EARLY_STAGE_PATH}"
    echo "uartprobe_log_mode=${UARTPROBE_LOG_MODE}"
    echo "uartprobe_log_phase=${UARTPROBE_LOG_PHASE}"
    echo "uartprobe_wait_mode=${UARTPROBE_WAIT_MODE}"
    echo "uartprobe_seed_wait_mode=${UARTPROBE_SEED_WAIT_MODE}"
    echo "uartprobe_proc_diag_enable=${UARTPROBE_RUNNER_PROC_DIAG_ENABLE}"
    echo "uartprobe_proc_diag_polls=${UARTPROBE_PROC_DIAG_POLLS}"
    echo "uartprobe_proc_diag_sleep_seconds=${UARTPROBE_PROC_DIAG_SLEEP_SECONDS}"
    echo "uartprobe_proc_diag_kill_on_stuck=${UARTPROBE_PROC_DIAG_KILL_ON_STUCK}"
    echo "uartprobe_wrapper_proc_diag_enable=${UARTPROBE_WRAPPER_PROC_DIAG_ENABLE}"
    echo "uartprobe_wrapper_sync_child_snapshot_enable=${UARTPROBE_WRAPPER_SYNC_CHILD_SNAPSHOT_ENABLE}"
    echo "uartprobe_alias_mode=${UARTPROBE_ALIAS_MODE}"
    echo "uartprobe_bytes=${UARTPROBE_BYTES:-65536}"
    echo "uartprobe_page_bytes=${UARTPROBE_PAGE_BYTES:-1024}"
    echo "uartprobe_dst_offset=${UARTPROBE_DST_OFFSET:-3072}"
    echo "uartprobe_repeat_count=${UARTPROBE_REPEAT_COUNT:-8}"
    echo "capture_echo_stdout=${UARTPROBE_CAPTURE_ECHO_STDOUT}"
    echo "auto_poweroff=${CAPTURE_AUTO_POWEROFF}"
  } > "${STATUS_PATH}"
}

mkdir -p "${LOG_DIR}"
rm -f "${SPARSE_LOG_PATH}" "${DEEP_LOG_PATH}" "${STATUS_PATH}" "${RUNNER_STAGE_PATH}" "${BINARY_STAGE_PATH}" "${PROC_STAGE_PATH}" "${RUNNER_PROC_STAGE_PATH}" "${RUNNER_EARLY_STAGE_PATH}" "${LOG_PIPE_PATH}"
mkfifo "${LOG_PIPE_PATH}"

echo "[firemarshal] uartprobe sparse log path: ${SPARSE_LOG_PATH}"
echo "[firemarshal] uartprobe deep log path: ${DEEP_LOG_PATH}"
echo "[firemarshal] uartprobe runner: ${GUEST_RUNNER}"
echo "[firemarshal-uartprobe] wrapper-enter runner_proc_diag=${UARTPROBE_RUNNER_PROC_DIAG_ENABLE} wrapper_proc_diag=${UARTPROBE_WRAPPER_PROC_DIAG_ENABLE} polls=${UARTPROBE_PROC_DIAG_POLLS} sleep=${UARTPROBE_PROC_DIAG_SLEEP_SECONDS} kill=${UARTPROBE_PROC_DIAG_KILL_ON_STUCK} echo_stdout=${UARTPROBE_CAPTURE_ECHO_STDOUT}"

write_status "starting" ""
flush_capture_state

# Preserve tee as the pipe consumer, but discard its stdout so this control
# only removes guest console echo without switching consumer semantics to cat.
tee -a "${SPARSE_LOG_PATH}" < "${LOG_PIPE_PATH}" > /dev/null &
tee_pid=$!

AUTO_POWEROFF=0 \
  UARTPROBE_RUNNER_LOG_ENABLE="${UARTPROBE_RUNNER_LOG_ENABLE}" \
  UARTPROBE_RUNNER_STAGE_PATH="${RUNNER_STAGE_PATH}" \
  UARTPROBE_BINARY_STAGE_PATH="${BINARY_STAGE_PATH}" \
  UARTPROBE_PROC_STAGE_PATH="${RUNNER_PROC_STAGE_PATH}" \
  UARTPROBE_RUNNER_PROC_STAGE_PATH="${RUNNER_PROC_STAGE_PATH}" \
  UARTPROBE_RUNNER_EARLY_STAGE_PATH="${RUNNER_EARLY_STAGE_PATH}" \
  UARTPROBE_LOG_MODE="${UARTPROBE_LOG_MODE}" \
  UARTPROBE_LOG_PHASE="${UARTPROBE_LOG_PHASE}" \
  UARTPROBE_WAIT_MODE="${UARTPROBE_WAIT_MODE}" \
  UARTPROBE_SEED_WAIT_MODE="${UARTPROBE_SEED_WAIT_MODE}" \
  UARTPROBE_ALIAS_MODE="${UARTPROBE_ALIAS_MODE}" \
  UARTPROBE_PROC_DIAG_ENABLE="${UARTPROBE_RUNNER_PROC_DIAG_ENABLE}" \
  UARTPROBE_PROC_DIAG_POLLS="${UARTPROBE_PROC_DIAG_POLLS}" \
  UARTPROBE_PROC_DIAG_SLEEP_SECONDS="${UARTPROBE_PROC_DIAG_SLEEP_SECONDS}" \
  UARTPROBE_PROC_DIAG_KILL_ON_STUCK="${UARTPROBE_PROC_DIAG_KILL_ON_STUCK}" \
  UARTPROBE_LOG_DIR="${LOG_DIR}" \
  UARTPROBE_LOG_PATH="${DEEP_LOG_PATH}" \
  "${GUEST_RUNNER}" "$@" > "${LOG_PIPE_PATH}" 2>&1 &
child_pid=$!

append_wrapper_stage_line "after-child-launch"
append_wrapper_sync_child_snapshot "main-after-child-launch"
start_periodic_sync_loop
append_wrapper_stage_line "after-periodic-sync-start"
start_wrapper_proc_diag_loop
append_wrapper_stage_line "after-wrapper-procdiag-start"
write_status "running" ""
append_wrapper_stage_line "after-running-status"
append_wrapper_sync_child_snapshot "main-after-running-status"
flush_capture_state

if wait "${child_pid}"; then
  rc=0
else
  rc=$?
fi

stop_log_tee
stop_wrapper_proc_diag_loop
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
