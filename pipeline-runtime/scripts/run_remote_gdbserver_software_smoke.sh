#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_remote_gdbserver_software_smoke.sh <run-host-private-ip> [local-port]

Run the software-breakpoint-only remote gdbserver regression through the FireSim
run host. The target workload must already be running and the guest gdbserver
must be reachable at 172.16.0.2:2345 through the FireSim NIC.

Optional environment:
  GDBSERVER_SMOKE_REMOTE_TIMEOUT      GDB RSP packet timeout in seconds
                                      (default: 120)
  GDBSERVER_SMOKE_TCP_CONNECT_TIMEOUT GDB TCP connect timeout in seconds
                                      (default: 60)
  GDBSERVER_SMOKE_GUEST_IP            Guest-side gdbserver IPv4
                                      (default: 172.16.0.2)
  GDBSERVER_SMOKE_TAP_DEV             Run-host tap device (default: tap0)
  GDBSERVER_SMOKE_STATIC_NEIGH_MAC    If non-empty, install this static
                                      neighbor entry before opening the tunnel
                                      (default: 00:12:6d:00:00:02)

This script deliberately does not use hbreak/watch. It also does not probe the
guest port with nc/telnet, because the smoke workload uses gdbserver --once and
the first TCP connection must be a real GDB remote connection.
EOF
}

if [[ $# -lt 1 || "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit $([[ $# -lt 1 ]] && echo 2 || echo 0)
fi

private_ip="$1"
local_port="${2:-32345}"

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
ssh_key_path="${FIRESIM_SSH_KEY_PATH:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
gdb="${RISCV_GDB:-${cy_dir}/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb}"
target_bin="${GDBSERVER_SMOKE_HOST_BIN:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke}"
remote_timeout="${GDBSERVER_SMOKE_REMOTE_TIMEOUT:-120}"
tcp_connect_timeout="${GDBSERVER_SMOKE_TCP_CONNECT_TIMEOUT:-60}"
guest_ip="${GDBSERVER_SMOKE_GUEST_IP:-172.16.0.2}"
tap_dev="${GDBSERVER_SMOKE_TAP_DEV:-tap0}"
static_neigh_mac="${GDBSERVER_SMOKE_STATIC_NEIGH_MAC:-00:12:6d:00:00:02}"
out_root="${GDBSERVER_SMOKE_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/remote-swbreak-${stamp}-${private_ip//./_}"
gdb_cmds="${out_dir}/remote-swbreak.gdb"
gdb_log="${out_dir}/remote-swbreak.gdb.log"
tunnel_log="${out_dir}/ssh-tunnel.log"

mkdir -p "${out_dir}"

if [[ ! -x "${gdb}" ]]; then
  echo "missing gdb: ${gdb}" >&2
  exit 1
fi
if [[ ! -f "${target_bin}" ]]; then
  echo "missing target binary: ${target_bin}" >&2
  exit 1
fi
if [[ ! -r "${ssh_key_path}" ]]; then
  echo "missing SSH key: ${ssh_key_path}" >&2
  exit 1
fi

cat > "${gdb_cmds}" <<EOF
set pagination off
set confirm off
set print thread-events off
set breakpoint pending off
set debuginfod enabled off
set mi-async on
set remotetimeout ${remote_timeout}
set tcp connect-timeout ${tcp_connect_timeout}
file ${target_bin}
target remote :${local_port}
echo GDB_MARK_CONNECTED\n
break main
break smoke_iteration_hook
break smoke_worker_heartbeat
info breakpoints
continue
echo GDB_MARK_HIT_MAIN\n
bt
info registers pc sp ra a0 a1
x/12i \$pc
print smoke_counter
print/x smoke_memory_probe[0]
set variable smoke_memory_probe[7] = 0xabcdef
print/x smoke_memory_probe[7]
next
echo GDB_MARK_NEXT_DONE\n
disable 1
continue
echo GDB_MARK_HIT_MULTI_SOFTWARE_BREAK\n
bt
info args
info threads
thread apply all bt
delete
break smoke_iteration_hook if iteration >= 2
continue
echo GDB_MARK_HIT_CONDITIONAL_BREAK\n
bt
info args
info registers pc sp
delete
echo GDB_MARK_INTERRUPT_BEGIN\n
continue&
shell sleep 2
interrupt
echo GDB_MARK_INTERRUPT_DONE\n
info threads
thread apply all bt
info registers pc sp ra
x/8i \$pc
signal 0
quit
EOF

echo "[remote-swbreak] private_ip=${private_ip}"
echo "[remote-swbreak] local_port=${local_port}"
echo "[remote-swbreak] guest_ip=${guest_ip}"
echo "[remote-swbreak] target_bin=${target_bin}"
echo "[remote-swbreak] out_dir=${out_dir}"

if [[ -n "${static_neigh_mac}" ]]; then
  echo "[remote-swbreak] static_neigh=${guest_ip} ${static_neigh_mac} dev ${tap_dev}"
  ssh \
    -i "${ssh_key_path}" \
    -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -o ConnectTimeout=10 \
    "${ssh_user}@${private_ip}" \
    "sudo ip neigh replace '${guest_ip}' lladdr '${static_neigh_mac}' dev '${tap_dev}' nud permanent && ip neigh show dev '${tap_dev}'" \
    >"${out_dir}/static-neigh.log" 2>&1
fi

ssh -N \
  -L "${local_port}:${guest_ip}:2345" \
  -i "${ssh_key_path}" \
  -o ExitOnForwardFailure=yes \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ServerAliveInterval=30 \
  -o ServerAliveCountMax=3 \
  "${ssh_user}@${private_ip}" >"${tunnel_log}" 2>&1 &
tunnel_pid=$!

cleanup() {
  if kill -0 "${tunnel_pid}" 2>/dev/null; then
    kill "${tunnel_pid}" 2>/dev/null || true
    wait "${tunnel_pid}" 2>/dev/null || true
  fi
}
trap cleanup EXIT

for _ in $(seq 1 50); do
  if ss -ltn "sport = :${local_port}" | grep -q ":${local_port}"; then
    break
  fi
  if ! kill -0 "${tunnel_pid}" 2>/dev/null; then
    echo "SSH tunnel exited before listening; see ${tunnel_log}" >&2
    exit 1
  fi
  sleep 0.1
done

if ! ss -ltn "sport = :${local_port}" | grep -q ":${local_port}"; then
  echo "SSH tunnel did not listen on local port ${local_port}; see ${tunnel_log}" >&2
  exit 1
fi

set +e
"${gdb}" -q -batch -x "${gdb_cmds}" >"${gdb_log}" 2>&1
gdb_rc=$?
set -e

echo "[remote-swbreak] gdb_rc=${gdb_rc}"
echo "[remote-swbreak] gdb_log=${gdb_log}"

if [[ "${gdb_rc}" -ne 0 ]]; then
  tail -n 160 "${gdb_log}" >&2 || true
  exit "${gdb_rc}"
fi

for marker in \
  GDB_MARK_CONNECTED \
  GDB_MARK_HIT_MAIN \
  GDB_MARK_NEXT_DONE \
  GDB_MARK_HIT_MULTI_SOFTWARE_BREAK \
  GDB_MARK_HIT_CONDITIONAL_BREAK \
  GDB_MARK_INTERRUPT_BEGIN \
  GDB_MARK_INTERRUPT_DONE; do
  if ! grep -q "${marker}" "${gdb_log}"; then
    echo "missing ${marker}; see ${gdb_log}" >&2
    tail -n 160 "${gdb_log}" >&2 || true
    exit 1
  fi
done

if ! grep -Eq "Breakpoint 1, main|GDB_MARK_HIT_MAIN" "${gdb_log}"; then
  echo "did not hit main with a software breakpoint; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

if ! grep -Eq "smoke_iteration_hook|smoke_worker_heartbeat" "${gdb_log}"; then
  echo "did not hit a non-main software breakpoint; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

if ! grep -q "0xabcdef" "${gdb_log}"; then
  echo "memory write/read evidence missing; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

if ! grep -Eq "Thread .*|thread apply all bt" "${gdb_log}"; then
  echo "thread inspection evidence missing; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

if ! grep -Eq "exited normally|exited with code 0" "${gdb_log}"; then
  echo "inferior did not exit normally after interrupt recovery; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

echo "[remote-swbreak] PASS"
