#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_artifact_validate_stage_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach cross-gdb to a pipeline-runtime gdbserver --once run, stop at
validate_stage_against_mapping_entries() for one artifact validation stage,
then use a bounded GDB `finish` to classify whether that stage returns quickly
or wedges inside validation. On timeout, this helper sends Ctrl-C to the GDB
PTY and asks GDB to dump stack/register context.

This helper never probes the guest TCP port with nc/telnet/curl; the first TCP
connection to gdbserver is GDB.

Environment:
  RISCV_GDB                 Cross GDB path.
  PRT_GDB_TARGET_BIN        Host-side RISC-V ELF with symbols.
  PRT_GDB_OUT_ROOT          Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV           Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC  Optional static neighbor MAC for the guest.
  EXPECT_BIN                Expect interpreter. Defaults to PATH lookup.
  PRT_GDB_VALIDATE_SEG      Segment index to catch. Default: 14.
  PRT_GDB_VALIDATE_STAGE    Local stage index to catch. Default: 0.
  PRT_GDB_HIT_TIMEOUT       Seconds to wait for the stage breakpoint. Default: 900.
  PRT_GDB_FINISH_TIMEOUT    Seconds to wait for the selected stage to return. Default: 120.
  PRT_GDB_DETACH            Detach before quitting after diagnostics. Default: 1.
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
validate_seg="${PRT_GDB_VALIDATE_SEG:-14}"
validate_stage="${PRT_GDB_VALIDATE_STAGE:-0}"
hit_timeout="${PRT_GDB_HIT_TIMEOUT:-900}"
finish_timeout="${PRT_GDB_FINISH_TIMEOUT:-120}"
detach_after="${PRT_GDB_DETACH:-1}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-artifact-validate-stage-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
gdb_setup="${out_dir}/artifact-validate-stage-setup.gdb"
gdb_hit="${out_dir}/artifact-validate-stage-hit.gdb"
gdb_finish="${out_dir}/artifact-validate-stage-finish.gdb"
gdb_timeout="${out_dir}/artifact-validate-stage-timeout.gdb"
expect_script="${out_dir}/artifact-validate-stage.expect"
transcript="${out_dir}/artifact-validate-stage.gdb.log"

mkdir -p "${out_dir}"

for numeric in validate_seg validate_stage hit_timeout finish_timeout; do
  value="${!numeric}"
  if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
    echo "invalid ${numeric}=${value}" >&2
    exit 2
  fi
done

case "${detach_after}" in
  0|1) ;;
  *)
    echo "invalid PRT_GDB_DETACH=${detach_after}; expected 0 or 1" >&2
    exit 2
    ;;
esac

if [[ "${hit_timeout}" -lt 1 || "${finish_timeout}" -lt 1 ]]; then
  echo "timeouts must be positive" >&2
  exit 2
fi

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
break validate_stage_against_mapping_entries if seg_idx == ${validate_seg} && stage_idx == ${validate_stage}
continue
EOF

cat > "${gdb_hit}" <<'EOF'
printf "\n--- artifact validation stage hit ---\n"
bt 12
info args
info locals
print path
print db->count
print seg_idx
print stage_idx
print *stage
print stage->stage_id
print stage->layer_id
print stage->acc_util
print stage->tensor_id_count
print stage->dram_bypass_count
print stage->spm_bypass_count
print stage->local_spm_tensor_count
print stage->local_spm_page_span
set $stagep = stage
set $dbp = db
x/16i $pc-32
printf "\n--- artifact validation stage finish begins ---\n"
EOF

cat > "${gdb_finish}" <<'EOF'
printf "\n--- artifact validation stage returned ---\n"
bt 12
info args
info locals
print $_return
print $stagep->stage_id
print $stagep->layer_id
print $stagep->local_spm_tensor_count
print $stagep->local_spm_page_span
x/16i $pc-32
EOF

cat > "${gdb_timeout}" <<EOF
printf "\\n--- artifact validation timeout diagnostic ---\\n"
printf "expected seg=${validate_seg} stage=${validate_stage}\\n"
bt full
printf "\\n--- all thread bt full ---\\n"
thread apply all bt full
printf "\\n--- registers ---\\n"
info registers
printf "\\n--- pc window ---\\n"
x/24i \$pc-48
printf "\\n--- stage/db convenience pointers ---\\n"
print \$stagep
print \$dbp
print g_prt_gdb_marker_state
EOF

if [[ "${detach_after}" == "1" ]]; then
  cat >> "${gdb_timeout}" <<'EOF'
detach
EOF
  cat >> "${gdb_finish}" <<'EOF'
detach
EOF
fi

cat > "${expect_script}" <<'EOF'
#!/usr/bin/env expect
set timeout -1

set gdb [lindex $argv 0]
set target_bin [lindex $argv 1]
set setup_cmds [lindex $argv 2]
set hit_cmds [lindex $argv 3]
set finish_cmds [lindex $argv 4]
set timeout_cmds [lindex $argv 5]
set transcript [lindex $argv 6]
set hit_timeout [lindex $argv 7]
set finish_timeout [lindex $argv 8]

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

proc send_gdb {cmd seconds} {
  send -- "$cmd\r"
  return [wait_prompt $seconds]
}

proc interrupt_and_diag {timeout_cmds} {
  send "\003"
  set rc [wait_prompt 30]
  if {$rc == 0} {
    send -- "source $timeout_cmds\r"
    set diag_rc [wait_prompt 120]
    send -- "quit\r"
    expect eof
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
  interrupt_and_diag $timeout_cmds
}

set rc [send_gdb "source $hit_cmds" 120]
if {$rc != 0} {
  interrupt_and_diag $timeout_cmds
}

send -- "finish\r"
set rc [wait_prompt $finish_timeout]
if {$rc != 0} {
  interrupt_and_diag $timeout_cmds
}

set rc [send_gdb "source $finish_cmds" 120]
if {$rc != 0} {
  interrupt_and_diag $timeout_cmds
}

send -- "quit\r"
expect eof
exit 0
EOF
chmod +x "${expect_script}"

target_sha="$(sha256sum "${target_bin}" | awk '{print $1}')"
echo "[pairdummy-gdb-artifact-validate] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-artifact-validate] target_bin=${target_bin}"
echo "[pairdummy-gdb-artifact-validate] target_sha256=${target_sha}"
echo "[pairdummy-gdb-artifact-validate] seg=${validate_seg} stage=${validate_stage}"
echo "[pairdummy-gdb-artifact-validate] hit_timeout=${hit_timeout} finish_timeout=${finish_timeout}"
echo "[pairdummy-gdb-artifact-validate] gdb_setup=${gdb_setup}"
echo "[pairdummy-gdb-artifact-validate] transcript=${transcript}"

set +e
"${expect_bin}" "${expect_script}" \
  "${gdb}" "${target_bin}" "${gdb_setup}" "${gdb_hit}" "${gdb_finish}" \
  "${gdb_timeout}" "${transcript}" "${hit_timeout}" "${finish_timeout}"
gdb_rc=$?
set -e

echo "[pairdummy-gdb-artifact-validate] gdb_rc=${gdb_rc}"
echo "[pairdummy-gdb-artifact-validate] out_dir=${out_dir}"
exit "${gdb_rc}"
