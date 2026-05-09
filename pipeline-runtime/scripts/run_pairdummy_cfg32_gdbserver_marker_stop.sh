#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_marker_stop.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach cross-gdb to a pipeline-runtime gdbserver --once run, break on the
source-level prt_gdb_marker_stop() helper, continue until the marker hits, and
capture the marker state plus backtraces. This helper never probes the guest
TCP port with nc/telnet/curl; the first TCP connection to gdbserver is GDB.

Environment:
  RISCV_GDB                 Cross GDB path.
  PRT_GDB_TARGET_BIN        Host-side RISC-V ELF with symbols. Defaults to the
                            FireMarshal-staged build/rerocc-linux-tests binary.
  PRT_GDB_OUT_ROOT          Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV           Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC  Optional static neighbor MAC for the guest.
  PRT_GDB_MARKER_TIMEOUT    Seconds before killing GDB. Default: 1200.
  PRT_GDB_INITIAL_CONTINUE_TIMEOUT
                            Optional seconds before GDB interrupts the initial
                            continue while waiting for prt_gdb_marker_stop().
                            Default: 0, disabled.
  PRT_GDB_MARKER_DETACH     Detach after collecting marker state. Default: 1.
  PRT_GDB_MARKER_DELETE_AFTER_HIT
                            Delete breakpoint 1 after the marker hit. Default: 0.
  PRT_GDB_POST_MARKER_GDB_CMDS
                            Optional GDB commands to append after marker evidence.
                            C-style backslash escapes are expanded, so use \n for
                            multi-line snippets.
  PRT_GDB_POST_MARKER_GDB_FILE
                            Optional file of GDB commands to source after marker
                            evidence. Runs before the optional detach/quit.
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
marker_timeout="${PRT_GDB_MARKER_TIMEOUT:-1200}"
initial_continue_timeout="${PRT_GDB_INITIAL_CONTINUE_TIMEOUT:-0}"
marker_detach="${PRT_GDB_MARKER_DETACH:-1}"
marker_delete_after_hit="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-0}"
post_marker_gdb_cmds="${PRT_GDB_POST_MARKER_GDB_CMDS:-}"
post_marker_gdb_file="${PRT_GDB_POST_MARKER_GDB_FILE:-}"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/pairdummy-cfg32-marker-stop-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
gdb_cmds="${out_dir}/pairdummy-cfg32-marker-stop.gdb"
transcript="${out_dir}/pairdummy-cfg32-marker-stop.gdb.log"

mkdir -p "${out_dir}"

if [[ ! "${marker_timeout}" =~ ^[0-9]+$ || "${marker_timeout}" -lt 1 ]]; then
  echo "invalid PRT_GDB_MARKER_TIMEOUT=${marker_timeout}" >&2
  exit 2
fi
if [[ ! "${initial_continue_timeout}" =~ ^[0-9]+$ ]]; then
  echo "invalid PRT_GDB_INITIAL_CONTINUE_TIMEOUT=${initial_continue_timeout}" >&2
  exit 2
fi
case "${marker_detach}" in
  0|1) ;;
  *)
    echo "invalid PRT_GDB_MARKER_DETACH=${marker_detach}; expected 0 or 1" >&2
    exit 2
    ;;
esac
case "${marker_delete_after_hit}" in
  0|1) ;;
  *)
    echo "invalid PRT_GDB_MARKER_DELETE_AFTER_HIT=${marker_delete_after_hit}; expected 0 or 1" >&2
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
if [[ -n "${post_marker_gdb_file}" && ! -f "${post_marker_gdb_file}" ]]; then
  echo "missing PRT_GDB_POST_MARKER_GDB_FILE: ${post_marker_gdb_file}" >&2
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

cat > "${gdb_cmds}" <<EOF
set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 120
set tcp connect-timeout 60
target remote :${local_port}
break prt_gdb_marker_stop
set \$prt_initial_continue_timeout = 0
EOF

if [[ "${initial_continue_timeout}" -gt 0 ]]; then
  cat >> "${gdb_cmds}" <<EOF
python
import gdb
import threading

_prt_initial_continue_cancel = threading.Event()

def _prt_initial_continue_stop_handler(event):
    _prt_initial_continue_cancel.set()

gdb.events.stop.connect(_prt_initial_continue_stop_handler)

def _prt_interrupt_initial_continue():
    if _prt_initial_continue_cancel.wait(${initial_continue_timeout}):
        return
    def _do_interrupt():
        try:
            gdb.execute("set \$prt_initial_continue_timeout = 1")
            gdb.execute("interrupt")
        except Exception as exc:
            try:
                gdb.write("[prt-gdb] initial continue interrupt failed: %s\\n" % exc)
            except Exception:
                pass
    try:
        gdb.post_event(_do_interrupt)
    except Exception as exc:
        try:
            gdb.write("[prt-gdb] initial continue post_event failed: %s\\n" % exc)
        except Exception:
            pass

threading.Thread(target=_prt_interrupt_initial_continue, daemon=True).start()
end
EOF
fi

cat >> "${gdb_cmds}" <<'EOF'
continue
if $prt_initial_continue_timeout
  printf "\n--- initial continue timeout before marker ---\n"
  x/i $pc
  printf "\n--- current bt at initial-timeout stop ---\n"
  bt
  printf "\n--- all thread bt at initial-timeout stop ---\n"
  thread apply all bt
  printf "\n--- debug states at initial-timeout stop ---\n"
  print g_prt_debug_state
  print g_prt_debug_tls_state
  print g_prt_gdb_marker_state
  printf "\n--- registers at initial-timeout stop ---\n"
  info registers
  printf "\n--- pc window at initial-timeout stop ---\n"
  x/16i $pc-32
  detach
  quit 20
end
printf "\n--- marker state ---\n"
print g_prt_gdb_marker_state
printf "\n--- current bt ---\n"
bt
printf "\n--- all thread bt ---\n"
thread apply all bt
printf "\n--- registers ---\n"
info registers
printf "\n--- pc window ---\n"
x/16i $pc-32
EOF

if [[ "${marker_delete_after_hit}" == "1" ]]; then
  cat >> "${gdb_cmds}" <<'EOF'
delete 1
EOF
fi

if [[ -n "${post_marker_gdb_cmds}" ]]; then
  {
    echo 'printf "\n--- post-marker inline gdb commands ---\n"'
    printf '%b\n' "${post_marker_gdb_cmds}"
  } >> "${gdb_cmds}"
fi

if [[ -n "${post_marker_gdb_file}" ]]; then
  {
    echo 'printf "\n--- post-marker gdb command file ---\n"'
    printf 'source %s\n' "${post_marker_gdb_file}"
  } >> "${gdb_cmds}"
fi

if [[ "${marker_detach}" == "1" ]]; then
  cat >> "${gdb_cmds}" <<'EOF'
detach
quit
EOF
fi

target_sha="$(sha256sum "${target_bin}" | awk '{print $1}')"
echo "[pairdummy-gdb-marker] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb-marker] target_bin=${target_bin}"
echo "[pairdummy-gdb-marker] target_sha256=${target_sha}"
echo "[pairdummy-gdb-marker] gdb_cmds=${gdb_cmds}"
echo "[pairdummy-gdb-marker] transcript=${transcript}"

set +e
timeout "${marker_timeout}" "${gdb}" -q -batch -x "${gdb_cmds}" "${target_bin}" 2>&1 | tee "${transcript}"
gdb_rc=${PIPESTATUS[0]}
set -e

echo "[pairdummy-gdb-marker] gdb_rc=${gdb_rc}"
echo "[pairdummy-gdb-marker] out_dir=${out_dir}"
exit "${gdb_rc}"
