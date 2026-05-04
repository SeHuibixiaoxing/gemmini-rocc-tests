#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_remote_gdbserver_software_expect_smoke.sh <run-host-private-ip> [local-port]

Run the software-breakpoint-only remote gdbserver regression with an interactive
GDB session driven by expect. The target workload must already have reached the
guest gdbserver listener at 172.16.0.2:2345.

This test deliberately does not use hbreak/watch. It also does not probe the
guest TCP port with nc/telnet; the first TCP connection is the GDB remote
connection.

Optional environment:
  GDBSERVER_SMOKE_GUEST_IP            Guest-side gdbserver IPv4
                                      (default: 172.16.0.2)
  GDBSERVER_SMOKE_TAP_DEV             Run-host tap device (default: tap0)
  GDBSERVER_SMOKE_STATIC_NEIGH_MAC    If non-empty, install this static
                                      run-host neighbor entry before opening
                                      the tunnel (default: disabled)
  GDBSERVER_SMOKE_ADVERTISE_HOST_ARP  If set to 1, send host ARP
                                      advertisements before opening the tunnel
                                      (default: 0)
  GDBSERVER_SMOKE_HOST_MAC            Run-host tap MAC advertised to the guest
                                      (default: 8e:6b:35:04:00:00)
  GDBSERVER_SMOKE_EXPECT_TIMEOUT      Default expect timeout in seconds
                                      (default: 180)
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
expect_bin="${EXPECT:-$(command -v expect || true)}"
target_bin="${GDBSERVER_SMOKE_HOST_BIN:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/overlay/root/gdbserver-smoke/gdbserver-smoke}"
guest_ip="${GDBSERVER_SMOKE_GUEST_IP:-172.16.0.2}"
host_ip="${GDBSERVER_SMOKE_HOST_IP:-172.16.0.1}"
tap_dev="${GDBSERVER_SMOKE_TAP_DEV:-tap0}"
static_neigh_mac="${GDBSERVER_SMOKE_STATIC_NEIGH_MAC:-}"
advertise_host_arp="${GDBSERVER_SMOKE_ADVERTISE_HOST_ARP:-0}"
host_mac="${GDBSERVER_SMOKE_HOST_MAC:-8e:6b:35:04:00:00}"
expect_timeout="${GDBSERVER_SMOKE_EXPECT_TIMEOUT:-180}"
out_root="${GDBSERVER_SMOKE_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/remote-swbreak-expect-${stamp}-${private_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
expect_script="${out_dir}/remote-swbreak.expect"
transcript="${out_dir}/remote-swbreak.expect.log"

mkdir -p "${out_dir}"

if [[ ! -x "${gdb}" ]]; then
  echo "missing gdb: ${gdb}" >&2
  exit 1
fi
if [[ -z "${expect_bin}" || ! -x "${expect_bin}" ]]; then
  echo "missing expect; set EXPECT=/path/to/expect" >&2
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

echo "[remote-swbreak-expect] private_ip=${private_ip}"
echo "[remote-swbreak-expect] local_port=${local_port}"
echo "[remote-swbreak-expect] guest_ip=${guest_ip}"
echo "[remote-swbreak-expect] target_bin=${target_bin}"
echo "[remote-swbreak-expect] out_dir=${out_dir}"

ssh_base=(
  ssh
  -i "${ssh_key_path}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o ConnectTimeout=10
  "${ssh_user}@${private_ip}"
)

if [[ -n "${static_neigh_mac}" ]]; then
  echo "[remote-swbreak-expect] static_neigh=${guest_ip} ${static_neigh_mac} dev ${tap_dev}"
  "${ssh_base[@]}" \
    "sudo ip neigh replace '${guest_ip}' lladdr '${static_neigh_mac}' dev '${tap_dev}' nud permanent && ip neigh show dev '${tap_dev}'" \
    >"${out_dir}/static-neigh.log" 2>&1
fi

if [[ "${advertise_host_arp}" == "1" ]]; then
  if [[ -z "${static_neigh_mac}" ]]; then
    echo "GDBSERVER_SMOKE_ADVERTISE_HOST_ARP=1 requires GDBSERVER_SMOKE_STATIC_NEIGH_MAC" >&2
    exit 1
  fi
  echo "[remote-swbreak-expect] advertise_host_arp=${host_ip} ${host_mac} -> ${guest_ip} ${static_neigh_mac}"
  "${ssh_base[@]}" "sudo python3 -" >"${out_dir}/host-arp-advertise.log" 2>&1 <<PY
import socket
import struct
import time

iface = "${tap_dev}"
host_mac = bytes.fromhex("${host_mac//:/}")
target_mac = bytes.fromhex("${static_neigh_mac//:/}")
host_ip = socket.inet_aton("${host_ip}")
target_ip = socket.inet_aton("${guest_ip}")
eth_type = b"\\x08\\x06"

arp_reply = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 2,
                        host_mac, host_ip, target_mac, target_ip)
unicast = target_mac + host_mac + eth_type + arp_reply

arp_announce = struct.pack("!HHBBH6s4s6s4s", 1, 0x0800, 6, 4, 1,
                           host_mac, host_ip, b"\\x00" * 6, host_ip)
broadcast = b"\\xff" * 6 + host_mac + eth_type + arp_announce

s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
s.bind((iface, 0))
for _ in range(16):
    s.send(unicast)
    s.send(broadcast)
    time.sleep(0.05)
print("sent host ARP advertisements on", iface)
PY
else
  echo "[remote-swbreak-expect] advertise_host_arp=disabled" >"${out_dir}/host-arp-advertise.log"
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

cat >"${expect_script}" <<'EOF'
#!/usr/bin/env expect
set timeout [lindex $argv 3]

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 4]

log_file -noappend $transcript
spawn $gdb $elf

proc need_prompt {} {
    expect {
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for gdb prompt"; exit 3 }
        eof { puts stderr "gdb exited unexpectedly"; exit 4 }
    }
}

proc gdb_cmd {cmd {timeout_s 180}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "$cmd\r"
    need_prompt
    set ::timeout $old_timeout
}

proc gdb_target_remote {port {timeout_s 240}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "target remote :$port\r"
    expect {
        -re "Remote communication error|Target disconnected|Connection reset by peer" {
            puts stderr "target remote failed: remote communication error"
            exit 7
        }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for target remote stop"; exit 8 }
        eof { puts stderr "gdb exited during target remote"; exit 9 }
    }
    set ::timeout $old_timeout
}

proc gdb_continue_to {pattern {timeout_s 180}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "continue\r"
    expect {
        -re $pattern { need_prompt }
        timeout { puts stderr "timeout waiting for continue pattern: $pattern"; exit 5 }
        eof { puts stderr "gdb exited during continue"; exit 6 }
    }
    set ::timeout $old_timeout
}

need_prompt
gdb_cmd "set pagination off"
gdb_cmd "set confirm off"
gdb_cmd "set print thread-events off"
gdb_cmd "set debuginfod enabled off"
gdb_cmd "set auto-load safe-path /"
gdb_cmd "set remotetimeout 180"
gdb_target_remote $port 240
puts "GDB_MARK_CONNECTED"
gdb_cmd "break main"
gdb_cmd "break smoke_iteration_hook"
gdb_cmd "break smoke_worker_heartbeat"
gdb_cmd "info breakpoints"

gdb_continue_to "Breakpoint 1,.*main" 240
puts "GDB_MARK_HIT_MAIN"
gdb_cmd "bt"
gdb_cmd "info registers pc sp ra a0 a1"
gdb_cmd "x/12i \$pc"
gdb_cmd "print smoke_counter"
gdb_cmd "print/x smoke_memory_probe\[0\]"
gdb_cmd "set variable smoke_memory_probe\[7\] = 0xabcdef"
gdb_cmd "print/x smoke_memory_probe\[7\]"
gdb_cmd "next" 180
puts "GDB_MARK_NEXT_DONE"
gdb_cmd "disable 1"

gdb_continue_to "Breakpoint (2|3),.*smoke_(iteration_hook|worker_heartbeat)" 240
puts "GDB_MARK_HIT_MULTI_SOFTWARE_BREAK"
gdb_cmd "bt"
gdb_cmd "info args"
gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 240
gdb_cmd "delete breakpoints"

gdb_cmd "break smoke_iteration_hook if iteration >= 2"
gdb_continue_to "Breakpoint .*,.*smoke_iteration_hook" 240
puts "GDB_MARK_HIT_CONDITIONAL_BREAK"
gdb_cmd "bt"
gdb_cmd "info args"
gdb_cmd "info registers pc sp"
gdb_cmd "delete breakpoints"

puts "GDB_MARK_INTERRUPT_BEGIN"
set old_timeout $timeout
set timeout 60
send -- "continue\r"
after 2000
send \003
expect {
    -re "received signal SIGINT|Program received signal SIGINT|Thread .* received signal SIGINT|Program received signal SIGTRAP|Thread .* received signal SIGTRAP" { need_prompt }
    timeout { puts stderr "timeout waiting for Ctrl-C/SIGINT stop"; exit 7 }
    eof { puts stderr "gdb exited during Ctrl-C test"; exit 8 }
}
set timeout $old_timeout
puts "GDB_MARK_INTERRUPT_DONE"

gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 240
gdb_cmd "info registers pc sp ra"
gdb_cmd "x/8i \$pc"

set timeout 240
send -- "signal 0\r"
expect {
    -re "exited normally|Inferior .* exited normally|exited with code 0" { need_prompt }
    -re "\\(gdb\\) $" {}
    timeout { puts stderr "timeout waiting for inferior clean exit after signal 0"; exit 9 }
    eof {}
}
puts "GDB_MARK_EXIT_OK"

send -- "quit\r"
expect eof
exit 0
EOF

set +e
"${expect_bin}" -f "${expect_script}" "${gdb}" "${target_bin}" "${local_port}" "${expect_timeout}" "${transcript}" \
  >"${out_dir}/expect-driver.stdout" 2>"${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[remote-swbreak-expect] expect_rc=${expect_rc}"
echo "[remote-swbreak-expect] transcript=${transcript}"

if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 220 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

for marker in \
  GDB_MARK_CONNECTED \
  GDB_MARK_HIT_MAIN \
  GDB_MARK_NEXT_DONE \
  GDB_MARK_HIT_MULTI_SOFTWARE_BREAK \
  GDB_MARK_HIT_CONDITIONAL_BREAK \
  GDB_MARK_INTERRUPT_BEGIN \
  GDB_MARK_INTERRUPT_DONE \
  GDB_MARK_EXIT_OK; do
  if ! grep -q "${marker}" "${transcript}"; then
    echo "missing ${marker}; see ${transcript}" >&2
    tail -n 220 "${transcript}" >&2 || true
    exit 1
  fi
done

if ! grep -q "0xabcdef" "${transcript}"; then
  echo "memory write/read evidence missing; see ${transcript}" >&2
  tail -n 220 "${transcript}" >&2 || true
  exit 1
fi

if ! grep -Eq "Thread .*|thread apply all bt" "${transcript}"; then
  echo "thread inspection evidence missing; see ${transcript}" >&2
  tail -n 220 "${transcript}" >&2 || true
  exit 1
fi

echo "[remote-swbreak-expect] PASS"
