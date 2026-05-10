#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: verify_pairdummy_firesim_remote_image_freshness.sh <workload-json> <private-ip> [remote-image-path]

Verify that the FireSim run host is using the same pairdummy image as the
locally verified FireMarshal image.

The SSH target must be a private IP address. This helper intentionally refuses
to use public addressing.
EOF
}

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
workload_json="$1"
private_ip="$2"
remote_image_path="${3:-}"
local_verify_script="${script_dir}/verify_pairdummy_firemarshal_image_freshness.sh"
ssh_key_path="${FIRESIM_SSH_KEY_PATH:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"

if [[ "${workload_json}" != /* ]]; then
  workload_json="${cy_dir}/${workload_json}"
fi

if [[ ! "${private_ip}" =~ ^(10\.|172\.(1[6-9]|2[0-9]|3[0-1])\.|192\.168\.) ]]; then
  echo "[remote-image-freshness] expected a private IP, got: ${private_ip}" >&2
  exit 2
fi

if [[ ! -x "${local_verify_script}" ]]; then
  echo "[remote-image-freshness] missing local verify script: ${local_verify_script}" >&2
  exit 1
fi

if [[ ! -f "${ssh_key_path}" ]]; then
  echo "[remote-image-freshness] missing SSH key: ${ssh_key_path}" >&2
  exit 1
fi

workload_name="$(basename "${workload_json}" .json)"
local_image_path="${cy_dir}/software/firemarshal/images/firechip/${workload_name}/${workload_name}.img"

if [[ ! -f "${local_image_path}" ]]; then
  echo "[remote-image-freshness] missing local image: ${local_image_path}" >&2
  exit 1
fi

"${local_verify_script}" "${workload_json}"

ssh_base=(
  ssh
  -i "${ssh_key_path}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
  "${ssh_user}@${private_ip}"
)

if [[ -z "${remote_image_path}" ]]; then
  mapfile -t remote_candidates < <(
    "${ssh_base[@]}" \
      "find /home/ubuntu/sim_slot_0 -maxdepth 1 -type f -name '*${workload_name}*.img' | sort"
  )
  if [[ "${#remote_candidates[@]}" -ne 1 ]]; then
    echo "[remote-image-freshness] expected exactly one remote image candidate for ${workload_name}, got ${#remote_candidates[@]}" >&2
    printf '%s\n' "${remote_candidates[@]}" >&2
    exit 1
  fi
  remote_image_path="${remote_candidates[0]}"
fi

local_sha="$(sha256sum "${local_image_path}" | awk '{print $1}')"
remote_sha="$("${ssh_base[@]}" "sha256sum '${remote_image_path}' | awk '{print \$1}'")"

echo "[remote-image-freshness] local_image=${local_image_path}"
echo "[remote-image-freshness] remote_image=${remote_image_path}"
echo "[remote-image-freshness] local_sha256=${local_sha}"
echo "[remote-image-freshness] remote_sha256=${remote_sha}"

if [[ "${local_sha}" != "${remote_sha}" ]]; then
  echo "[remote-image-freshness] SHA mismatch" >&2
  exit 1
fi

"${ssh_base[@]}" \
  "debugfs -R 'cat /firemarshal.env' '${remote_image_path}' | \
    egrep 'PIPELINE_RUNTIME_(PROFILE_ID|TRACE_SUMMARY_ONLY|UART_LOG_ENABLE|GUEST_LOG_ENABLE|GUEST_DEEP_LOG_ENABLE|YAML_LINE_LOG_ENABLE|AUDIT_LOG_ENABLE|STDIO_CAPTURE_MODE|MLOCKALL_MODE|GDBSERVER_ENABLE|GDBSERVER_BIND_ADDR|GDBSERVER_PORT|GDBSERVER_INFO_PATH|GDBSERVER_LOG_PATH|GDB_MARKER_ENABLE|GDB_MARKER_SITE|GDB_MARKER_SEGMENT|GDB_MARKER_GLOBAL_STAGE|GDB_MARKER_LOCAL_STAGE|GDB_MARKER_SUBBATCH|GDB_MARKER_MANAGER|GDB_MARKER_TENSOR|GDB_MARKER_PAGE|GDB_MARKER_TOKEN|LOCAL_GDB_ENABLE|LOCAL_GDB_INFO_PATH|LOCAL_GDB_LOG_PATH|LOCAL_GDB_TIMEOUT_SECS|LOCAL_GDB_CONTINUE_AFTER_MAIN|NO_DMA_COMPUTE_ENABLE|DMA_FORCE_DIRECT_ENABLE|DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE|DMA_BOUNCE_BYPASS_ENABLE|DMA_EXPORT_PROBE_ENABLE|DMA_EXPORT_PROBE_TOKEN_START|DMA_EXPORT_PROBE_TOKEN_END|DMA_EXPORT_PAGE_START|DMA_EXPORT_PAGE_END|DMA_FIXED_LOAD_PROBE_ENABLE|DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE|DMA_FIXED_LOAD_PRE_SRC_NOPS|DMA_FIXED_LOAD_PROBE_TOKEN_START|DMA_FIXED_LOAD_PROBE_TOKEN_END|DMA_FIXED_LOAD_PAGE_START|DMA_FIXED_LOAD_PAGE_END|DMA_FIXED_LOAD_CHECKPOINT_ENABLE|BREADCRUMB_ENABLE|BREADCRUMB_PATH|BREADCRUMB_SEGMENT|BREADCRUMB_GLOBAL_STAGE|BREADCRUMB_LOCAL_STAGE|BREADCRUMB_SUBBATCH|DEBUG_TRIGGER_ENABLE|DEBUG_TRIGGER_KIND|DEBUG_TRIGGER_SEGMENT|DEBUG_TRIGGER_GLOBAL_STAGE|DEBUG_TRIGGER_LOCAL_STAGE|DEBUG_TRIGGER_SUBBATCH|DEBUG_TRIGGER_MANAGER|DEBUG_TRIGGER_TENSOR_ID|DEBUG_TRIGGER_PAGE|DEBUG_TRIGGER_TOKEN|DEBUG_TRIGGER_PRE_RING|DEBUG_TRIGGER_POST_BUDGET|DEBUG_TRIGGER_MATCH_ONCE|DEBUG_TRIGGER_LOG_PATH)|PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE' || true"

echo "[remote-image-freshness] PASS workload=${workload_name} private_ip=${private_ip}"
