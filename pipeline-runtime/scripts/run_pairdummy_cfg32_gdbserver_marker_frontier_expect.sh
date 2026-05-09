#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_marker_frontier_expect.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach cross-GDB to a pipeline-runtime gdbserver --once run, wait for
prt_gdb_marker_stop(), and optionally arm a post-marker frontier. This helper
uses expect/PTY Ctrl-C for bounded continue windows. It never probes the guest
TCP port with nc/telnet/curl; the first TCP connection to gdbserver is GDB.

Environment:
  RISCV_GDB                    Cross GDB path.
  EXPECT                       expect executable path.
  PRT_GDB_TARGET_BIN           Host-side RISC-V ELF with symbols.
  PRT_GDB_OUT_ROOT             Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV              Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC     Optional static neighbor MAC for the guest.
  PRT_GDB_MARKER_TIMEOUT       Seconds to wait for prt_gdb_marker_stop. Default: 300.
  PRT_GDB_FRONTIER_TIMEOUT     Seconds to wait after arming frontier commands. Default: 120.
  PRT_GDB_FRONTIER_GDB_CMDS    Optional GDB commands to arm the frontier.
                                Backslash escapes are expanded, so use \n for
                                multiple lines. These commands must return to a
                                prompt; do not put continue here.
  PRT_GDB_FRONTIER_GDB_FILE    Optional GDB command file to arm the frontier.
                                The file must return to a prompt.
  PRT_GDB_MARKER_DELETE_AFTER_HIT
                                Delete marker breakpoint after hit. Default: 1.
  PRT_GDB_DETACH               Detach before quitting. Default: 1.
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
out_root="${PRT_GDB_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
tap_dev="${PRT_GDB_TAP_DEV:-tap0}"
static_neigh_mac="${PRT_GDB_STATIC_NEIGH_MAC:-}"
marker_timeout="${PRT_GDB_MARKER_TIMEOUT:-300}"
frontier_timeout="${PRT_GDB_FRONTIER_TIMEOUT:-120}"
frontier_gdb_cmds="${PRT_GDB_FRONTIER_GDB_CMDS:-}"
frontier_gdb_file="${PRT_GDB_FRONTIER_GDB_FILE:-}"
marker_delete_after_hit="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-1}"
detach_after="${PRT_GDB_DETACH:-1}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-marker-frontier-expect-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
expect_script="${out_dir}/pairdummy-cfg32-marker-frontier.expect"
frontier_cmds_file="${out_dir}/frontier.gdb"
transcript="${out_dir}/pairdummy-cfg32-marker-frontier.expect.log"

mkdir -p "${out_dir}"

for pair in \
  "PRT_GDB_MARKER_TIMEOUT:${marker_timeout}" \
  "PRT_GDB_FRONTIER_TIMEOUT:${frontier_timeout}"; do
  name="${pair%%:*}"
  value="${pair#*:}"
  if [[ ! "${value}" =~ ^[0-9]+$ || "${value}" -lt 1 ]]; then
    echo "invalid ${name}=${value}" >&2
    exit 2
  fi
done

case "${marker_delete_after_hit}" in
  0|1) ;;
  *) echo "invalid PRT_GDB_MARKER_DELETE_AFTER_HIT=${marker_delete_after_hit}; expected 0 or 1" >&2; exit 2 ;;
esac
case "${detach_after}" in
  0|1) ;;
  *) echo "invalid PRT_GDB_DETACH=${detach_after}; expected 0 or 1" >&2; exit 2 ;;
esac

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
if [[ -n "${frontier_gdb_file}" && ! -f "${frontier_gdb_file}" ]]; then
  echo "missing PRT_GDB_FRONTIER_GDB_FILE: ${frontier_gdb_file}" >&2
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

if pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >/dev/null 2>&1; then
  echo "refusing duplicate tunnel on local port ${local_port} to ${guest_ip}:${guest_port}" >&2
  pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >&2 || true
  exit 1
fi

if [[ -n "${static_neigh_mac}" ]]; then
  "${ssh_base[@]}" \
    "sudo ip neigh replace '${guest_ip}' lladdr '${static_neigh_mac}' dev '${tap_dev}' nud permanent && ip neigh show dev '${tap_dev}'" \
    >"${out_dir}/static-neigh.log" 2>&1
fi

{
  if [[ -n "${frontier_gdb_cmds}" ]]; then
    printf '%b\n' "${frontier_gdb_cmds}"
  fi
  if [[ -n "${frontier_gdb_file}" ]]; then
    printf 'source %s\n' "${frontier_gdb_file}"
  fi
} >"${frontier_cmds_file}"

has_frontier=0
if [[ -s "${frontier_cmds_file}" ]]; then
  has_frontier=1
fi

cleanup() {
  if [[ -n "${tunnel_pid:-}" ]] && kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
    kill "${tunnel_pid}" >/dev/null 2>&1 || true
    wait "${tunnel_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

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
set timeout [lindex $argv 7]

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 3]
set marker_timeout [lindex $argv 4]
set frontier_timeout [lindex $argv 5]
set frontier_cmds_file [lindex $argv 6]
set has_frontier [lindex $argv 8]
set marker_delete_after_hit [lindex $argv 9]
set detach_after [lindex $argv 10]

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

proc wait_for_stop_after_ctrl_c {label} {
    expect {
        -re "received signal SIGINT|Program received signal SIGINT|Thread .* received signal SIGINT|Program received signal SIGTRAP|Thread .* received signal SIGTRAP" {
            need_prompt
            return 0
        }
        -re "\\(gdb\\) $" {
            return 0
        }
        -re "Remote connection closed|Target disconnected|Disconnected from target|Connection reset by peer" {
            puts stderr "${label}: target disconnected during Ctrl-C"
            exit 18
        }
        timeout {
            puts stderr "${label}: timeout waiting for Ctrl-C stop"
            exit 13
        }
        eof {
            puts stderr "${label}: gdb exited during Ctrl-C"
            exit 14
        }
    }
}

proc dump_state {label} {
    gdb_cmd "printf \"\\n--- $label: threads ---\\n\"" 120
    gdb_cmd "info threads" 300
    gdb_cmd "printf \"\\n--- $label: all bt full ---\\n\"" 120
    gdb_cmd "thread apply all bt full" 600
    gdb_cmd "printf \"\\n--- $label: registers ---\\n\"" 120
    gdb_cmd "info registers pc sp ra a0 a1 a2 a3 a4 a5 a6 a7" 300
    gdb_cmd "printf \"\\n--- $label: pc window ---\\n\"" 120
    gdb_cmd "x/32i \$pc-64" 300
    gdb_cmd "printf \"\\n--- $label: marker/debug state ---\\n\"" 120
    gdb_cmd "print g_prt_gdb_marker_state" 300
    gdb_cmd "print g_prt_debug_state" 300
    gdb_cmd "print g_prt_debug_tls_state" 300
}

proc maybe_detach {detach_after} {
    if {$detach_after != 1} {
        return
    }
    send -- "detach\r"
    expect {
        -re "Ending remote debugging|Inferior .* detached|Detaching from program" { need_prompt }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for detach"; exit 15 }
        eof { puts stderr "gdb exited during detach"; exit 16 }
    }
    puts "GDB_MARK_DETACH_OK"
}

proc continue_to_marker {marker_timeout} {
    set old_timeout $::timeout
    set ::timeout $marker_timeout
    puts "GDB_MARKER_CONTINUE"
    send -- "continue\r"
    expect {
        -re "Breakpoint \[0-9\]+, .*prt_gdb_marker_stop" {
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "prt_gdb_marker_stop" {
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "exited normally|exited with code" {
            puts stderr "inferior exited before marker"
            need_prompt
            exit 10
        }
        -re "Remote connection closed|Target disconnected|Disconnected from target|Connection reset by peer" {
            puts stderr "target disconnected before marker"
            exit 12
        }
        timeout {
            set ::timeout [expr {$old_timeout > 120 ? $old_timeout : 120}]
            puts "GDB_MARKER_TIMEOUT_SEND_CTRL_C"
            send \003
            wait_for_stop_after_ctrl_c "marker-timeout"
            puts "GDB_MARKER_TIMEOUT_STOPPED"
            set ::timeout $old_timeout
            return 1
        }
        eof { puts stderr "gdb exited before marker"; exit 12 }
    }
}

proc continue_frontier {frontier_timeout} {
    set old_timeout $::timeout
    set ::timeout $frontier_timeout
    puts "GDB_FRONTIER_CONTINUE"
    send -- "continue\r"
    expect {
        -re "Breakpoint \[0-9\]+, .*|Temporary breakpoint \[0-9\]+, .*|Program received signal SIGTRAP|Thread .* received signal SIGTRAP" {
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "exited normally|exited with code" {
            puts "GDB_FRONTIER_INFERIOR_EXITED"
            need_prompt
            set ::timeout $old_timeout
            return 2
        }
        -re "Remote connection closed|Target disconnected|Disconnected from target|Connection reset by peer" {
            puts stderr "target disconnected during frontier continue"
            exit 17
        }
        timeout {
            set ::timeout [expr {$old_timeout > 120 ? $old_timeout : 120}]
            puts "GDB_FRONTIER_TIMEOUT_SEND_CTRL_C"
            send \003
            wait_for_stop_after_ctrl_c "frontier-timeout"
            puts "GDB_FRONTIER_TIMEOUT_STOPPED"
            set ::timeout $old_timeout
            return 1
        }
        eof { puts stderr "gdb exited during frontier continue"; exit 17 }
    }
}

need_prompt
gdb_cmd "set pagination off"
gdb_cmd "set confirm off"
gdb_cmd "set print pretty on"
gdb_cmd "set print thread-events off"
gdb_cmd "set breakpoint pending off"
gdb_cmd "set debuginfod enabled off"
gdb_cmd "set auto-load safe-path /"
gdb_cmd "set remotetimeout 180"
target_remote $port 300
puts "GDB_MARK_CONNECTED"

gdb_cmd "break prt_gdb_marker_stop" 120
set marker_outcome [continue_to_marker $marker_timeout]

if {$marker_outcome == 1} {
    puts "GDB_MARKER_TIMEOUT_DIAGNOSTIC_BEGIN"
    dump_state "initial marker timeout"
    maybe_detach $detach_after
    send -- "quit\r"
    expect eof
    exit 20
}

puts "GDB_MARKER_HIT"
gdb_cmd "printf \"\\n--- marker state ---\\n\"" 120
gdb_cmd "print g_prt_gdb_marker_state" 300
gdb_cmd "bt 16" 300
gdb_cmd "thread apply all bt" 300

if {$marker_delete_after_hit == 1} {
    gdb_cmd "delete 1" 120
}

if {$has_frontier == 1} {
    gdb_cmd "printf \"\\n--- arming frontier commands ---\\n\"" 120
    gdb_cmd "source $frontier_cmds_file" 300
    set frontier_outcome [continue_frontier $frontier_timeout]
    if {$frontier_outcome == 0} {
        puts "GDB_FRONTIER_STOPPED"
        dump_state "frontier stop"
    } elseif {$frontier_outcome == 1} {
        puts "GDB_FRONTIER_TIMEOUT_DIAGNOSTIC_BEGIN"
        dump_state "frontier timeout"
    }
}

maybe_detach $detach_after
send -- "quit\r"
expect eof
exit 0
EOF

target_sha="$(sha256sum "${target_bin}" | awk '{print $1}')"
echo "[pairdummy-gdb-marker-frontier] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-marker-frontier] target_bin=${target_bin}"
echo "[pairdummy-gdb-marker-frontier] target_sha256=${target_sha}"
echo "[pairdummy-gdb-marker-frontier] marker_timeout=${marker_timeout} frontier_timeout=${frontier_timeout}"
echo "[pairdummy-gdb-marker-frontier] frontier_cmds=${frontier_cmds_file} has_frontier=${has_frontier}"
echo "[pairdummy-gdb-marker-frontier] out_dir=${out_dir}"
echo "[pairdummy-gdb-marker-frontier] transcript=${transcript}"

set +e
"${expect_bin}" -f "${expect_script}" \
  "${gdb}" "${target_bin}" "${local_port}" "${transcript}" \
  "${marker_timeout}" "${frontier_timeout}" "${frontier_cmds_file}" 300 \
  "${has_frontier}" "${marker_delete_after_hit}" "${detach_after}" \
  >"${out_dir}/expect-driver.stdout" 2>"${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[pairdummy-gdb-marker-frontier] expect_rc=${expect_rc}"
if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 260 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

if ! grep -q "GDB_MARK_CONNECTED" "${transcript}" && \
    ! grep -q "GDB_MARK_CONNECTED" "${out_dir}/expect-driver.stdout"; then
  echo "missing GDB_MARK_CONNECTED; see ${transcript}" >&2
  exit 1
fi

echo "[pairdummy-gdb-marker-frontier] PASS"
