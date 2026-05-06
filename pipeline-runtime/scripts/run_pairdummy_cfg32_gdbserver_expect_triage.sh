#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_expect_triage.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Run a non-interactive first remote-GDB triage against the cfg32 NIC
pipeline-runtime gdbserver workload. The target workload must already have
printed its [gdbserver] phase=listening announcement.

This helper deliberately does not probe the target port with nc/telnet. The
first TCP connection to gdbserver --once is the GDB target remote connection.

Environment:
  RISCV_GDB             Cross GDB path.
  EXPECT                expect executable path.
  PRT_GDB_TARGET_BIN    Host-side RISC-V ELF with symbols.
  PRT_GDB_EXPECT_TIMEOUT Default expect timeout in seconds. Default: 240.
  PRT_GDB_OUT_ROOT      Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
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
expect_timeout="${PRT_GDB_EXPECT_TIMEOUT:-240}"
out_root="${PRT_GDB_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-expect-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
expect_script="${out_dir}/pairdummy-cfg32-triage.expect"
transcript="${out_dir}/pairdummy-cfg32-triage.expect.log"

mkdir -p "${out_dir}"

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
set timeout [lindex $argv 4]

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 3]

log_file -noappend $transcript
spawn $gdb -q $elf

proc need_prompt {} {
    expect {
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for gdb prompt"; exit 3 }
        eof { puts stderr "gdb exited unexpectedly"; exit 4 }
    }
}

proc gdb_cmd {cmd {timeout_s 240}} {
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

proc continue_to_break {timeout_s} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "continue\r"
    expect {
        -re "Breakpoint \[0-9\]+, .*" { need_prompt }
        -re "Program received signal SIGTRAP|Thread .* received signal SIGTRAP" { need_prompt }
        -re "exited normally|exited with code" {
            puts stderr "inferior exited before first triage breakpoint"
            exit 10
        }
        timeout { puts stderr "timeout waiting for first triage breakpoint"; exit 11 }
        eof { puts stderr "gdb exited during continue"; exit 12 }
    }
    set ::timeout $old_timeout
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
puts "GDB_MARK_CONNECTED"

gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 240
gdb_cmd "info registers"
gdb_cmd "x/16i \$pc"

foreach sym {
    prt_runtime_run
    prt_action_bind_topology
    stage_prepare_exec_views
    prt_dma_submit
    prt_dma_wait
    dma_blocking_wait
    prt_gemmini_spm_xlate_program
    prt_gemmini_spm_xlate_flush
    prt_rr_release_scope
    prt_gemm_conv_run
    prt_gemm_fence
} {
    gdb_cmd "break $sym"
}
gdb_cmd "info breakpoints"

continue_to_break 600
puts "GDB_MARK_HIT_FIRST_BREAK"
gdb_cmd "bt"
gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 300
gdb_cmd "info registers pc sp ra a0 a1 a2 a3"
gdb_cmd "x/16i \$pc"

gdb_cmd "disable"
puts "GDB_MARK_INTERRUPT_BEGIN"
set old_timeout $timeout
set timeout 90
send -- "continue\r"
after 2000
send \003
expect {
    -re "received signal SIGINT|Program received signal SIGINT|Thread .* received signal SIGINT|Program received signal SIGTRAP|Thread .* received signal SIGTRAP" {
        need_prompt
    }
    -re "\\(gdb\\) $" {}
    -re "exited normally|exited with code" {
        puts "GDB_MARK_INFERIOR_EXITED_DURING_INTERRUPT"
        need_prompt
    }
    timeout { puts stderr "timeout waiting for Ctrl-C/SIGINT stop"; exit 13 }
    eof { puts stderr "gdb exited during Ctrl-C test"; exit 14 }
}
set timeout $old_timeout
puts "GDB_MARK_INTERRUPT_DONE"

gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 300
gdb_cmd "info registers pc sp ra"
gdb_cmd "x/8i \$pc"

set timeout 180
send -- "detach\r"
expect {
    -re "Ending remote debugging|Inferior .* detached|Detaching from program" { need_prompt }
    timeout { puts stderr "timeout waiting for detach"; exit 15 }
    eof { puts stderr "gdb exited during detach"; exit 16 }
}
puts "GDB_MARK_DETACH_OK"

send -- "quit\r"
expect eof
exit 0
EOF

echo "[pairdummy-gdb-expect] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-expect] target_bin=${target_bin}"
echo "[pairdummy-gdb-expect] out_dir=${out_dir}"

set +e
"${expect_bin}" -f "${expect_script}" "${gdb}" "${target_bin}" "${local_port}" "${transcript}" "${expect_timeout}" \
  >"${out_dir}/expect-driver.stdout" 2>"${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[pairdummy-gdb-expect] expect_rc=${expect_rc}"
echo "[pairdummy-gdb-expect] transcript=${transcript}"

if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 220 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

expect_stdout="${out_dir}/expect-driver.stdout"
for marker in \
  GDB_MARK_CONNECTED \
  GDB_MARK_HIT_FIRST_BREAK \
  GDB_MARK_INTERRUPT_BEGIN \
  GDB_MARK_INTERRUPT_DONE \
  GDB_MARK_DETACH_OK; do
  if ! grep -q "${marker}" "${transcript}" && ! grep -q "${marker}" "${expect_stdout}"; then
    echo "missing ${marker}; see ${transcript} and ${expect_stdout}" >&2
    tail -n 220 "${transcript}" >&2 || true
    tail -n 80 "${expect_stdout}" >&2 || true
    exit 1
  fi
done

if ! grep -Eq "Breakpoint [0-9]+, .*prt_|Breakpoint [0-9]+, .*stage_prepare_exec_views|Breakpoint [0-9]+, .*dma_blocking_wait" "${transcript}"; then
  echo "missing first breakpoint evidence; see ${transcript}" >&2
  tail -n 220 "${transcript}" >&2 || true
  exit 1
fi

if ! grep -Eq "Thread .*|thread apply all bt" "${transcript}" && \
    ! grep -Eq "Thread .*|thread apply all bt" "${expect_stdout}"; then
  echo "thread inspection evidence missing; see ${transcript}" >&2
  tail -n 220 "${transcript}" >&2 || true
  tail -n 80 "${expect_stdout}" >&2 || true
  exit 1
fi

echo "[pairdummy-gdb-expect] PASS"
