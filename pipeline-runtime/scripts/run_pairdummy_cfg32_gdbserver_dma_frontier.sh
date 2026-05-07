#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_dma_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Run a non-interactive remote-GDB session against the cfg32 NIC
pipeline-runtime gdbserver workload and stop near the DMA wait frontier.
The target workload must already have printed its [gdbserver] phase=listening
announcement. This helper does not probe the target port with nc/telnet/curl;
the first TCP connection to gdbserver --once is the GDB target remote command.

Environment:
  RISCV_GDB                    Cross GDB path.
  EXPECT                       expect executable path.
  PRT_GDB_TARGET_BIN           Host-side RISC-V ELF with symbols.
  PRT_GDB_OUT_ROOT             Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV              Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC     Optional static neighbor MAC for the guest.
  PRT_GDB_FRONTIER_FUNC        Breakpoint function. Default: dma_blocking_wait.
  PRT_GDB_FRONTIER_CONDITION   GDB C expression. Default targets stage0 tensor2 token>=546.
  PRT_GDB_FRONTIER_TIMEOUT     Seconds to wait for frontier breakpoint. Default: 1200.
  PRT_GDB_POST_HIT_SECONDS     Seconds to run after breakpoint before Ctrl-C. Default: 8.
  PRT_GDB_INTERRUPT_TIMEOUT    Seconds to wait for Ctrl-C stop. Default: 240.
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
frontier_func="${PRT_GDB_FRONTIER_FUNC:-dma_blocking_wait}"
frontier_condition="${PRT_GDB_FRONTIER_CONDITION:-tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 546}"
frontier_timeout="${PRT_GDB_FRONTIER_TIMEOUT:-1200}"
post_hit_seconds="${PRT_GDB_POST_HIT_SECONDS:-8}"
interrupt_timeout="${PRT_GDB_INTERRUPT_TIMEOUT:-240}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-dma-frontier-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
expect_script="${out_dir}/pairdummy-cfg32-dma-frontier.expect"
transcript="${out_dir}/pairdummy-cfg32-dma-frontier.expect.log"
expect_stdout="${out_dir}/expect-driver.stdout"

mkdir -p "${out_dir}"

for numeric in frontier_timeout post_hit_seconds interrupt_timeout; do
  value="${!numeric}"
  if [[ ! "${value}" =~ ^[0-9]+$ || "${value}" -lt 1 ]]; then
    echo "invalid ${numeric}=${value}" >&2
    exit 2
  fi
done

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
  echo "[pairdummy-gdb-dma-frontier] static_neigh=${guest_ip} ${static_neigh_mac} dev ${tap_dev}"
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
set timeout [lindex $argv 6]

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 3]
set frontier_func [lindex $argv 4]
set frontier_condition [lindex $argv 5]
set frontier_timeout [lindex $argv 6]
set post_hit_seconds [lindex $argv 7]
set interrupt_timeout [lindex $argv 8]

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
        -re "Remote communication error|Target disconnected|Connection reset by peer|Connection refused" {
            puts stderr "target remote failed"
            exit 7
        }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for target remote"; exit 8 }
        eof { puts stderr "gdb exited during target remote"; exit 9 }
    }
    set ::timeout $old_timeout
}

proc continue_to_frontier {timeout_s} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "continue\r"
    expect {
        -re "Breakpoint \[0-9\]+, .*" { need_prompt; set ::timeout $old_timeout; return 0 }
        -re "Program received signal SIGTRAP|Thread .* received signal SIGTRAP" { need_prompt; set ::timeout $old_timeout; return 0 }
        -re "exited normally|exited with code" {
            puts "GDB_DMA_FRONTIER_INFERIOR_EXITED_BEFORE_HIT"
            need_prompt
            set ::timeout $old_timeout
            return 1
        }
        timeout { puts stderr "timeout waiting for DMA frontier breakpoint"; exit 11 }
        eof { puts stderr "gdb exited during DMA frontier continue"; exit 12 }
    }
}

proc continue_then_interrupt {post_hit_seconds interrupt_timeout} {
    set old_timeout $::timeout
    set wait_ms [expr {$post_hit_seconds * 1000}]
    set ::timeout $interrupt_timeout
    puts "GDB_DMA_FRONTIER_POST_CONTINUE"
    send -- "continue\r"
    after $wait_ms
    puts "GDB_DMA_FRONTIER_POST_INTERRUPT"
    send \003
    expect {
        -re "received signal SIGINT|Program received signal SIGINT|Thread .* received signal SIGINT|Program received signal SIGTRAP|Thread .* received signal SIGTRAP" {
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "exited normally|exited with code" {
            puts "GDB_DMA_FRONTIER_INFERIOR_EXITED_AFTER_HIT"
            need_prompt
            set ::timeout $old_timeout
            return 1
        }
        -re "\\(gdb\\) $" {
            set ::timeout $old_timeout
            return 0
        }
        timeout { puts stderr "timeout waiting for Ctrl-C/SIGINT stop"; exit 13 }
        eof { puts stderr "gdb exited during Ctrl-C after DMA frontier"; exit 14 }
    }
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
puts "GDB_DMA_FRONTIER_MARK_CONNECTED"

gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 300
gdb_cmd "info registers pc sp ra a0 a1 a2 a3"
gdb_cmd "break $frontier_func if $frontier_condition"
gdb_cmd "info breakpoints"
puts "GDB_DMA_FRONTIER_MARK_BREAK_SET"

set exited [continue_to_frontier $frontier_timeout]
if {$exited} {
    send -- "quit\r"
    expect eof
    exit 0
}
puts "GDB_DMA_FRONTIER_MARK_HIT"
gdb_cmd "bt" 300
gdb_cmd "info threads"
gdb_cmd "thread apply all bt" 300
gdb_cmd "info registers pc sp ra a0 a1 a2 a3"
gdb_cmd "print/x tok"
gdb_cmd "print tok->id"
gdb_cmd "print tok->stage_idx"
gdb_cmd "print tok->tensor_id"
gdb_cmd "print tok->rr_manager_id"
gdb_cmd "print tok->rr_scope_valid"
gdb_cmd "print tok->rr_scope_external"
gdb_cmd "print tok->hw_done_flag"
gdb_cmd "print/x tok->debug_src_addr"
gdb_cmd "print/x tok->debug_dst_addr"
gdb_cmd "print/x tok->debug_done_flag_pa"
gdb_cmd "print tok->debug_bytes"
gdb_cmd "x/16i \$pc"
gdb_cmd "disable"

set exited_after [continue_then_interrupt $post_hit_seconds $interrupt_timeout]
if {!$exited_after} {
    puts "GDB_DMA_FRONTIER_MARK_INTERRUPTED_AFTER_HIT"
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
puts "GDB_DMA_FRONTIER_MARK_DETACH_OK"

send -- "quit\r"
expect eof
exit 0
EOF

echo "[pairdummy-gdb-dma-frontier] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-dma-frontier] target_bin=${target_bin}"
echo "[pairdummy-gdb-dma-frontier] frontier_func=${frontier_func}"
echo "[pairdummy-gdb-dma-frontier] frontier_condition=${frontier_condition}"
echo "[pairdummy-gdb-dma-frontier] frontier_timeout=${frontier_timeout} post_hit_seconds=${post_hit_seconds}"
echo "[pairdummy-gdb-dma-frontier] out_dir=${out_dir}"

set +e
"${expect_bin}" -f "${expect_script}" \
  "${gdb}" \
  "${target_bin}" \
  "${local_port}" \
  "${transcript}" \
  "${frontier_func}" \
  "${frontier_condition}" \
  "${frontier_timeout}" \
  "${post_hit_seconds}" \
  "${interrupt_timeout}" \
  >"${expect_stdout}" 2>"${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[pairdummy-gdb-dma-frontier] expect_rc=${expect_rc}"
echo "[pairdummy-gdb-dma-frontier] transcript=${transcript}"

if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 260 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

for marker in \
  GDB_DMA_FRONTIER_MARK_CONNECTED \
  GDB_DMA_FRONTIER_MARK_BREAK_SET; do
  if ! grep -q "${marker}" "${transcript}" && ! grep -q "${marker}" "${expect_stdout}"; then
    echo "missing ${marker}; see ${transcript} and ${expect_stdout}" >&2
    tail -n 260 "${transcript}" >&2 || true
    tail -n 260 "${expect_stdout}" >&2 || true
    exit 1
  fi
done

if grep -q "GDB_DMA_FRONTIER_INFERIOR_EXITED_BEFORE_HIT" "${transcript}" || \
   grep -q "GDB_DMA_FRONTIER_INFERIOR_EXITED_BEFORE_HIT" "${expect_stdout}"; then
  echo "[pairdummy-gdb-dma-frontier] inferior exited before frontier hit"
  exit 0
fi

for marker in \
  GDB_DMA_FRONTIER_MARK_HIT \
  GDB_DMA_FRONTIER_MARK_DETACH_OK; do
  if ! grep -q "${marker}" "${transcript}" && ! grep -q "${marker}" "${expect_stdout}"; then
    echo "missing ${marker}; see ${transcript} and ${expect_stdout}" >&2
    tail -n 260 "${transcript}" >&2 || true
    tail -n 260 "${expect_stdout}" >&2 || true
    exit 1
  fi
done

echo "[pairdummy-gdb-dma-frontier] PASS"
