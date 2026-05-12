#!/bin/sh

set -eu

LOG_DIR="${LOCAL_GDB_SMOKE_LOG_DIR:-/root/gdbserver-smoke}"
INFO_PATH="${LOCAL_GDB_SMOKE_INFO_PATH:-${LOG_DIR}/local-gdb.info}"
LOG_PATH="${LOCAL_GDB_SMOKE_LOG_PATH:-${LOG_DIR}/local-gdb.log}"
GDB_TOOL="${LOCAL_GDB_SMOKE_TOOL:-/usr/bin/gdb}"
TARGET_BIN="${LOCAL_GDB_SMOKE_BIN:-/root/gdbserver-smoke/gdbserver-smoke}"
GDB_CMDS="${LOCAL_GDB_SMOKE_CMDS:-/tmp/local-gdb-smoke.gdb}"
GDB_TIMEOUT_SECS="${LOCAL_GDB_SMOKE_TIMEOUT_SECS:-300}"
GDB_STATUS_INTERVAL_SECS="${LOCAL_GDB_SMOKE_STATUS_INTERVAL_SECS:-30}"
SMOKE_SLEEP_ITERS="${LOCAL_GDB_SMOKE_SLEEP_ITERS:-0}"

mkdir -p "${LOG_DIR}"

write_info() {
  phase="$1"
  extra="${2:-}"
  {
    printf 'phase=%s\n' "${phase}"
    printf 'gdb_tool=%s\n' "${GDB_TOOL}"
    printf 'target_bin=%s\n' "${TARGET_BIN}"
    if [ -n "${extra}" ]; then
      printf 'extra=%s\n' "${extra}"
    fi
  } > "${INFO_PATH}"
}

announce_console() {
  phase="$1"
  printf '[local-gdb] phase=%s gdb_tool=%s target_bin=%s\n' \
    "${phase}" "${GDB_TOOL}" "${TARGET_BIN}" >/dev/console
}

announce_status() {
  phase="$1"
  extra="${2:-}"
  if [ -n "${extra}" ]; then
    printf '[local-gdb] phase=%s %s gdb_tool=%s target_bin=%s\n' \
      "${phase}" "${extra}" "${GDB_TOOL}" "${TARGET_BIN}" >/dev/console
  else
    announce_console "${phase}"
  fi
}

{
  printf '[local-gdb-smoke] target_bin=%s\n' "${TARGET_BIN}"
  printf '[local-gdb-smoke] gdb_tool=%s\n' "${GDB_TOOL}"
  printf '[local-gdb-smoke] timeout_secs=%s\n' "${GDB_TIMEOUT_SECS}"
  printf '[local-gdb-smoke] smoke_sleep_iters=%s\n' "${SMOKE_SLEEP_ITERS}"
} > "${LOG_PATH}"

if [ ! -x "${GDB_TOOL}" ]; then
  echo "[local-gdb-smoke] missing gdb tool: ${GDB_TOOL}" >> "${LOG_PATH}"
  write_info "missing-gdb" "path=${GDB_TOOL}"
  announce_console "missing-gdb"
  sync
  poweroff -f || poweroff || true
  exit 1
fi

if [ ! -x "${TARGET_BIN}" ]; then
  echo "[local-gdb-smoke] missing target bin: ${TARGET_BIN}" >> "${LOG_PATH}"
  write_info "missing-target-bin" "path=${TARGET_BIN}"
  announce_console "missing-target-bin"
  sync
  poweroff -f || poweroff || true
  exit 1
fi

cat > "${GDB_CMDS}" <<EOF
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set auto-load safe-path /
set environment GDBSERVER_SMOKE_SLEEP_ITERS ${SMOKE_SLEEP_ITERS}
break main
run
bt
info registers
x/8i $pc
continue
quit
EOF

write_info "running"
announce_status "running" "timeout_secs=${GDB_TIMEOUT_SECS}"

set +e
"${GDB_TOOL}" -q -batch -x "${GDB_CMDS}" --args "${TARGET_BIN}" >> "${LOG_PATH}" 2>&1 &
gdb_pid=$!
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
    echo "[local-gdb-smoke] timeout elapsed_secs=${elapsed}; interrupting gdb pid=${gdb_pid}" >> "${LOG_PATH}"
    announce_status "timeout" "elapsed_secs=${elapsed}"
    kill -INT "${gdb_pid}" 2>/dev/null || true
    sleep 5
    kill -TERM "${gdb_pid}" 2>/dev/null || true
    sleep 5
    kill -KILL "${gdb_pid}" 2>/dev/null || true
    break
  fi
done

wait "${gdb_pid}"
gdb_rc=$?
if [ "${timed_out}" -ne 0 ]; then
  gdb_rc=124
fi
set -e

echo "[local-gdb-smoke] gdb exited rc=${gdb_rc}" >> "${LOG_PATH}"
{
  printf '[local-gdb] gdb log tail begin\n'
  tail -80 "${LOG_PATH}" 2>/dev/null || true
  printf '[local-gdb] gdb log tail end\n'
} >/dev/console
if grep -q "Inferior .* exited normally" "${LOG_PATH}" 2>/dev/null; then
  write_info "exit" "gdb_rc=${gdb_rc},inferior=exited-normally"
elif grep -q "Inferior .* exited with code 0" "${LOG_PATH}" 2>/dev/null; then
  write_info "exit" "gdb_rc=${gdb_rc},inferior=exited-code-0"
elif [ "${timed_out}" -ne 0 ]; then
  write_info "timeout" "gdb_rc=${gdb_rc},elapsed_secs=${elapsed}"
else
  write_info "exit" "gdb_rc=${gdb_rc},inferior=unknown-or-nonzero"
fi
announce_console "exit"
sync
poweroff -f || poweroff || true
exit "${gdb_rc}"
