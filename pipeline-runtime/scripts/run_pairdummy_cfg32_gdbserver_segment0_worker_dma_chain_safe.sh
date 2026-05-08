#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach cross-gdb to a pairdummy cfg32 gdbserver run and walk the early segment0
marker chain with controlled per-marker timeouts. On a marker timeout this
helper sends Ctrl-C, captures thread/register state, detaches, and exits with
124 instead of killing GDB from the outside.

The helper waits for the runtime marker env initialization to return, then
enables the marker filter through GDB. The fixed guest image may therefore keep
markers disabled by default:

  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0

This helper does not probe the gdbserver port; the first TCP connection to the
guest port remains GDB.

Environment:
  RISCV_GDB                         Cross GDB path.
  EXPECT                            expect executable path.
  PRT_GDB_TARGET_BIN                Host-side RISC-V ELF with symbols.
  PRT_GDB_OUT_ROOT                  Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV                   Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC          Optional static neighbor MAC for the guest.
  PRT_GDB_INITIAL_MARKER_TIMEOUT    Timeout for initial segment-begin marker. Default: 1200.
  PRT_GDB_STEP_MARKER_TIMEOUT       Timeout for each later marker. Default: 900.
  PRT_GDB_PAGE_ACCOUNTED_SEQUENCE   Optional comma-separated fixed-load page indices to
                                    wait for after page0 accounting, e.g. 1,2,15,16,23,24.
  PRT_GDB_PAGE_STAGE_ID             Stage id for page sequence filters. Default: 0.
  PRT_GDB_PAGE_MANAGER_ID           Manager id for page sequence filters. Default: 0.
  PRT_GDB_PAGE_TENSOR_ID            Tensor id for page sequence filters. Default: 0.
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
initial_timeout="${PRT_GDB_INITIAL_MARKER_TIMEOUT:-1200}"
step_timeout="${PRT_GDB_STEP_MARKER_TIMEOUT:-900}"
page_accounted_sequence="${PRT_GDB_PAGE_ACCOUNTED_SEQUENCE:-}"
page_stage_id="${PRT_GDB_PAGE_STAGE_ID:-0}"
page_manager_id="${PRT_GDB_PAGE_MANAGER_ID:-0}"
page_tensor_id="${PRT_GDB_PAGE_TENSOR_ID:-0}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-segment0-worker-dma-safe-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
expect_script="${out_dir}/pairdummy-cfg32-segment0-worker-dma-safe.expect"
transcript="${out_dir}/pairdummy-cfg32-segment0-worker-dma-safe.expect.log"

mkdir -p "${out_dir}"

for timeout_value in "${initial_timeout}" "${step_timeout}"; do
  if [[ ! "${timeout_value}" =~ ^[0-9]+$ || "${timeout_value}" -lt 1 ]]; then
    echo "invalid marker timeout: ${timeout_value}" >&2
    exit 2
  fi
done
for filter_value in "${page_stage_id}" "${page_manager_id}" "${page_tensor_id}"; do
  if [[ ! "${filter_value}" =~ ^[0-9]+$ ]]; then
    echo "invalid page sequence filter value: ${filter_value}" >&2
    exit 2
  fi
done
if [[ -n "${page_accounted_sequence}" &&
      ! "${page_accounted_sequence}" =~ ^[0-9]+(,[0-9]+)*$ ]]; then
  echo "invalid PRT_GDB_PAGE_ACCOUNTED_SEQUENCE: ${page_accounted_sequence}" >&2
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

if pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >/dev/null 2>&1; then
  echo "refusing duplicate tunnel on local port ${local_port} to ${guest_ip}:${guest_port}" >&2
  pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >&2 || true
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

if [[ -n "${static_neigh_mac}" ]]; then
  echo "[pairdummy-gdb-safe] static_neigh=${guest_ip} ${static_neigh_mac} dev ${tap_dev}"
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
set timeout 300

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 3]
set initial_timeout [lindex $argv 4]
set step_timeout [lindex $argv 5]
set page_accounted_sequence [lindex $argv 6]
set page_stage_id [lindex $argv 7]
set page_manager_id [lindex $argv 8]
set page_tensor_id [lindex $argv 9]

log_file -noappend $transcript
spawn $gdb -q $elf

proc wait_prompt {{timeout_s 300}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    expect {
        -re "Remote communication error|Target disconnected|Connection reset by peer|Remote connection closed" {
            puts stderr "remote connection failed while waiting for gdb prompt"
            exit 7
        }
        -re "\\(gdb\\) $" {}
        timeout {
            puts stderr "timeout waiting for gdb prompt"
            exit 3
        }
        eof {
            puts stderr "gdb exited unexpectedly"
            exit 4
        }
    }
    set ::timeout $old_timeout
}

proc gdb_cmd {cmd {timeout_s 300}} {
    send -- "$cmd\r"
    wait_prompt $timeout_s
}

proc collect_state {label} {
    puts "PRT_SAFE_COLLECT_BEGIN $label"
    gdb_cmd "printf \"\\n--- safe marker: $label ---\\n\"" 120
    gdb_cmd "print g_prt_gdb_marker_state" 120
    gdb_cmd "print g_prt_debug_state" 120
    gdb_cmd "bt" 300
    gdb_cmd "info threads" 300
    gdb_cmd "thread apply all bt" 300
    gdb_cmd "info registers pc sp ra a0 a1 a2 a3" 120
    gdb_cmd "x/16i \$pc" 120
    puts "PRT_SAFE_COLLECT_END $label"
}

proc collect_marker_state {label} {
    puts "PRT_SAFE_MARKER_BEGIN $label"
    gdb_cmd "printf \"\\n--- safe marker: $label ---\\n\"" 120
    gdb_cmd "print g_prt_gdb_marker_state" 120
    gdb_cmd "print g_prt_debug_state" 120
    gdb_cmd "bt" 300
    puts "PRT_SAFE_MARKER_END $label"
}

proc detach_quit {{exit_code 0}} {
    send -- "detach\r"
    set old_timeout $::timeout
    set ::timeout 180
    expect {
        -re "\\(gdb\\) $" {}
        -re "Remote communication error|Target disconnected|Connection reset by peer|Remote connection closed" {}
        timeout { puts stderr "timeout waiting for detach" }
        eof {}
    }
    set ::timeout $old_timeout
    send -- "quit\r"
    expect {
        eof {}
        timeout {}
    }
    exit $exit_code
}

proc set_filter {site segment global_stage local_stage subbatch manager tensor page token} {
    gdb_cmd "set variable g_prt_gdb_marker_filter.site_id = $site" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.segment_idx = $segment" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.global_stage_id = $global_stage" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.local_stage_id = $local_stage" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.subbatch_id = $subbatch" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.manager_id = $manager" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.tensor_id = $tensor" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.page_idx = $page" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.token_id = $token" 120
}

proc enable_marker_filter {} {
    gdb_cmd "set variable g_prt_gdb_marker_filter.initialized = 1" 120
    gdb_cmd "set variable g_prt_gdb_marker_filter.enabled = 1" 120
}

proc wait_for_runtime_marker_init {} {
    gdb_cmd "tbreak prt_gdb_marker_init_from_env" 120
    puts "PRT_SAFE_WAIT_RUNTIME_MARKER_INIT"
    send -- "continue\r"
    wait_prompt 900
    puts "PRT_SAFE_HIT_RUNTIME_MARKER_INIT"
    gdb_cmd "finish" 900
    puts "PRT_SAFE_AFTER_RUNTIME_MARKER_INIT"
}

proc continue_to_marker {label timeout_s} {
    puts "PRT_SAFE_WAIT_BEGIN $label timeout=$timeout_s"
    send -- "continue\r"
    set old_timeout $::timeout
    set ::timeout $timeout_s
    expect {
        -re "Remote communication error|Target disconnected|Connection reset by peer|Remote connection closed" {
            puts stderr "remote connection failed while waiting for marker $label"
            exit 7
        }
        -re "exited normally|exited with code|Inferior .* exited" {
            puts stderr "inferior exited while waiting for marker $label"
            wait_prompt 120
            exit 8
        }
        -re "\\(gdb\\) $" {
            set ::timeout $old_timeout
            puts "PRT_SAFE_WAIT_HIT $label"
            return 0
        }
        timeout {
            set ::timeout $old_timeout
            puts "PRT_SAFE_WAIT_TIMEOUT $label"
            send \003
            wait_prompt 180
            collect_state "timeout-$label"
            detach_quit 124
        }
        eof {
            puts stderr "gdb exited while waiting for marker $label"
            exit 9
        }
    }
}

wait_prompt 300
gdb_cmd "set pagination off"
gdb_cmd "set confirm off"
gdb_cmd "set print pretty on"
gdb_cmd "set print thread-events off"
gdb_cmd "set breakpoint pending off"
gdb_cmd "set debuginfod enabled off"
gdb_cmd "set auto-load safe-path /"
gdb_cmd "set remotetimeout 180"
gdb_cmd "target remote :$port" 300
puts "PRT_SAFE_CONNECTED"
set any 4294967295
wait_for_runtime_marker_init
enable_marker_filter
set_filter 2 0 $any $any $any $any $any $any $any
gdb_cmd "break prt_gdb_marker_stop" 120

continue_to_marker "segment-begin-env" $initial_timeout
collect_state "segment-begin-env"

set_filter 3 0 $any 0 $any $any $any $any $any
continue_to_marker "worker-create-stage0" $step_timeout
collect_state "worker-create-stage0"

set_filter 4 0 $any 0 $any $any $any $any $any
continue_to_marker "worker-entry-stage0" $step_timeout
collect_state "worker-entry-stage0"

set_filter 12 0 $any 0 $any $any $any $any $any
continue_to_marker "first-dma-wait-enter-stage0" $step_timeout
collect_state "first-dma-wait-enter-stage0"

set_filter 13 0 $any 0 $any $any $any $any $any
continue_to_marker "first-dma-wait-return-stage0" $step_timeout
collect_state "first-dma-wait-return-stage0"

set_filter 25 0 $any 0 $any $any $any $any $any
continue_to_marker "first-submitwait-after-wait-stage0" $step_timeout
collect_state "first-submitwait-after-wait-stage0"

set_filter 26 0 $any 0 $any $any $any $any $any
continue_to_marker "first-submitwait-after-cleanup-stage0" $step_timeout
collect_state "first-submitwait-after-cleanup-stage0"

set_filter 28 0 $any 0 $any $any $any $any $any
continue_to_marker "first-fixed-load-submitwait-end-stage0" $step_timeout
collect_state "first-fixed-load-submitwait-end-stage0"

set_filter 29 0 $any 0 $any $any $any $any $any
continue_to_marker "first-fixed-load-page-accounted-stage0" $step_timeout
collect_state "first-fixed-load-page-accounted-stage0"

if {$page_accounted_sequence ne ""} {
    foreach page [split $page_accounted_sequence ","] {
        set page [string trim $page]
        if {$page eq ""} {
            continue
        }
        if {[expr {$page == 0}]} {
            continue
        }
        set_filter 29 0 $any $page_stage_id $any $page_manager_id $page_tensor_id $page $any
        continue_to_marker "fixed-load-page${page}-accounted-stage${page_stage_id}" $step_timeout
        collect_marker_state "fixed-load-page${page}-accounted-stage${page_stage_id}"
    }
}

puts "PRT_SAFE_SEQUENCE_PASS"
detach_quit 0
EOF

target_sha="$(sha256sum "${target_bin}" | awk '{print $1}')"
echo "[pairdummy-gdb-safe] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-safe] target_bin=${target_bin}"
echo "[pairdummy-gdb-safe] target_sha256=${target_sha}"
echo "[pairdummy-gdb-safe] initial_timeout=${initial_timeout} step_timeout=${step_timeout}"
if [[ -n "${page_accounted_sequence}" ]]; then
  echo "[pairdummy-gdb-safe] page_accounted_sequence=${page_accounted_sequence}"
  echo "[pairdummy-gdb-safe] page_stage_id=${page_stage_id} page_manager_id=${page_manager_id} page_tensor_id=${page_tensor_id}"
fi
echo "[pairdummy-gdb-safe] out_dir=${out_dir}"

set +e
"${expect_bin}" -f "${expect_script}" "${gdb}" "${target_bin}" "${local_port}" "${transcript}" \
  "${initial_timeout}" "${step_timeout}" "${page_accounted_sequence}" \
  "${page_stage_id}" "${page_manager_id}" "${page_tensor_id}" \
  >"${out_dir}/expect-driver.stdout" 2>"${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[pairdummy-gdb-safe] expect_rc=${expect_rc}"
echo "[pairdummy-gdb-safe] transcript=${transcript}"

if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 260 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

if ! grep -q "PRT_SAFE_SEQUENCE_PASS" "${transcript}" && \
   ! grep -q "PRT_SAFE_SEQUENCE_PASS" "${out_dir}/expect-driver.stdout"; then
  echo "missing PRT_SAFE_SEQUENCE_PASS; see ${transcript}" >&2
  exit 1
fi

echo "[pairdummy-gdb-safe] PASS"
