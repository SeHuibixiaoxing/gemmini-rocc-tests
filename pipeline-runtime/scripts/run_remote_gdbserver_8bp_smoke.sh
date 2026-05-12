#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_remote_gdbserver_8bp_smoke.sh <run-host-private-ip> [local-port]

Run a remote gdbserver smoke test through the FireSim run host. This covers the
previously validated remote-gdb feature set and additionally validates that
eight hardware breakpoints can be inserted at once and that breakpoint resources
can be deleted/reused before the inferior exits normally.

The script does not probe the target gdbserver port with nc/telnet, because the
smoke workload uses gdbserver --once and the first TCP connection must be gdb.
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
out_root="${GDBSERVER_SMOKE_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/remote-8bp-${stamp}-${private_ip//./_}"
gdb_cmds="${out_dir}/remote-8bp.gdb"
gdb_log="${out_dir}/remote-8bp.gdb.log"
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
set remote hardware-breakpoint-limit 8
file ${target_bin}
target remote :${local_port}
echo GDB_MARK_CONNECTED\n
break main
info breakpoints
continue
echo GDB_MARK_SOFTWARE_BREAK_MAIN\n
bt
info registers pc sp
x/8i \$pc
p/x smoke_memory_probe[0]
set var smoke_memory_probe[0] = 0xfeedface
p/x smoke_memory_probe[0]
info threads
thread 1
frame 0
next
echo GDB_MARK_NEXT_DONE\n
x/8i \$pc
delete
hbreak smoke_sleep_iters
hbreak smoke_iteration_hook
hbreak smoke_worker_heartbeat
hbreak smoke_worker_main
hbreak __pthread_create
hbreak __pthread_join
hbreak usleep
hbreak exit
info breakpoints
continue
echo GDB_MARK_8_HBREAK_HIT\n
bt
info registers pc sp
info threads
delete
hbreak smoke_iteration_hook
hbreak smoke_worker_heartbeat
info breakpoints
continue
echo GDB_MARK_REUSED_HBREAK_HIT\n
bt
info threads
thread apply all bt
delete
echo GDB_MARK_INTERRUPT_BEGIN\n
continue&
shell sleep 2
interrupt
echo GDB_MARK_INTERRUPT_DONE\n
bt
info registers pc sp
continue
quit
EOF

echo "[remote-8bp] private_ip=${private_ip}"
echo "[remote-8bp] local_port=${local_port}"
echo "[remote-8bp] target_bin=${target_bin}"
echo "[remote-8bp] out_dir=${out_dir}"

ssh -N \
  -L "${local_port}:172.16.0.2:2345" \
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

echo "[remote-8bp] gdb_rc=${gdb_rc}"
echo "[remote-8bp] gdb_log=${gdb_log}"

if [[ "${gdb_rc}" -ne 0 ]]; then
  tail -n 120 "${gdb_log}" >&2 || true
  exit "${gdb_rc}"
fi

for n in 2 3 4 5 6 7 8 9; do
  if ! grep -Eq "Hardware assisted breakpoint ${n} at|^${n}[[:space:]]+hw breakpoint[[:space:]]+keep[[:space:]]+y" "${gdb_log}"; then
    echo "missing evidence for hardware breakpoint ${n}; see ${gdb_log}" >&2
    tail -n 160 "${gdb_log}" >&2 || true
    exit 1
  fi
done

if ! grep -Eq "Breakpoint 1, main|GDB_MARK_SOFTWARE_BREAK_MAIN" "${gdb_log}"; then
  echo "did not hit main with a software breakpoint; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

if ! grep -q "GDB_MARK_8_HBREAK_HIT" "${gdb_log}"; then
  echo "did not hit a breakpoint after inserting eight hardware breakpoints; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

if ! grep -Eq "Breakpoint 10, smoke_iteration_hook|Hardware assisted breakpoint 10, smoke_iteration_hook|Breakpoint 11, smoke_worker_heartbeat|Hardware assisted breakpoint 11, smoke_worker_heartbeat|GDB_MARK_REUSED_HBREAK_HIT" "${gdb_log}"; then
  echo "did not hit a reused hardware breakpoint after delete/reinsert; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

for marker in \
  GDB_MARK_CONNECTED \
  GDB_MARK_NEXT_DONE \
  GDB_MARK_INTERRUPT_BEGIN \
  GDB_MARK_INTERRUPT_DONE; do
  if ! grep -q "${marker}" "${gdb_log}"; then
    echo "missing ${marker}; see ${gdb_log}" >&2
    tail -n 160 "${gdb_log}" >&2 || true
    exit 1
  fi
done

if ! grep -q "0xfeedface" "${gdb_log}"; then
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
  echo "inferior did not exit normally; see ${gdb_log}" >&2
  tail -n 160 "${gdb_log}" >&2 || true
  exit 1
fi

echo "[remote-8bp] PASS"
