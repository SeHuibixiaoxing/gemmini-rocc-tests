#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage:
  capture_firesim_nic_debug_scene.sh start-tcpdump <run-host-private-ip> [tag]
  capture_firesim_nic_debug_scene.sh capture <run-host-private-ip> [tag] [outdir]

Start a run-host tap0 capture, or archive and copy back the current FireSim NIC
debug scene without terminating the run farm.

Environment:
  FIRESIM_SSH_KEY_PATH          default: /home/ubuntu/firesim.pem
  FIRESIM_SSH_USER              default: ubuntu
  FIRESIM_NIC_TCPDUMP_SECONDS   default: 1800
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "[nic-scene] missing required command: $1" >&2
    exit 1
  fi
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" || "$1" == "help" ) ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 ]]; then
  usage >&2
  exit 2
fi

mode="$1"
private_ip="$2"
tag="${3:-$(date -u +%Y%m%dT%H%M%SZ)}"
outdir="${4:-$(pwd)}"
ssh_key_path="${FIRESIM_SSH_KEY_PATH:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
tcpdump_seconds="${FIRESIM_NIC_TCPDUMP_SECONDS:-1800}"

require_cmd ssh
require_cmd scp
require_cmd mkdir

if [[ ! "${private_ip}" =~ ^(10\.|172\.(1[6-9]|2[0-9]|3[0-1])\.|192\.168\.) ]]; then
  echo "[nic-scene] expected a private IP, got: ${private_ip}" >&2
  exit 2
fi

if [[ ! -f "${ssh_key_path}" ]]; then
  echo "[nic-scene] missing SSH key: ${ssh_key_path}" >&2
  exit 1
fi

if [[ ! "${tag}" =~ ^[A-Za-z0-9._-]+$ ]]; then
  echo "[nic-scene] tag must contain only [A-Za-z0-9._-], got: ${tag}" >&2
  exit 2
fi

ssh_base=(
  ssh
  -i "${ssh_key_path}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
  "${ssh_user}@${private_ip}"
)

scp_base=(
  scp
  -i "${ssh_key_path}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
)

remote_prefix="/home/${ssh_user}/nic-debug-${tag}"
remote_pcap="${remote_prefix}.tap0.pcap"
remote_tcpdump_log="${remote_prefix}.tcpdump.log"
remote_tcpdump_pid="${remote_prefix}.tcpdump.pid"
remote_summary="${remote_prefix}.summary.txt"
remote_tar="${remote_prefix}.tgz"
remote_tar_err="${remote_prefix}.tar.err"

case "${mode}" in
  start-tcpdump)
    "${ssh_base[@]}" "set -euo pipefail
rm -f '${remote_pcap}' '${remote_tcpdump_log}' '${remote_tcpdump_pid}'
nohup sudo timeout '${tcpdump_seconds}' tcpdump -i tap0 -n -s 0 -U -w '${remote_pcap}' >'${remote_tcpdump_log}' 2>&1 < /dev/null &
echo \$! > '${remote_tcpdump_pid}'
echo '[nic-scene] tcpdump_pid='\"\$(cat '${remote_tcpdump_pid}')\"
ip -s link show tap0 || true
ip neigh show dev tap0 || true"
    echo "[nic-scene] started tcpdump on ${private_ip}: ${remote_pcap}"
    ;;

  capture)
    mkdir -p "${outdir}"
    "${ssh_base[@]}" "set -euo pipefail
summary='${remote_summary}'
{
  echo '## date'
  date -u
  echo
  echo '## hostname'
  hostname || true
  echo
  echo '## screen -ls'
  screen -ls || true
  echo
  echo '## sim_slot_0 listing'
  ls -lah /home/${ssh_user}/sim_slot_0 2>&1 || true
  echo
  echo '## switch_slot_0 listing'
  ls -lah /home/${ssh_user}/switch_slot_0 2>&1 || true
  echo
  echo '## FireSim processes'
  ps -eo pid,ppid,stat,etime,cmd | grep -E 'FireSim|switch0|screen|tcpdump' | grep -v grep || true
  echo
  echo '## tap0 link'
  ip -s link show tap0 2>&1 || true
  echo
  echo '## tap0 addr'
  ip addr show dev tap0 2>&1 || true
  echo
  echo '## tap0 neigh'
  ip neigh show dev tap0 2>&1 || true
  echo
  echo '## uartlog tail'
  tail -300 /home/${ssh_user}/sim_slot_0/uartlog 2>&1 || true
  echo
  echo '## heartbeat tail'
  tail -80 /home/${ssh_user}/sim_slot_0/heartbeat.csv 2>&1 || true
  echo
  echo '## switchlog tail'
  tail -200 /home/${ssh_user}/switch_slot_0/switchlog 2>&1 || true
  echo
  echo '## tcpdump log tail'
  tail -100 '${remote_tcpdump_log}' 2>&1 || true
} > \"\${summary}\"

shopt -s nullglob
files=(
  \"\${summary}\"
  /home/${ssh_user}/sim_slot_0/uartlog
  /home/${ssh_user}/sim_slot_0/heartbeat.csv
  /home/${ssh_user}/sim_slot_0/niclog0
  /home/${ssh_user}/sim_slot_0/metasim_stderr.out
  /home/${ssh_user}/sim_slot_0/AUTOCOUNTERFILE*
  /home/${ssh_user}/switch_slot_0/switchlog
  '${remote_pcap}'
  '${remote_tcpdump_log}'
  '${remote_tcpdump_pid}'
)
tar --ignore-failed-read -czf '${remote_tar}' \"\${files[@]}\" >'${remote_tar_err}' 2>&1 || true
ls -lh '${remote_tar}' '${remote_summary}' '${remote_tar_err}' 2>&1 || true"
    "${scp_base[@]}" "${ssh_user}@${private_ip}:${remote_tar}" "${outdir}/"
    echo "[nic-scene] copied ${private_ip}:${remote_tar} to ${outdir}/"
    ;;

  -h|--help|help)
    usage
    ;;

  *)
    usage >&2
    exit 2
    ;;
esac
