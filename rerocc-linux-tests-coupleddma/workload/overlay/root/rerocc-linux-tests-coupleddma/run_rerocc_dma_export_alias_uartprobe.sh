#!/bin/sh
set -eu

ROOT_DIR="/root/rerocc-linux-tests-coupleddma"
BIN="${UARTPROBE_BIN_OVERRIDE:-${ROOT_DIR}/rerocc_dma_export_alias_uartprobe-linux}"
LOG_DIR="${UARTPROBE_LOG_DIR:-/root/pipeline-runtime-debug}"
LOG_PATH="${UARTPROBE_LOG_PATH:-${LOG_DIR}/uartprobe.deep.log}"
RUNNER_STAGE_PATH="${UARTPROBE_RUNNER_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.stage}"
RUNNER_POST_STAGE_PATH="${UARTPROBE_RUNNER_POST_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.post.stage}"
BINARY_STAGE_PATH="${UARTPROBE_BINARY_STAGE_PATH:-${LOG_DIR}/uartprobe.binary.stage}"
RUNNER_EARLY_STAGE_PATH="${UARTPROBE_RUNNER_EARLY_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.early.stage}"
PROC_STAGE_PATH="${UARTPROBE_RUNNER_PROC_STAGE_PATH:-${UARTPROBE_PROC_STAGE_PATH:-${LOG_DIR}/uartprobe.runner.proc.stage}}"
AUTO_POWEROFF="${AUTO_POWEROFF:-1}"
UARTPROBE_OBSERVABILITY="${UARTPROBE_OBSERVABILITY:-full}"
UARTPROBE_RUNNER_LOG_ENABLE="${UARTPROBE_RUNNER_LOG_ENABLE:-1}"
UARTPROBE_RUNNER_STAGE_ENABLE="${UARTPROBE_RUNNER_STAGE_ENABLE:-auto}"
UARTPROBE_LOG_MODE="${UARTPROBE_LOG_MODE:-stdout_write}"
UARTPROBE_LOG_PHASE="${UARTPROBE_LOG_PHASE:-post_submit_pre_wait}"
UARTPROBE_WAIT_MODE="${UARTPROBE_WAIT_MODE:-fence}"
UARTPROBE_SEED_WAIT_MODE="${UARTPROBE_SEED_WAIT_MODE:-fence}"
UARTPROBE_ALIAS_MODE="${UARTPROBE_ALIAS_MODE:-abab}"
UARTPROBE_BYTES="${UARTPROBE_BYTES:-65536}"
UARTPROBE_PAGE_BYTES="${UARTPROBE_PAGE_BYTES:-1024}"
UARTPROBE_DST_OFFSET="${UARTPROBE_DST_OFFSET:-3072}"
UARTPROBE_REPEAT_COUNT="${UARTPROBE_REPEAT_COUNT:-8}"
UARTPROBE_BURST_LINES="${UARTPROBE_BURST_LINES:-1}"
UARTPROBE_BURST_BYTES="${UARTPROBE_BURST_BYTES:-96}"
NUM_GEMMINI="${NUM_GEMMINI:-2}"
DMA_BASE_ID="${DMA_BASE_ID:-${NUM_GEMMINI}}"
UARTPROBE_DMA_MANAGER_ID="${UARTPROBE_DMA_MANAGER_ID:-${DMA_BASE_ID}}"
UARTPROBE_SRC_LOCAL_ADDR="${UARTPROBE_SRC_LOCAL_ADDR:-132096}"
UARTPROBE_DUMP_RECORDS="${UARTPROBE_DUMP_RECORDS:-0}"
UARTPROBE_PROC_DIAG_ENABLE="${UARTPROBE_PROC_DIAG_ENABLE:-0}"
UARTPROBE_PROC_DIAG_POLLS="${UARTPROBE_PROC_DIAG_POLLS:-8}"
UARTPROBE_PROC_DIAG_SLEEP_SECONDS="${UARTPROBE_PROC_DIAG_SLEEP_SECONDS:-1}"
UARTPROBE_PROC_DIAG_KILL_ON_STUCK="${UARTPROBE_PROC_DIAG_KILL_ON_STUCK:-1}"

mkdir -p "${LOG_DIR}"
rm -f "${RUNNER_POST_STAGE_PATH}"

observability_is_full() {
  [ "${UARTPROBE_OBSERVABILITY}" = "full" ]
}

runner_stage_enabled() {
  case "${UARTPROBE_RUNNER_STAGE_ENABLE}" in
    1|true|TRUE|yes|YES)
      return 0
      ;;
    0|false|FALSE|no|NO)
      return 1
      ;;
    auto|AUTO)
      observability_is_full
      return $?
      ;;
    *)
      observability_is_full
      return $?
      ;;
  esac
}

write_runner_early_stage() {
  stage="$1"
  runner_stage_enabled || return 0
  echo "stage=${stage} pid=$$ ppid=${PPID:-} log_mode=${UARTPROBE_LOG_MODE} log_phase=${UARTPROBE_LOG_PHASE} wait_mode=${UARTPROBE_WAIT_MODE} seed_wait_mode=${UARTPROBE_SEED_WAIT_MODE}" >> "${RUNNER_EARLY_STAGE_PATH}"
}

write_runner_early_stage "shell-enter"

if [ ! -x "${BIN}" ]; then
  write_runner_early_stage "missing-bin"
  echo "missing UART probe binary: ${BIN}" >&2
  exit 1
fi

write_runner_early_stage "after-bin-check"

write_runner_stage() {
  stage="$1"
  runner_stage_enabled || return 0
  echo "stage=${stage} pid=$$ ppid=${PPID:-} log_mode=${UARTPROBE_LOG_MODE} log_phase=${UARTPROBE_LOG_PHASE} wait_mode=${UARTPROBE_WAIT_MODE} seed_wait_mode=${UARTPROBE_SEED_WAIT_MODE}" >> "${RUNNER_STAGE_PATH}"
}

write_runner_post_stage() {
  stage="$1"
  runner_stage_enabled || return 0
  echo "stage=${stage} pid=$$ ppid=${PPID:-} runner_log_enable=${UARTPROBE_RUNNER_LOG_ENABLE} log_mode=${UARTPROBE_LOG_MODE} log_phase=${UARTPROBE_LOG_PHASE} wait_mode=${UARTPROBE_WAIT_MODE} seed_wait_mode=${UARTPROBE_SEED_WAIT_MODE}" >> "${RUNNER_POST_STAGE_PATH}"
}

runner_log() {
  observability_is_full || return 0
  if [ "${UARTPROBE_RUNNER_LOG_ENABLE}" = "0" ]; then
    return 0
  fi
  echo "$@"
}

append_proc_stage() {
  label="$1"
  pid="$2"
  runner_stage_enabled || return 0

  {
    echo "sample=${label}"
    echo "pid=${pid}"
    echo "--- cmdline ---"
    tr '\000' ' ' < "/proc/${pid}/cmdline" 2>/dev/null || true
    echo
    echo "--- exe ---"
    readlink "/proc/${pid}/exe" 2>/dev/null || true
    echo "--- fd1 ---"
    readlink "/proc/${pid}/fd/1" 2>/dev/null || true
    cat "/proc/${pid}/fdinfo/1" 2>/dev/null || true
    echo "--- fd2 ---"
    readlink "/proc/${pid}/fd/2" 2>/dev/null || true
    cat "/proc/${pid}/fdinfo/2" 2>/dev/null || true
    echo "--- wchan ---"
    cat "/proc/${pid}/wchan" 2>/dev/null || true
    echo "--- syscall ---"
    cat "/proc/${pid}/syscall" 2>/dev/null || true
    echo "--- status ---"
    cat "/proc/${pid}/status" 2>/dev/null || true
    echo "--- stat ---"
    cat "/proc/${pid}/stat" 2>/dev/null || true
    echo "===="
  } >> "${PROC_STAGE_PATH}"
}

run_with_proc_diag() {
  rc=0
  rm -f "${PROC_STAGE_PATH}"
  "${BIN}" "$@" &
  child_pid=$!
  write_runner_stage "after-fork pid=${child_pid}"
  write_runner_stage "before-proc-launch pid=${child_pid}"
  append_proc_stage "launch" "${child_pid}"
  write_runner_stage "after-proc-launch pid=${child_pid}"

  poll_idx=1
  while [ "${poll_idx}" -le "${UARTPROBE_PROC_DIAG_POLLS}" ]; do
    if ! kill -0 "${child_pid}" 2>/dev/null; then
      break
    fi
    write_runner_stage "before-proc-poll-${poll_idx} pid=${child_pid}"
    append_proc_stage "poll-${poll_idx}" "${child_pid}"
    write_runner_stage "after-proc-poll-${poll_idx} pid=${child_pid}"
    sleep "${UARTPROBE_PROC_DIAG_SLEEP_SECONDS}"
    poll_idx=$((poll_idx + 1))
  done

  if kill -0 "${child_pid}" 2>/dev/null; then
    write_runner_stage "stuck-child pid=${child_pid}"
    write_runner_stage "before-stuck-proc pid=${child_pid}"
    append_proc_stage "stuck-before-kill" "${child_pid}"
    write_runner_stage "after-stuck-proc pid=${child_pid}"
    if [ "${UARTPROBE_PROC_DIAG_KILL_ON_STUCK}" = "1" ]; then
      kill "${child_pid}" 2>/dev/null || true
      sleep 1
      if kill -0 "${child_pid}" 2>/dev/null; then
        kill -9 "${child_pid}" 2>/dev/null || true
        sleep 1
      fi
      if kill -0 "${child_pid}" 2>/dev/null; then
        append_proc_stage "still-alive-after-kill" "${child_pid}"
      fi
    fi
  fi

  if wait "${child_pid}"; then
    rc=0
  else
    rc=$?
  fi
  write_runner_stage "before-after-wait-proc rc=${rc} pid=${child_pid}"
  append_proc_stage "after-wait rc=${rc}" "${child_pid}"
  write_runner_stage "after-after-wait-proc rc=${rc} pid=${child_pid}"
  return "${rc}"
}

export UARTPROBE_LOG_PATH="${LOG_PATH}"
export UARTPROBE_OBSERVABILITY
export UARTPROBE_LOG_MODE
export UARTPROBE_LOG_PHASE
export UARTPROBE_WAIT_MODE
export UARTPROBE_SEED_WAIT_MODE
export UARTPROBE_ALIAS_MODE
export UARTPROBE_BYTES
export UARTPROBE_PAGE_BYTES
export UARTPROBE_DST_OFFSET
export UARTPROBE_REPEAT_COUNT
export UARTPROBE_BURST_LINES
export UARTPROBE_BURST_BYTES
export UARTPROBE_DMA_MANAGER_ID
export UARTPROBE_SRC_LOCAL_ADDR
export UARTPROBE_DUMP_RECORDS
export UARTPROBE_RUNNER_STAGE_PATH="${RUNNER_STAGE_PATH}"
export UARTPROBE_RUNNER_POST_STAGE_PATH="${RUNNER_POST_STAGE_PATH}"
export UARTPROBE_BINARY_STAGE_PATH="${BINARY_STAGE_PATH}"
export UARTPROBE_PROC_STAGE_PATH="${PROC_STAGE_PATH}"
export UARTPROBE_RUNNER_EARLY_STAGE_PATH="${RUNNER_EARLY_STAGE_PATH}"
export UARTPROBE_RUNNER_STAGE_ENABLE

write_runner_early_stage "before-runner-enter"
write_runner_early_stage "before-runner-stage"
write_runner_stage "runner-enter"
write_runner_early_stage "after-runner-stage"
write_runner_post_stage "after-runner-stage"
runner_log "[uartprobe] runner-enter log_mode=${UARTPROBE_LOG_MODE} log_phase=${UARTPROBE_LOG_PHASE} wait=${UARTPROBE_WAIT_MODE} seed_wait=${UARTPROBE_SEED_WAIT_MODE} alias=${UARTPROBE_ALIAS_MODE}"
write_runner_early_stage "after-runner-log-enter"
write_runner_post_stage "after-runner-log-enter"
runner_log "[uartprobe] runner-config bytes=${UARTPROBE_BYTES} page_bytes=${UARTPROBE_PAGE_BYTES} dst_offset=${UARTPROBE_DST_OFFSET} repeats=${UARTPROBE_REPEAT_COUNT} dma_mgr=${UARTPROBE_DMA_MANAGER_ID} src_local_addr=${UARTPROBE_SRC_LOCAL_ADDR}"
write_runner_early_stage "after-runner-log-config"
write_runner_post_stage "after-runner-log-config"
runner_log "[uartprobe] runner-log-path ${UARTPROBE_LOG_PATH}"
write_runner_early_stage "after-runner-log-path"
write_runner_post_stage "after-runner-log-path"
write_runner_stage "before-bin"
write_runner_early_stage "after-before-bin-stage"
write_runner_post_stage "after-before-bin-stage"
write_runner_early_stage "before-bin-launch"
write_runner_post_stage "before-bin-launch"
rm -f "${BINARY_STAGE_PATH}"
rm -f "${PROC_STAGE_PATH}"

if [ "${UARTPROBE_PROC_DIAG_ENABLE}" = "1" ]; then
  if run_with_proc_diag "$@"; then
    rc=0
  else
    rc=$?
  fi
else
  if "${BIN}" "$@"; then
    rc=0
  else
    rc=$?
  fi
fi

write_runner_early_stage "after-bin-launch rc=${rc}"
write_runner_stage "after-bin rc=${rc}"
write_runner_post_stage "after-bin-launch rc=${rc}"
runner_log "[uartprobe] runner-exit rc=${rc}"

sync || true
if [ "${AUTO_POWEROFF}" = "0" ]; then
  exit "${rc}"
fi

poweroff -f
exit "${rc}"
