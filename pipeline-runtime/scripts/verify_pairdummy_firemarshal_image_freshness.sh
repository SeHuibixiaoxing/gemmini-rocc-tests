#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: verify_pairdummy_firemarshal_image_freshness.sh <workload-json>

Verify that the local FireMarshal image for the pairdummy pipeline-runtime
workload contains the current host-side firemarshal entrypoint, file-only
wrapper, runner script, runtime binary, and generated firemarshal environment.
EOF
}

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
workload_json="$1"
if [[ "${workload_json}" != /* ]]; then
  workload_json="${cy_dir}/${workload_json}"
fi

workload_name="$(basename "${workload_json}" .json)"
image_path="${cy_dir}/software/firemarshal/images/firechip/${workload_name}/${workload_name}.img"
entrypoint_src="${cy_dir}/software/firemarshal/boards/firechip/distros/br/overlay/firemarshal.sh"
wrapper_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh"
runner_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh"
runtime_bin_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
guest_env_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/firemarshal.env"

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "[image-freshness] missing required file: ${path}" >&2
    exit 1
  fi
}

require_file "${workload_json}"
require_file "${image_path}"
require_file "${entrypoint_src}"
require_file "${wrapper_src}"
require_file "${runner_src}"
require_file "${runtime_bin_src}"
require_file "${guest_env_src}"

tmpdir="$(mktemp -d)"
trap 'rm -rf "${tmpdir}"' EXIT

extract_from_image() {
  local guest_path="$1"
  local out_path="$2"
  if ! debugfs -R "cat ${guest_path}" "${image_path}" > "${out_path}" 2>/dev/null; then
    echo "[image-freshness] failed to extract ${guest_path} from ${image_path}" >&2
    return 1
  fi
}

verify_match() {
  local label="$1"
  local src_path="$2"
  local guest_path="$3"
  local out_path="${tmpdir}/$(basename "${label}")"
  local src_sha=""
  local img_sha=""

  extract_from_image "${guest_path}" "${out_path}"
  src_sha="$(sha256sum "${src_path}" | awk '{print $1}')"
  img_sha="$(sha256sum "${out_path}" | awk '{print $1}')"

  if ! cmp -s "${src_path}" "${out_path}"; then
    echo "[image-freshness] FAIL ${label} src_sha=${src_sha} img_sha=${img_sha}" >&2
    echo "[image-freshness] src=${src_path}" >&2
    echo "[image-freshness] guest=${guest_path}" >&2
    return 1
  fi

  echo "[image-freshness] OK ${label} sha256=${src_sha}"
}

fail=0
verify_match firemarshal-entrypoint "${entrypoint_src}" /firemarshal.sh || fail=1
verify_match fileonly-wrapper "${wrapper_src}" /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh || fail=1
verify_match runner-script "${runner_src}" /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh || fail=1
verify_match runtime-binary "${runtime_bin_src}" /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux || fail=1
verify_match firemarshal-env "${guest_env_src}" /firemarshal.env || fail=1

if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi

debugfs -R 'cat /firemarshal.env' "${image_path}" | \
  egrep 'PIPELINE_RUNTIME_(PROFILE_ID|UART_LOG_ENABLE|GUEST_LOG_ENABLE|GUEST_DEEP_LOG_ENABLE|AUDIT_LOG_ENABLE|STDIO_CAPTURE_MODE|MLOCKALL_MODE|DMA_EXPORT_PROBE_ENABLE|DMA_EXPORT_PROBE_TOKEN_START|DMA_EXPORT_PROBE_TOKEN_END|DMA_EXPORT_PAGE_START|DMA_EXPORT_PAGE_END|DMA_FIXED_LOAD_PROBE_ENABLE|DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE|DMA_FIXED_LOAD_PRE_SRC_NOPS|DMA_FIXED_LOAD_PROBE_TOKEN_START|DMA_FIXED_LOAD_PROBE_TOKEN_END|DMA_FIXED_LOAD_PAGE_START|DMA_FIXED_LOAD_PAGE_END|DMA_FIXED_LOAD_CHECKPOINT_ENABLE|BREADCRUMB_ENABLE|BREADCRUMB_PATH|BREADCRUMB_SEGMENT|BREADCRUMB_GLOBAL_STAGE|BREADCRUMB_LOCAL_STAGE|BREADCRUMB_SUBBATCH|DEBUG_TRIGGER_ENABLE|DEBUG_TRIGGER_KIND|DEBUG_TRIGGER_SEGMENT|DEBUG_TRIGGER_GLOBAL_STAGE|DEBUG_TRIGGER_LOCAL_STAGE|DEBUG_TRIGGER_SUBBATCH|DEBUG_TRIGGER_MANAGER|DEBUG_TRIGGER_TENSOR_ID|DEBUG_TRIGGER_PAGE|DEBUG_TRIGGER_TOKEN|DEBUG_TRIGGER_PRE_RING|DEBUG_TRIGGER_POST_BUDGET|DEBUG_TRIGGER_MATCH_ONCE|DEBUG_TRIGGER_LOG_PATH)|PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE' || true

echo "[image-freshness] PASS workload=${workload_name} image=${image_path}"
