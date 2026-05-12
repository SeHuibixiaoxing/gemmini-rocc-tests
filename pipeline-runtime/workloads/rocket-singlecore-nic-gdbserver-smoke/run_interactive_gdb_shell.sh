#!/bin/sh

set -eu

LOG_DIR="${INTERACTIVE_GDB_LOG_DIR:-/root/gdbserver-smoke}"
INFO_PATH="${INTERACTIVE_GDB_INFO_PATH:-${LOG_DIR}/interactive-gdb.info}"
LOG_PATH="${INTERACTIVE_GDB_LOG_PATH:-${LOG_DIR}/interactive-gdb.log}"
GDB_TOOL="${INTERACTIVE_GDB_TOOL:-/usr/bin/gdb}"
TARGET_BIN="${INTERACTIVE_GDB_BIN:-/root/gdbserver-smoke/gdbserver-smoke}"

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
  printf '[interactive-gdb] phase=%s gdb_tool=%s target_bin=%s\n' \
    "${phase}" "${GDB_TOOL}" "${TARGET_BIN}" >/dev/console
}

{
  printf '[interactive-gdb] gdb_tool=%s\n' "${GDB_TOOL}"
  printf '[interactive-gdb] target_bin=%s\n' "${TARGET_BIN}"
  printf '[interactive-gdb] tty=%s\n' "$(tty 2>/dev/null || true)"
} > "${LOG_PATH}"

if [ -x /bin/stty ]; then
  /bin/stty -F /dev/console sane 2>/dev/null || true
elif [ -x /usr/bin/stty ]; then
  /usr/bin/stty -F /dev/console sane 2>/dev/null || true
fi

if [ ! -x "${GDB_TOOL}" ]; then
  write_info "missing-gdb" "path=${GDB_TOOL}"
  announce_console "missing-gdb"
else
  write_info "ready"
  announce_console "ready"
fi

if [ ! -x "${TARGET_BIN}" ]; then
  write_info "missing-target-bin" "path=${TARGET_BIN}"
  announce_console "missing-target-bin"
fi

cat >/dev/console <<EOF
[interactive-gdb] UART shell is ready.
[interactive-gdb] Try:
  ${GDB_TOOL} -q ${TARGET_BIN}
  set environment GDBSERVER_SMOKE_SLEEP_ITERS 0
  break main
  run
  bt
EOF

export HOME=/root
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
cd /root

if command -v setsid >/dev/null 2>&1 && command -v cttyhack >/dev/null 2>&1; then
  setsid cttyhack /bin/sh -i </dev/console >/dev/console 2>&1 || true
else
  /bin/sh -i </dev/console >/dev/console 2>&1 || true
fi

write_info "shell-exited"
announce_console "shell-exited"
while true; do
  sleep 3600
done
