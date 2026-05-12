#!/bin/sh

set -eu

LOG_DIR="${GDBSERVER_SMOKE_LOG_DIR:-/root/gdbserver-smoke}"
INFO_PATH="${GDBSERVER_SMOKE_INFO_PATH:-${LOG_DIR}/gdbserver.info}"
LOG_PATH="${GDBSERVER_SMOKE_LOG_PATH:-${LOG_DIR}/gdbserver.log}"
GDBSERVER_TOOL="${GDBSERVER_SMOKE_TOOL:-/usr/bin/gdbserver}"
GDBSERVER_PORT="${GDBSERVER_SMOKE_PORT:-2345}"
NET_DEV="${GDBSERVER_SMOKE_NET_DEV:-eth0}"
TARGET_BIN="${GDBSERVER_SMOKE_BIN:-/root/gdbserver-smoke/gdbserver-smoke}"
IP_ATTEMPTS="${GDBSERVER_SMOKE_IP_ATTEMPTS:-1}"
IP_SLEEP_SECS="${GDBSERVER_SMOKE_IP_SLEEP_SECS:-1}"

mkdir -p "${LOG_DIR}"

find_tool() {
  for candidate in "$@"; do
    if [ -x "${candidate}" ]; then
      printf '%s\n' "${candidate}"
      return 0
    fi
  done
  return 1
}

detect_guest_ipv4() {
  ip_tool="$(find_tool /sbin/ip /bin/ip /usr/sbin/ip /usr/bin/ip || true)"
  if [ -n "${ip_tool}" ]; then
    "${ip_tool}" -4 -o addr show dev "${NET_DEV}" 2>/dev/null | awk '{print $4}' | cut -d/ -f1 | head -n 1
    return 0
  fi

  ifconfig_tool="$(find_tool /sbin/ifconfig /bin/ifconfig /usr/sbin/ifconfig /usr/bin/ifconfig || true)"
  if [ -n "${ifconfig_tool}" ]; then
    "${ifconfig_tool}" "${NET_DEV}" 2>/dev/null | awk '/inet addr:/ {sub("addr:", "", $2); print $2; exit} /inet / {print $2; exit}'
    return 0
  fi

  return 1
}

fallback_guest_ipv4_from_mac() {
  mac_path="/sys/class/net/${NET_DEV}/address"
  if [ ! -r "${mac_path}" ]; then
    return 1
  fi

  mac="$(cat "${mac_path}")"
  high="$(printf '%d' "0x$(echo "${mac}" | cut -d: -f5)")"
  low="$(printf '%d' "0x$(echo "${mac}" | cut -d: -f6)")"
  printf '172.16.%s.%s\n' "${high}" "${low}"
}

write_info() {
  phase="$1"
  extra="${2:-}"
  {
    printf 'phase=%s\n' "${phase}"
    printf 'net_dev=%s\n' "${NET_DEV}"
    printf 'guest_ipv4=%s\n' "${GUEST_IPV4:-unknown}"
    printf 'endpoint=%s:%s\n' "${GUEST_IPV4:-unknown}" "${GDBSERVER_PORT}"
    printf 'pid=%s\n' "${GDBSERVER_PID:-0}"
    if [ -n "${extra}" ]; then
      printf 'extra=%s\n' "${extra}"
    fi
  } > "${INFO_PATH}"
}

announce_console() {
  phase="$1"
  printf '[gdbserver] phase=%s net_dev=%s guest_ipv4=%s endpoint=%s:%s pid=%s\n' \
    "${phase}" \
    "${NET_DEV}" \
    "${GUEST_IPV4:-unknown}" \
    "${GUEST_IPV4:-unknown}" \
    "${GDBSERVER_PORT}" \
    "${GDBSERVER_PID:-0}" >/dev/console
}

echo "[gdbserver-smoke] target_bin=${TARGET_BIN}" > "${LOG_PATH}"
echo "[gdbserver-smoke] gdbserver_tool=${GDBSERVER_TOOL}" >> "${LOG_PATH}"
echo "[gdbserver-smoke] net_dev=${NET_DEV}" >> "${LOG_PATH}"
echo "[gdbserver-smoke] ip_attempts=${IP_ATTEMPTS}" >> "${LOG_PATH}"

if [ ! -x "${GDBSERVER_TOOL}" ]; then
  echo "[gdbserver-smoke] missing gdbserver tool: ${GDBSERVER_TOOL}" >> "${LOG_PATH}"
  write_info "missing-gdbserver" "path=${GDBSERVER_TOOL}"
  sync
  poweroff -f || poweroff || true
  exit 1
fi

if [ ! -x "${TARGET_BIN}" ]; then
  echo "[gdbserver-smoke] missing target bin: ${TARGET_BIN}" >> "${LOG_PATH}"
  write_info "missing-target-bin" "path=${TARGET_BIN}"
  sync
  poweroff -f || poweroff || true
  exit 1
fi

attempt=0
GUEST_IPV4=""
while [ "${attempt}" -lt "${IP_ATTEMPTS}" ]; do
  GUEST_IPV4="$(detect_guest_ipv4 || true)"
  if [ -n "${GUEST_IPV4}" ]; then
    break
  fi
  attempt=$((attempt + 1))
  if [ "${attempt}" -lt "${IP_ATTEMPTS}" ]; then
    sleep "${IP_SLEEP_SECS}"
  fi
done

if [ -z "${GUEST_IPV4}" ]; then
  GUEST_IPV4="$(fallback_guest_ipv4_from_mac || true)"
fi

echo "[gdbserver-smoke] guest_ipv4=${GUEST_IPV4:-unknown}" >> "${LOG_PATH}"
{
  printf '[gdbserver-smoke] net snapshot begin\n'
  ip_tool="$(find_tool /sbin/ip /bin/ip /usr/sbin/ip /usr/bin/ip || true)"
  if [ -n "${ip_tool}" ]; then
    "${ip_tool}" addr show dev "${NET_DEV}" 2>&1 || true
    "${ip_tool}" route 2>&1 || true
  fi
  printf '[gdbserver-smoke] net snapshot end\n'
} >> "${LOG_PATH}"
write_info "prelaunch"
announce_console "prelaunch"

"${GDBSERVER_TOOL}" --once "0.0.0.0:${GDBSERVER_PORT}" "${TARGET_BIN}" >> "${LOG_PATH}" 2>&1 &
GDBSERVER_PID=$!

attempt=0
listening=0
while [ "${attempt}" -lt 30 ]; do
  if grep -q "Listening on port" "${LOG_PATH}" 2>/dev/null; then
    listening=1
    break
  fi
  if ! kill -0 "${GDBSERVER_PID}" 2>/dev/null; then
    break
  fi
  attempt=$((attempt + 1))
  sleep 1
done

if [ "${listening}" -eq 1 ]; then
  write_info "listening"
  announce_console "listening"
else
  write_info "prelisten-timeout"
  announce_console "prelisten-timeout"
fi

set +e
wait "${GDBSERVER_PID}"
rc=$?
set -e

echo "[gdbserver-smoke] gdbserver exited rc=${rc}" >> "${LOG_PATH}"
write_info "exit" "rc=${rc}"
announce_console "exit"
sync
poweroff -f || poweroff || true
exit "${rc}"
