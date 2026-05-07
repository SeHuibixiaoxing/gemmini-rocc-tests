#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_stack_sample.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Run a non-interactive sampling GDB session against the cfg32 NIC pipeline-runtime
gdbserver workload. The target workload must already have printed its
[gdbserver] phase=listening announcement.

This helper deliberately does not probe the target port with nc/telnet. The
first TCP connection to gdbserver --once is the GDB target remote connection.

Environment:
  RISCV_GDB              Cross GDB path.
  EXPECT                 expect executable path.
  PRT_GDB_TARGET_BIN     Host-side RISC-V ELF with symbols.
  PRT_GDB_SAMPLE_COUNT   Number of continue/Ctrl-C stack samples. Default: 3.
  PRT_GDB_SAMPLE_SECONDS Seconds to run before each Ctrl-C. Default: 60.
  PRT_GDB_EXPECT_TIMEOUT Default expect timeout in seconds. Default: 300.
  PRT_GDB_OUT_ROOT       Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV        Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC
                         If non-empty, install a permanent run-host neighbor
                         entry for the guest before opening the SSH tunnel.
EOF
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"

run_host_ip="$1"
guest_endpoint="$2"
local_port="${3:-32345}"

guest_ip="${guest_endpoint%:*}"
guest_port="2345"
if [[ "${guest_endpoint}" == *:* ]]; then
  guest_port="${guest_endpoint##*:}"
fi

gdb="${RISCV_GDB:-${cy_dir}/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb}"
expect_bin="${EXPECT:-$(command -v expect || true)}"
target_bin="${PRT_GDB_TARGET_BIN:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux}"
ssh_key="${FIRESIM_SSH_KEY:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
sample_count="${PRT_GDB_SAMPLE_COUNT:-3}"
sample_seconds="${PRT_GDB_SAMPLE_SECONDS:-60}"
expect_timeout="${PRT_GDB_EXPECT_TIMEOUT:-300}"
out_root="${PRT_GDB_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
tap_dev="${PRT_GDB_TAP_DEV:-tap0}"
static_neigh_mac="${PRT_GDB_STATIC_NEIGH_MAC:-}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-stack-sample-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
expect_script="${out_dir}/pairdummy-cfg32-stack-sample.expect"
transcript="${out_dir}/pairdummy-cfg32-stack-sample.expect.log"

mkdir -p "${out_dir}"

if [[ ! "${sample_count}" =~ ^[0-9]+$ || "${sample_count}" -lt 1 ]]; then
  echo "invalid PRT_GDB_SAMPLE_COUNT=${sample_count}" >&2
  exit 2
fi
if [[ ! "${sample_seconds}" =~ ^[0-9]+$ || "${sample_seconds}" -lt 1 ]]; then
  echo "invalid PRT_GDB_SAMPLE_SECONDS=${sample_seconds}" >&2
  exit 2
fi

if [[ ! -x "${gdb}" ]]; then
  echo "missing executable cross-gdb: ${gdb}" >&2
  exit 1
fi
if [[ -z "${expect_bin}" || ! -x "${expect_bin}" ]]; then
  echo "missing expect; set EXPECT=/path/to/expect" >&2
  exit 1
fi
if [[ ! -f "${target_bin}" ]]; then
  echo "missing target ELF: ${target_bin}" >&2
  exit 1
fi
if [[ ! -f "${ssh_key}" ]]; then
  echo "missing FireSim SSH key: ${ssh_key}" >&2
  exit 1
fi

ssh_base=(
  ssh
  -i "${ssh_key}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o ConnectTimeout=10
  "${ssh_user}@${run_host_ip}"
)

if command -v readelf >/dev/null 2>&1; then
  if ! readelf -S "${target_bin}" | grep -q '\.debug_info'; then
    echo "warning: target ELF has no .debug_info: ${target_bin}" >&2
  fi
fi

if pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >/dev/null 2>&1; then
  echo "refusing duplicate tunnel on local port ${local_port} to ${guest_ip}:${guest_port}" >&2
  pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >&2 || true
  exit 1
fi

if [[ -n "${static_neigh_mac}" ]]; then
  echo "[pairdummy-gdb-stack] static_neigh=${guest_ip} ${static_neigh_mac} dev ${tap_dev}"
  "${ssh_base[@]}" \
    "sudo ip neigh replace '${guest_ip}' lladdr '${static_neigh_mac}' dev '${tap_dev}' nud permanent && ip neigh show dev '${tap_dev}'" \
    >"${out_dir}/static-neigh.log" 2>&1
fi

ssh -N \
  -L "${local_port}:${guest_ip}:${guest_port}" \
  -i "${ssh_key}" \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  -o ServerAliveInterval=30 \
  -o ServerAliveCountMax=3 \
  "${ssh_user}@${run_host_ip}" >"${tunnel_log}" 2>&1 &
tunnel_pid=$!

cleanup() {
  if kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
    kill "${tunnel_pid}" >/dev/null 2>&1 || true
    wait "${tunnel_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

for _ in $(seq 1 50); do
  if ss -ltn "sport = :${local_port}" | grep -q ":${local_port}"; then
    break
  fi
  if ! kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
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
set timeout [lindex $argv 5]

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 3]
set sample_count [lindex $argv 4]
set sample_seconds [lindex $argv 6]

log_file -noappend $transcript
spawn $gdb -q $elf

proc need_prompt {} {
    expect {
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for gdb prompt"; exit 3 }
        eof { puts stderr "gdb exited unexpectedly"; exit 4 }
    }
}

proc gdb_cmd {cmd {timeout_s 300}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "$cmd\r"
    need_prompt
    set ::timeout $old_timeout
}

proc target_remote {port {timeout_s 300}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "target remote :$port\r"
    expect {
        -re "Remote communication error|Target disconnected|Connection reset by peer" {
            puts stderr "target remote failed"
            exit 7
        }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for target remote"; exit 8 }
        eof { puts stderr "gdb exited during target remote"; exit 9 }
    }
    set ::timeout $old_timeout
}

proc continue_then_interrupt {sample_index sample_seconds} {
    set old_timeout $::timeout
    set wait_ms [expr {$sample_seconds * 1000}]
    set ::timeout [expr {$sample_seconds + 180}]
    puts "GDB_STACK_SAMPLE_${sample_index}_CONTINUE"
    send -- "continue\r"
    after $wait_ms
    puts "GDB_STACK_SAMPLE_${sample_index}_INTERRUPT"
    send \003
    expect {
        -re "received signal SIGINT|Program received signal SIGINT|Thread .* received signal SIGINT|Program received signal SIGTRAP|Thread .* received signal SIGTRAP" {
            need_prompt
        }
        -re "exited normally|exited with code" {
            puts "GDB_STACK_SAMPLE_${sample_index}_INFERIOR_EXITED"
            need_prompt
            set ::timeout $old_timeout
            return 1
        }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for Ctrl-C/SIGINT stop"; exit 13 }
        eof { puts stderr "gdb exited during Ctrl-C sample"; exit 14 }
    }
    set ::timeout $old_timeout
    puts "GDB_STACK_SAMPLE_${sample_index}_STOPPED"
    return 0
}

need_prompt
gdb_cmd "set pagination off"
gdb_cmd "set confirm off"
gdb_cmd "set print thread-events off"
gdb_cmd "set breakpoint pending off"
gdb_cmd "set debuginfod enabled off"
gdb_cmd "set auto-load safe-path /"
gdb_cmd "set remotetimeout 180"
target_remote $port 300
puts "GDB_STACK_MARK_CONNECTED"

gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 300
gdb_cmd "info registers pc sp ra a0 a1 a2 a3"
gdb_cmd "x/16i \$pc"

for {set i 1} {$i <= $sample_count} {incr i} {
    set exited [continue_then_interrupt $i $sample_seconds]
    if {$exited} {
        puts "GDB_STACK_MARK_EXITED"
        send -- "quit\r"
        expect eof
        exit 0
    }
    gdb_cmd "info threads"
    gdb_cmd "thread apply all bt" 300
    gdb_cmd "info registers pc sp ra a0 a1 a2 a3"
    gdb_cmd "x/16i \$pc"
}

set timeout 180
send -- "detach\r"
expect {
    -re "Ending remote debugging|Inferior .* detached|Detaching from program" { need_prompt }
    timeout { puts stderr "timeout waiting for detach"; exit 15 }
    eof { puts stderr "gdb exited during detach"; exit 16 }
}
puts "GDB_STACK_MARK_DETACH_OK"

send -- "quit\r"
expect eof
exit 0
EOF

echo "[pairdummy-gdb-stack] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-stack] target_bin=${target_bin}"
echo "[pairdummy-gdb-stack] sample_count=${sample_count} sample_seconds=${sample_seconds}"
echo "[pairdummy-gdb-stack] out_dir=${out_dir}"

set +e
"${expect_bin}" -f "${expect_script}" "${gdb}" "${target_bin}" "${local_port}" "${transcript}" "${sample_count}" "${expect_timeout}" "${sample_seconds}" \
  >"${out_dir}/expect-driver.stdout" 2>"${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[pairdummy-gdb-stack] expect_rc=${expect_rc}"
echo "[pairdummy-gdb-stack] transcript=${transcript}"

if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 260 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

if ! grep -q "GDB_STACK_MARK_CONNECTED" "${transcript}" && \
    ! grep -q "GDB_STACK_MARK_CONNECTED" "${out_dir}/expect-driver.stdout"; then
  echo "missing GDB_STACK_MARK_CONNECTED; see ${transcript}" >&2
  exit 1
fi

if ! grep -q "GDB_STACK_SAMPLE_1_STOPPED" "${transcript}" && \
    ! grep -q "GDB_STACK_SAMPLE_1_STOPPED" "${out_dir}/expect-driver.stdout" && \
    ! grep -q "GDB_STACK_SAMPLE_1_INFERIOR_EXITED" "${transcript}" && \
    ! grep -q "GDB_STACK_SAMPLE_1_INFERIOR_EXITED" "${out_dir}/expect-driver.stdout"; then
  echo "missing first stack sample evidence; see ${transcript} and ${out_dir}/expect-driver.stdout" >&2
  tail -n 260 "${transcript}" >&2 || true
  tail -n 260 "${out_dir}/expect-driver.stdout" >&2 || true
  exit 1
fi

echo "[pairdummy-gdb-stack] PASS"
