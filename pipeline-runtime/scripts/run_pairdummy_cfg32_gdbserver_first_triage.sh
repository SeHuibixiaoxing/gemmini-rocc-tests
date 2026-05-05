#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_first_triage.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Open an SSH local-forward to the FireSim run host and start cross-gdb for the
cfg32 NIC pipeline-runtime gdbserver workload.

Examples:
  run_pairdummy_cfg32_gdbserver_first_triage.sh 192.168.1.23 172.16.0.2
  run_pairdummy_cfg32_gdbserver_first_triage.sh 192.168.1.23 172.16.0.2:2345 32345

Environment:
  RISCV_GDB          Cross GDB path.
  PRT_GDB_TARGET_BIN Host-side RISC-V ELF with symbols.
  PRT_GDB_CONTINUE   If 1, continue after setting first breakpoints. Default: 1.
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
continue_after_breaks="${PRT_GDB_CONTINUE:-1}"

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

if command -v readelf >/dev/null 2>&1; then
  if ! readelf -S "${target_bin}" | grep -q '\.debug_info'; then
    echo "warning: target ELF has no .debug_info: ${target_bin}" >&2
  fi
fi

if pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >/dev/null 2>&1; then
  echo "refusing to open duplicate tunnel on local port ${local_port} to ${guest_ip}:${guest_port}" >&2
  pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >&2 || true
  exit 1
fi

tmpdir="$(mktemp -d)"
tunnel_pid=""
cleanup() {
  if [[ -n "${tunnel_pid}" ]]; then
    kill "${tunnel_pid}" >/dev/null 2>&1 || true
  fi
  rm -rf "${tmpdir}"
}
trap cleanup EXIT

ssh -N \
  -L "${local_port}:${guest_ip}:${guest_port}" \
  -i "${ssh_key}" \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  "ubuntu@${run_host_ip}" &
tunnel_pid="$!"

sleep 1
if ! kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
  echo "SSH tunnel exited before GDB started" >&2
  wait "${tunnel_pid}" || true
  exit 1
fi

gdb_cmds="${tmpdir}/pairdummy-cfg32-first-triage.gdb"
cat > "${gdb_cmds}" <<EOF
set pagination off
set confirm off
set print thread-events off
set breakpoint pending off
set debuginfod enabled off
target remote :${local_port}
info threads
thread apply all bt
info registers
x/16i \$pc
break prt_runtime_run
break prt_action_bind_topology
break stage_prepare_exec_views
break prt_dma_submit
break prt_dma_wait
break dma_blocking_wait
break prt_gemmini_spm_xlate_program
break prt_gemmini_spm_xlate_flush
break prt_rr_release_scope
break prt_gemm_conv_run
break prt_gemm_fence
EOF

if [[ "${continue_after_breaks}" != "0" ]]; then
  cat >> "${gdb_cmds}" <<'EOF'
continue
EOF
fi

echo "[pairdummy-gdb] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[pairdummy-gdb] target_bin=${target_bin}"
echo "[pairdummy-gdb] gdb_cmds=${gdb_cmds}"

"${gdb}" -q -x "${gdb_cmds}" "${target_bin}"
