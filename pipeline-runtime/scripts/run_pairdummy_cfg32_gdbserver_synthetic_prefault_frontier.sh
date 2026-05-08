#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_synthetic_prefault_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach cross-gdb to a pipeline-runtime gdbserver --once run prepared with:

  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin

The helper stops at prt_gdb_marker_stop(), arms source breakpoints in
prefault_and_lock_blob(), then advances through the synthetic model prefault
progress points. If the target stops responding, the helper sends Ctrl-C to the
GDB PTY and dumps stack/register/local context from the interrupted target.

This helper never probes the guest TCP port with nc/telnet/curl; the first TCP
connection to gdbserver is GDB.

Environment:
  RISCV_GDB                    Cross GDB path.
  PRT_GDB_TARGET_BIN           Host-side RISC-V ELF with symbols.
  PRT_GDB_OUT_ROOT             Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV              Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC     Optional static neighbor MAC for the guest.
  EXPECT_BIN                   Expect interpreter. Defaults to PATH lookup.
  PRT_GDB_PREF_HIT_TIMEOUT     Seconds to wait for marker hit. Default: 1200.
  PRT_GDB_PREF_ADVANCE_TIMEOUT Seconds to wait between prefault stops. Default: 180.
  PRT_GDB_PREF_DIAG_TIMEOUT    Seconds to wait for each diagnostic command. Default: 120.
  PRT_GDB_PREF_MIN_PAGES       First progress breakpoint threshold. Default: 3328.
  PRT_GDB_PREF_MAX_STOPS       Max prefault stops before classifying as no-end. Default: 8.
  PRT_GDB_DETACH               Detach before quitting after diagnostics. Default: 1.
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
target_bin="${PRT_GDB_TARGET_BIN:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux}"
ssh_key="${FIRESIM_SSH_KEY:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
out_root="${PRT_GDB_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
tap_dev="${PRT_GDB_TAP_DEV:-tap0}"
static_neigh_mac="${PRT_GDB_STATIC_NEIGH_MAC:-}"
expect_bin="${EXPECT_BIN:-$(command -v expect || true)}"
hit_timeout="${PRT_GDB_PREF_HIT_TIMEOUT:-1200}"
advance_timeout="${PRT_GDB_PREF_ADVANCE_TIMEOUT:-180}"
diag_timeout="${PRT_GDB_PREF_DIAG_TIMEOUT:-120}"
min_pages="${PRT_GDB_PREF_MIN_PAGES:-3328}"
max_stops="${PRT_GDB_PREF_MAX_STOPS:-8}"
detach_after="${PRT_GDB_DETACH:-1}"
runtime_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-synthetic-prefault-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
gdb_setup="${out_dir}/synthetic-prefault-setup.gdb"
gdb_marker="${out_dir}/synthetic-prefault-marker.gdb"
gdb_arm="${out_dir}/synthetic-prefault-arm.gdb"
gdb_progress="${out_dir}/synthetic-prefault-progress.gdb"
gdb_after="${out_dir}/synthetic-prefault-after.gdb"
gdb_timeout="${out_dir}/synthetic-prefault-timeout.gdb"
expect_script="${out_dir}/synthetic-prefault.expect"
transcript="${out_dir}/synthetic-prefault.gdb.log"

mkdir -p "${out_dir}"

for numeric in hit_timeout advance_timeout diag_timeout min_pages max_stops; do
  value="${!numeric}"
  if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
    echo "invalid ${numeric}=${value}" >&2
    exit 2
  fi
done
if [[ "${hit_timeout}" -lt 1 || "${advance_timeout}" -lt 1 ||
      "${diag_timeout}" -lt 1 || "${max_stops}" -lt 1 ]]; then
  echo "timeouts and max_stops must be positive" >&2
  exit 2
fi

case "${detach_after}" in
  0|1) ;;
  *)
    echo "invalid PRT_GDB_DETACH=${detach_after}; expected 0 or 1" >&2
    exit 2
    ;;
esac

if [[ ! -x "${gdb}" ]]; then
  echo "missing executable cross-gdb: ${gdb}" >&2
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
if [[ -z "${expect_bin}" || ! -x "${expect_bin}" ]]; then
  echo "missing executable expect interpreter; set EXPECT_BIN" >&2
  exit 1
fi
if [[ ! -f "${runtime_src}" ]]; then
  echo "missing runtime source: ${runtime_src}" >&2
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
  "${ssh_base[@]}" \
    "sudo ip neigh replace '${guest_ip}' lladdr '${static_neigh_mac}' dev '${tap_dev}' nud permanent && ip neigh show dev '${tap_dev}'" \
    > "${out_dir}/static-neigh.log" 2>&1
fi

cleanup() {
  if [[ -n "${tunnel_pid:-}" ]]; then
    kill "${tunnel_pid}" >/dev/null 2>&1 || true
    wait "${tunnel_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

ssh \
  -i "${ssh_key}" \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  -N \
  -L "${local_port}:${guest_ip}:${guest_port}" \
  "${ssh_user}@${run_host_ip}" \
  > "${tunnel_log}" 2>&1 &
tunnel_pid="$!"

sleep 1
if ! kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
  echo "SSH tunnel exited before GDB started" >&2
  wait "${tunnel_pid}" || true
  exit 1
fi

cat > "${gdb_setup}" <<EOF
set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 120
set tcp connect-timeout 60
target remote :${local_port}
break prt_gdb_marker_stop
continue
EOF

cat > "${gdb_marker}" <<'EOF'
printf "\n--- synthetic prefault marker hit ---\n"
print g_prt_gdb_marker_state
bt 16
printf "\n--- marker caller frame ---\n"
frame 1
info args
info locals
print kind
print path
print buf
print blob_size
print host_page_bytes
print step
print total_pages
print touched_pages
print next_progress_bytes
x/16i $pc-32
frame 0
EOF

cat > "${gdb_arm}" <<EOF
delete 1
break ${runtime_src}:995 if touched_pages >= ${min_pages}
tbreak ${runtime_src}:1010
info breakpoints
EOF

cat > "${gdb_progress}" <<'EOF'
printf "\n--- synthetic prefault progress stop ---\n"
printf "hit_bpnum="
print $_hit_bpnum
bt 12
info args
info locals
print off
print touched_pages
print blob_size
print step
print total_pages
print next_progress_bytes
print buf
print touch
print &touch[off]
x/16xb &touch[off]
x/20i $pc-40
EOF

cat > "${gdb_after}" <<'EOF'
printf "\n--- synthetic prefault after-prefault reached ---\n"
printf "hit_bpnum="
print $_hit_bpnum
bt 12
info args
info locals
print blob_size
print step
print total_pages
print touched_pages
print buf
print touch
x/20i $pc-40
EOF

cat > "${gdb_timeout}" <<EOF
printf "\\n--- synthetic prefault timeout diagnostic ---\\n"
printf "min_pages=${min_pages}\\n"
bt full
printf "\\n--- all thread bt full ---\\n"
thread apply all bt full
printf "\\n--- current frame args/locals ---\\n"
info args
info locals
printf "\\n--- marker state ---\\n"
print g_prt_gdb_marker_state
printf "\\n--- debug state ---\\n"
print g_prt_debug_state
print g_prt_debug_tls_state
printf "\\n--- registers ---\\n"
info registers
printf "\\n--- pc window ---\\n"
x/32i \$pc-64
EOF

if [[ "${detach_after}" == "1" ]]; then
  cat >> "${gdb_timeout}" <<'EOF'
detach
EOF
  cat >> "${gdb_after}" <<'EOF'
detach
EOF
fi

cat > "${expect_script}" <<'EOF'
#!/usr/bin/env expect
set timeout -1

set gdb [lindex $argv 0]
set target_bin [lindex $argv 1]
set setup_cmds [lindex $argv 2]
set marker_cmds [lindex $argv 3]
set arm_cmds [lindex $argv 4]
set progress_cmds [lindex $argv 5]
set after_cmds [lindex $argv 6]
set timeout_cmds [lindex $argv 7]
set transcript [lindex $argv 8]
set hit_timeout [lindex $argv 9]
set advance_timeout [lindex $argv 10]
set diag_timeout [lindex $argv 11]
set max_stops [lindex $argv 12]

log_file -a $transcript
spawn $gdb -q $target_bin

proc wait_prompt {seconds} {
  set timeout $seconds
  expect {
    -re {\(gdb\) $} { return 0 }
    eof { return 2 }
    timeout { return 1 }
  }
}

proc source_gdb {cmds seconds} {
  send -- "source $cmds\r"
  return [wait_prompt $seconds]
}

proc interrupt_and_diag {timeout_cmds diag_timeout} {
  send "\003"
  set rc [wait_prompt 30]
  if {$rc == 0} {
    send -- "source $timeout_cmds\r"
    set diag_rc [wait_prompt $diag_timeout]
    send -- "quit\r"
    expect {
      eof {}
      timeout {}
    }
    exit 124
  }
  send -- "quit\r"
  expect {
    eof {}
    timeout {}
  }
  exit 125
}

if {[wait_prompt 60] != 0} {
  exit 1
}

send -- "source $setup_cmds\r"
set rc [wait_prompt $hit_timeout]
if {$rc != 0} {
  interrupt_and_diag $timeout_cmds $diag_timeout
}

if {[source_gdb $marker_cmds $diag_timeout] != 0} {
  interrupt_and_diag $timeout_cmds $diag_timeout
}
if {[source_gdb $arm_cmds $diag_timeout] != 0} {
  interrupt_and_diag $timeout_cmds $diag_timeout
}

for {set i 0} {$i < $max_stops} {incr i} {
  send -- "continue\r"
  set timeout $advance_timeout
  expect {
    -re {Temporary breakpoint 3,} {
      set rc [wait_prompt $diag_timeout]
      if {$rc != 0} {
        interrupt_and_diag $timeout_cmds $diag_timeout
      }
      if {[source_gdb $after_cmds $diag_timeout] != 0} {
        interrupt_and_diag $timeout_cmds $diag_timeout
      }
      send -- "quit\r"
      expect eof
      exit 0
    }
    -re {Breakpoint 2,} {
      set rc [wait_prompt $diag_timeout]
      if {$rc != 0} {
        interrupt_and_diag $timeout_cmds $diag_timeout
      }
      if {[source_gdb $progress_cmds $diag_timeout] != 0} {
        interrupt_and_diag $timeout_cmds $diag_timeout
      }
    }
    eof {
      exit 2
    }
    timeout {
      interrupt_and_diag $timeout_cmds $diag_timeout
    }
  }
}

send -- "source $timeout_cmds\r"
set diag_rc [wait_prompt $diag_timeout]
send -- "quit\r"
expect {
  eof {}
  timeout {}
}
exit 126
EOF
chmod +x "${expect_script}"

target_sha="$(sha256sum "${target_bin}" | awk '{print $1}')"
echo "[pairdummy-gdb-synthetic-prefault] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-synthetic-prefault] target_bin=${target_bin}"
echo "[pairdummy-gdb-synthetic-prefault] target_sha256=${target_sha}"
echo "[pairdummy-gdb-synthetic-prefault] min_pages=${min_pages}"
echo "[pairdummy-gdb-synthetic-prefault] hit_timeout=${hit_timeout} advance_timeout=${advance_timeout} diag_timeout=${diag_timeout}"
echo "[pairdummy-gdb-synthetic-prefault] gdb_setup=${gdb_setup}"
echo "[pairdummy-gdb-synthetic-prefault] transcript=${transcript}"

set +e
"${expect_bin}" "${expect_script}" \
  "${gdb}" "${target_bin}" "${gdb_setup}" "${gdb_marker}" "${gdb_arm}" \
  "${gdb_progress}" "${gdb_after}" "${gdb_timeout}" "${transcript}" \
  "${hit_timeout}" "${advance_timeout}" "${diag_timeout}" "${max_stops}"
gdb_rc=$?
set -e

echo "[pairdummy-gdb-synthetic-prefault] gdb_rc=${gdb_rc}"
echo "[pairdummy-gdb-synthetic-prefault] out_dir=${out_dir}"
exit "${gdb_rc}"
