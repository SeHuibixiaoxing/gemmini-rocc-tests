#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"

build_name="${ROCKET_SINGLECORE_NIC_BUILD_NAME:-firesim_rocket_singlecore_nic_notrace_30mhz}"
built_hwdb_entry="${ROCKET_SINGLECORE_NIC_BUILT_HWDB_ENTRY:-${cy_dir}/sims/firesim/deploy/built-hwdb-entries/${build_name}}"
runtime_cfg="${ROCKET_SINGLECORE_NIC_RUNTIME_CFG:-${cy_dir}/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_relaxed_noprint_preserve_30mhz.yaml}"
build_cfg="${ROCKET_SINGLECORE_NIC_BUILD_CFG:-${cy_dir}/sims/firesim/deploy/config_build_f2_rocket_singlecore_nic_notrace_30mhz.yaml}"
build_recipes_cfg="${ROCKET_SINGLECORE_NIC_BUILD_RECIPES_CFG:-${cy_dir}/sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml}"
hwdb_cfg="${ROCKET_SINGLECORE_NIC_HWDB_CFG:-${cy_dir}/sims/firesim/deploy/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz.yaml}"
firesim_workload_cfg="${ROCKET_SINGLECORE_NIC_FIRESIM_WORKLOAD_CFG:-${cy_dir}/sims/firesim/deploy/workloads/rocket-singlecore-nic-gdbserver-smoke.json}"
marshal_workload_json="${ROCKET_SINGLECORE_NIC_MARSHAL_WORKLOAD_JSON:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/rocket-singlecore-nic-gdbserver-smoke.json}"
network_prepare_script="${ROCKET_SINGLECORE_NIC_NETWORK_PREPARE_SCRIPT:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/prepare_firesim_networked_gdbserver.sh}"
expected_driver_string="${ROCKET_SINGLECORE_NIC_EXPECTED_DRIVER_STRING:-IceNet TX coherent bounce enabled; checksum offload and SG disabled}"
state_dir="${cy_dir}/tmp/firesim-aws-f2/tmux"
workload_name="rocket-singlecore-nic-gdbserver-smoke"
image_path="${cy_dir}/software/firemarshal/images/firechip/${workload_name}/${workload_name}.img"
bootbinary_path="${cy_dir}/software/firemarshal/images/firechip/${workload_name}/${workload_name}-bin"
run_farm_tag="$(awk '/run_farm_tag:/ { print $2; exit }' "${runtime_cfg}")"

usage() {
  cat <<EOF
Usage: $(basename "$0") <command> [private-ip]

Commands:
  show                  Print workflow configuration
  sync-hwdb             Copy the latest built hwdb entry into ${hwdb_cfg}
  local-freshness       Verify the local smoke image contains gdbserver + payload
  launch                Sync hwdb, then launchrunfarm
  infrasetup            Verify local image, then run infrasetup
  current-private-ip    Print the sole running F2 private IP for this runtime config
  remote-freshness [ip] Compare local/rootfs image hash against the run host copy
  network-prepare [ip]  Patch switch0 with SSHPort and configure tap0 on the run host
  run [ip]              Remote freshness + network prepare + runworkload
  terminate             Force-terminate the run farm
EOF
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "missing required command: $1" >&2
    exit 1
  fi
}

timestamp() {
  date -u +%Y%m%d-%H%M%S
}

wait_for_exit_code() {
  local session="$1"
  local exit_file="$2"
  local rc=""
  while true; do
    if [[ -f "${exit_file}" ]]; then
      rc="$(tr -d '\n' < "${exit_file}")"
      if [[ -z "${rc}" ]]; then
        echo "empty exit code for session ${session}" >&2
        exit 1
      fi
      if [[ "${rc}" != "0" ]]; then
        echo "session ${session} failed with exit code ${rc}" >&2
        exit "${rc}"
      fi
      return 0
    fi
    if ! tmux has-session -t "${session}" 2>/dev/null; then
      echo "session ${session} ended before writing ${exit_file}" >&2
      exit 1
    fi
    sleep 5
  done
}

run_firesim_sync() {
  local subcmd="$1"
  local session="rocket-singlecore-nic-gdbserver-${subcmd}-$(timestamp)"
  local exit_file="${state_dir}/${session}.exitcode"
  shift
  "${cy_dir}/scripts/firesim-tmux-run.sh" --session-name "${session}" "${subcmd}" \
    -c "${runtime_cfg}" \
    -a "${hwdb_cfg}" \
    -r "${build_recipes_cfg}" \
    "$@"
  wait_for_exit_code "${session}" "${exit_file}"
}

run_firesim_async() {
  local subcmd="$1"
  local session="rocket-singlecore-nic-gdbserver-${subcmd}-$(timestamp)"
  shift
  "${cy_dir}/scripts/firesim-tmux-run.sh" --session-name "${session}" "${subcmd}" \
    -c "${runtime_cfg}" \
    -a "${hwdb_cfg}" \
    -r "${build_recipes_cfg}" \
    "$@"
  echo "${session}"
}

resolve_private_ip() {
  if [[ $# -ge 1 && -n "${1}" ]]; then
    printf '%s\n' "$1"
    return 0
  fi

  mapfile -t private_ips < <(
    aws ec2 describe-instances \
      --filters \
        Name=tag:fsimcluster,Values="${run_farm_tag}" \
        Name=instance-state-name,Values=running \
        Name=instance-type,Values=f2.6xlarge \
      --query 'Reservations[].Instances[].PrivateIpAddress' \
      --output text | tr '\t' '\n' | sed '/^$/d'
  )

  if [[ "${#private_ips[@]}" -ne 1 ]]; then
    echo "expected exactly one running FireSim private IP, got ${#private_ips[@]}" >&2
    printf '%s\n' "${private_ips[@]}" >&2
    exit 2
  fi

  printf '%s\n' "${private_ips[0]}"
}

sync_hwdb() {
  if [[ ! -f "${built_hwdb_entry}" ]]; then
    echo "missing built hwdb entry: ${built_hwdb_entry}" >&2
    exit 1
  fi
  cp "${built_hwdb_entry}" "${hwdb_cfg}"
  echo "synced_hwdb=${hwdb_cfg}"
  cat "${hwdb_cfg}"
}

verify_local_freshness() {
  require_cmd debugfs
  [[ -f "${image_path}" ]] || { echo "missing image: ${image_path}" >&2; exit 1; }
  [[ -f "${bootbinary_path}" ]] || { echo "missing bootbinary: ${bootbinary_path}" >&2; exit 1; }
  debugfs -R 'stat /usr/bin/gdbserver' "${image_path}" >/dev/null 2>&1 || {
    echo "missing /usr/bin/gdbserver in ${image_path}" >&2
    exit 1
  }
  debugfs -R 'stat /root/gdbserver-smoke/gdbserver-smoke' "${image_path}" >/dev/null 2>&1 || {
    echo "missing /root/gdbserver-smoke/gdbserver-smoke in ${image_path}" >&2
    exit 1
  }
  debugfs -R 'stat /firemarshal.sh' "${image_path}" >/dev/null 2>&1 || {
    echo "missing /firemarshal.sh in ${image_path}" >&2
    exit 1
  }
  if ! grep -aFq "${expected_driver_string}" "${bootbinary_path}"; then
    echo "bootbinary does not contain expected IceNet driver string: ${expected_driver_string}" >&2
    exit 1
  fi
  echo "local_freshness=pass"
  echo "image_path=${image_path}"
  echo "bootbinary_path=${bootbinary_path}"
  echo "expected_driver_string=${expected_driver_string}"
}

remote_freshness() {
  local private_ip="$1"
  local ssh_key_path="${FIRESIM_SSH_KEY_PATH:-/home/ubuntu/firesim.pem}"
  local ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
  local remote_image_path=""
  local remote_bootbinary_path=""
  local local_sha=""
  local remote_sha=""
  local local_bootbinary_sha=""
  local remote_bootbinary_sha=""

  verify_local_freshness

  mapfile -t remote_candidates < <(
    ssh -i "${ssh_key_path}" \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      "${ssh_user}@${private_ip}" \
      "find /home/ubuntu/sim_slot_0 -maxdepth 1 -type f -name '*${workload_name}*.img' | sort"
  )

  if [[ "${#remote_candidates[@]}" -ne 1 ]]; then
    echo "expected exactly one remote image candidate for ${workload_name}, got ${#remote_candidates[@]}" >&2
    printf '%s\n' "${remote_candidates[@]}" >&2
    exit 1
  fi
  remote_image_path="${remote_candidates[0]}"

  mapfile -t remote_bootbinary_candidates < <(
    ssh -i "${ssh_key_path}" \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      "${ssh_user}@${private_ip}" \
      "find /home/ubuntu/sim_slot_0 -maxdepth 1 -type f -name '*${workload_name}*-bin' | sort"
  )

  if [[ "${#remote_bootbinary_candidates[@]}" -ne 1 ]]; then
    echo "expected exactly one remote bootbinary candidate for ${workload_name}, got ${#remote_bootbinary_candidates[@]}" >&2
    printf '%s\n' "${remote_bootbinary_candidates[@]}" >&2
    exit 1
  fi
  remote_bootbinary_path="${remote_bootbinary_candidates[0]}"

  local_sha="$(sha256sum "${image_path}" | awk '{print $1}')"
  remote_sha="$(
    ssh -i "${ssh_key_path}" \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      "${ssh_user}@${private_ip}" \
      "sha256sum '${remote_image_path}' | awk '{print \$1}'"
  )"
  local_bootbinary_sha="$(sha256sum "${bootbinary_path}" | awk '{print $1}')"
  remote_bootbinary_sha="$(
    ssh -i "${ssh_key_path}" \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      "${ssh_user}@${private_ip}" \
      "sha256sum '${remote_bootbinary_path}' | awk '{print \$1}'"
  )"

  echo "remote_image=${remote_image_path}"
  echo "local_sha256=${local_sha}"
  echo "remote_sha256=${remote_sha}"
  echo "remote_bootbinary=${remote_bootbinary_path}"
  echo "local_bootbinary_sha256=${local_bootbinary_sha}"
  echo "remote_bootbinary_sha256=${remote_bootbinary_sha}"

  if [[ "${local_sha}" != "${remote_sha}" ]]; then
    echo "remote image SHA mismatch" >&2
    exit 1
  fi
  if [[ "${local_bootbinary_sha}" != "${remote_bootbinary_sha}" ]]; then
    echo "remote bootbinary SHA mismatch" >&2
    exit 1
  fi
  if ! ssh -i "${ssh_key_path}" \
      -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null \
      -o LogLevel=ERROR \
      "${ssh_user}@${private_ip}" \
      "grep -aFq '${expected_driver_string}' '${remote_bootbinary_path}'"; then
    echo "remote bootbinary does not contain expected IceNet driver string: ${expected_driver_string}" >&2
    exit 1
  fi
}

show_config() {
  cat <<EOF
build_name=${build_name}
built_hwdb_entry=${built_hwdb_entry}
build_cfg=${build_cfg}
build_recipes_cfg=${build_recipes_cfg}
runtime_cfg=${runtime_cfg}
hwdb_cfg=${hwdb_cfg}
firesim_workload_cfg=${firesim_workload_cfg}
marshal_workload_json=${marshal_workload_json}
image_path=${image_path}
bootbinary_path=${bootbinary_path}
run_farm_tag=${run_farm_tag}
network_prepare_script=${network_prepare_script}
expected_driver_string=${expected_driver_string}
EOF
}

if [[ $# -lt 1 ]]; then
  usage >&2
  exit 2
fi

require_cmd aws
require_cmd tmux

command_name="$1"
shift || true

case "${command_name}" in
  show)
    show_config
    ;;
  sync-hwdb)
    sync_hwdb
    ;;
  local-freshness)
    verify_local_freshness
    ;;
  launch)
    sync_hwdb
    run_firesim_sync launchrunfarm
    ;;
  infrasetup)
    verify_local_freshness
    run_firesim_sync infrasetup
    ;;
  current-private-ip)
    resolve_private_ip "${1:-}"
    ;;
  remote-freshness)
    private_ip="$(resolve_private_ip "${1:-}")"
    remote_freshness "${private_ip}"
    ;;
  network-prepare)
    private_ip="$(resolve_private_ip "${1:-}")"
    "${network_prepare_script}" "${private_ip}"
    ;;
  run)
    private_ip="$(resolve_private_ip "${1:-}")"
    remote_freshness "${private_ip}"
    "${network_prepare_script}" "${private_ip}"
    run_firesim_async runworkload
    ;;
  terminate)
    run_firesim_sync terminaterunfarm --forceterminate
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
