#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: prepare_firesim_networked_gdbserver.sh <run-host-private-ip>

Prepare the FireSim 1-node networked topology for host<->guest gdbserver attach:
  1. Rewrite the newest switch0 build to add SSHPort(1)
  2. Rebuild and redeploy switch0 to the run host
  3. Create/configure tap0 on the run host
  4. Stop Xilinx hw_server on the run host so its UDP discovery broadcast
     does not enter the guest NIC RX path

This helper is only intended for FireSim `example_1config`.
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[networked-gdbserver] missing required command: $1" >&2
    exit 1
  fi
}

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
private_ip="$1"
ssh_key_path="${FIRESIM_SSH_KEY_PATH:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
tap_user="${FIRESIM_TAP_USER:-${ssh_user}}"
switch_root="${cy_dir}/sims/firesim/target-design/switch"
remote_switch_path="/home/${ssh_user}/switch_slot_0/switch0"
remote_switch_tmp="${remote_switch_path}.new"
remote_tap_mac="${FIRESIM_TAP_MAC:-8e:6b:35:04:00:00}"
remote_tap_addr="${FIRESIM_TAP_ADDR:-172.16.0.1/16}"
kill_hw_server="${FIRESIM_GDBSERVER_KILL_HW_SERVER:-1}"

require_cmd find
require_cmd make
require_cmd scp
require_cmd ssh

if [[ ! "${private_ip}" =~ ^(10\.|172\.(1[6-9]|2[0-9]|3[0-1])\.|192\.168\.) ]]; then
  echo "[networked-gdbserver] expected a private IP, got: ${private_ip}" >&2
  exit 2
fi

if [[ ! -f "${ssh_key_path}" ]]; then
  echo "[networked-gdbserver] missing SSH key: ${ssh_key_path}" >&2
  exit 1
fi

mapfile -t switch_build_dirs < <(
  find "${switch_root}" \
    -maxdepth 1 \
    -mindepth 1 \
    -type d \
    -name 'switch0-*-build' \
    -printf '%T@ %p\n' | sort -n | awk '{ $1=""; sub(/^ /, ""); print }'
)

if [[ "${#switch_build_dirs[@]}" -eq 0 ]]; then
  echo "[networked-gdbserver] no switch0 build dir found under ${switch_root}" >&2
  exit 1
fi

# Important: switch build dir suffixes are random disambiguators, not timestamps.
# We must pick the most recently modified build directory so the gdbserver
# network prepare step patches the switch produced by the latest infrasetup
# instead of an arbitrarily older lexicographic entry.
latest_index=$(( ${#switch_build_dirs[@]} - 1 ))
switch_build_dir="${switch_build_dirs[${latest_index}]}"
switch_cfg="${switch_build_dir}/switchconfig.h"
switch_bin="${switch_build_dir}/switch"
switch_slot_bin="${switch_build_dir}/switch0"

if [[ ! -f "${switch_cfg}" ]]; then
  echo "[networked-gdbserver] missing switch config: ${switch_cfg}" >&2
  exit 1
fi

numports="$(awk '$1 == "#define" && $2 == "NUMPORTS" { print $3; exit }' "${switch_cfg}")"
numdownlinks="$(awk '$1 == "#define" && $2 == "NUMDOWNLINKS" { print $3; exit }' "${switch_cfg}")"
numuplinks="$(awk '$1 == "#define" && $2 == "NUMUPLINKS" { print $3; exit }' "${switch_cfg}")"
link_id="$(awk 'match($0, /ports\[0\] = new ShmemPort\(0, "([^"]+)", false\);/, m) { print m[1]; exit }' "${switch_cfg}")"
sshport_present=0
if grep -q 'SSHPort(1)' "${switch_cfg}"; then
  sshport_present=1
fi
mac2port_size_present=0
if grep -q '#define MAC2PORT_SIZE 3' "${switch_cfg}"; then
  mac2port_size_present=1
fi
canonical_switchconfig=0
if [[ "${sshport_present}" -eq 1 && \
      "${numports}" == "2" && \
      "${numdownlinks}" == "1" && \
      "${numuplinks}" == "1" && \
      "${mac2port_size_present}" -eq 1 ]]; then
  canonical_switchconfig=1
fi

if [[ "${sshport_present}" -eq 0 ]]; then
  if [[ "${numports}" != "1" || "${numdownlinks}" != "1" || "${numuplinks}" != "0" ]]; then
    echo "[networked-gdbserver] unexpected switch0 topology in ${switch_cfg}" >&2
    echo "[networked-gdbserver] expected stock example_1config switch0 before SSHPort patch" >&2
    echo "[networked-gdbserver] got NUMPORTS=${numports:-missing} NUMDOWNLINKS=${numdownlinks:-missing} NUMUPLINKS=${numuplinks:-missing}" >&2
    exit 1
  fi
else
  if [[ "${numports}" != "2" ]]; then
    echo "[networked-gdbserver] switch0 already contains SSHPort but NUMPORTS is not example_1config-compatible in ${switch_cfg}" >&2
    echo "[networked-gdbserver] got NUMPORTS=${numports:-missing} NUMDOWNLINKS=${numdownlinks:-missing} NUMUPLINKS=${numuplinks:-missing}" >&2
    exit 1
  fi
fi

if [[ -z "${link_id}" ]]; then
  echo "[networked-gdbserver] failed to extract switch0 shmem link id from ${switch_cfg}" >&2
  exit 1
fi

if [[ "${canonical_switchconfig}" -eq 0 ]]; then
  cat > "${switch_cfg}" <<EOF
// THIS FILE IS MACHINE GENERATED. SEE deploy/buildtools/switchmodelconfig.py

#ifdef NUMCLIENTSCONFIG
#define NUMPORTS 2
#define NUMDOWNLINKS 1
#define NUMUPLINKS 1
#endif
#ifdef PORTSETUPCONFIG
ports[0] = new ShmemPort(0, "${link_id}", false);
ports[1] = new SSHPort(1);

#endif

#ifdef MACPORTSCONFIG
#define MAC2PORT_SIZE 3
uint16_t mac2port[3]  {1, 1, 0};
#endif
EOF
fi

make -C "${switch_build_dir}"
cp "${switch_bin}" "${switch_slot_bin}"

scp_base=(
  scp
  -i "${ssh_key_path}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
)
ssh_base=(
  ssh
  -i "${ssh_key_path}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
  "${ssh_user}@${private_ip}"
)

remote_preflight="$(cat <<EOF
set -euo pipefail
if pgrep -x FireSim-f2 >/dev/null 2>&1 || pgrep -x switch0 >/dev/null 2>&1; then
  echo "[networked-gdbserver] remote FireSim/switch process is already running" >&2
  echo "[networked-gdbserver] run this after infrasetup and before runworkload; hot-patching switch0 can kill the FPGA simulator" >&2
  ps -eo pid,ppid,stat,etime,cmd | grep -E 'FireSim-f2|switch0' | grep -v grep >&2 || true
  exit 1
fi
EOF
)"

if [[ "${FIRESIM_NETWORK_PREPARE_ALLOW_RUNNING:-0}" != "1" ]]; then
  "${ssh_base[@]}" "${remote_preflight}"
fi

"${scp_base[@]}" "${switch_slot_bin}" "${ssh_user}@${private_ip}:${remote_switch_tmp}"

remote_cmd="$(cat <<EOF
set -euo pipefail
if [[ ! -f "${remote_switch_tmp}" ]]; then
  echo "[networked-gdbserver] missing uploaded switch temp file: ${remote_switch_tmp}" >&2
  exit 1
fi
chmod +x "${remote_switch_tmp}"
mv -f "${remote_switch_tmp}" "${remote_switch_path}"
sudo ip tuntap add mode tap dev tap0 user "${tap_user}" 2>/dev/null || true
sudo ip link set dev tap0 address "${remote_tap_mac}"
sudo ip link set dev tap0 up
if ! ip -o addr show dev tap0 | grep -q ' ${remote_tap_addr//\//\\/}\$'; then
  sudo ip addr flush dev tap0
  sudo ip addr add "${remote_tap_addr}" dev tap0
fi
sudo sysctl -w net.ipv6.conf.tap0.disable_ipv6=1 >/dev/null
if [[ "${kill_hw_server}" == "1" ]]; then
  sudo pkill -x hw_server 2>/dev/null || true
fi
ip -brief link show tap0
ip -brief addr show dev tap0
ps -eo pid,comm,args | grep -E '(^|[[:space:]])(hw_server|virtual_jtag)([[:space:]]|$)' | grep -v grep || true
EOF
)"

"${ssh_base[@]}" "${remote_cmd}"

echo "[networked-gdbserver] switch_build_dir=${switch_build_dir}"
echo "[networked-gdbserver] link_id=${link_id}"
echo "[networked-gdbserver] remote_switch_path=${remote_switch_path}"
echo "[networked-gdbserver] remote_tap_addr=${remote_tap_addr}"
echo "[networked-gdbserver] kill_hw_server=${kill_hw_server}"
echo "[networked-gdbserver] PASS private_ip=${private_ip}"
