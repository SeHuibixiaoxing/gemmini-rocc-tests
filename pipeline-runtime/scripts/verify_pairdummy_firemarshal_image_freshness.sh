#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: verify_pairdummy_firemarshal_image_freshness.sh <workload-json>

Verify that the local FireMarshal image for the pairdummy pipeline-runtime
workload contains the current workload run script, file-only wrapper, runner
script, runtime binary, and generated firemarshal environment.
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
fallback_entrypoint_src="${cy_dir}/software/firemarshal/boards/firechip/distros/br/overlay/firemarshal.sh"
wrapper_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh"
runner_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh"
runtime_bin_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux"
ptrace_probe_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_ptrace_peek-linux"
guest_env_src="${PAIRDUMMY_GUEST_ENV_OVERRIDE_PATH:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/firemarshal.env}"
gdbserver_expect="${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}"
local_gdb_expect="${PIPELINE_RUNTIME_LOCAL_GDB_ENABLE:-0}"
workload_run_src="$(
  python3 - "${workload_json}" <<'PY'
import json
import pathlib
import shlex
import sys

workload_path = pathlib.Path(sys.argv[1])
with workload_path.open() as f:
    workload = json.load(f)
run = workload.get("run", "")
parts = shlex.split(run)
if not parts:
    sys.exit(0)
run_path = pathlib.Path(parts[0])
if not run_path.is_absolute():
    run_path = (workload_path.parent / run_path).resolve()
print(run_path)
PY
)"

require_file() {
  local path="$1"
  if [[ ! -f "${path}" ]]; then
    echo "[image-freshness] missing required file: ${path}" >&2
    exit 1
  fi
}

require_file "${workload_json}"
require_file "${image_path}"
if [[ -n "${workload_run_src}" ]]; then
  require_file "${workload_run_src}"
else
  require_file "${fallback_entrypoint_src}"
fi
require_file "${wrapper_src}"
require_file "${runner_src}"
require_file "${runtime_bin_src}"
require_file "${ptrace_probe_src}"
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
if [[ -n "${workload_run_src}" ]]; then
  verify_match workload-run-entrypoint "${workload_run_src}" /firemarshal.sh || fail=1
else
  verify_match firemarshal-entrypoint "${fallback_entrypoint_src}" /firemarshal.sh || fail=1
fi
verify_match fileonly-wrapper "${wrapper_src}" /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh || fail=1
verify_match runner-script "${runner_src}" /root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh || fail=1
verify_match runtime-binary "${runtime_bin_src}" /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux || fail=1
verify_match ptrace-probe "${ptrace_probe_src}" /root/rerocc-linux-tests/rerocc_ptrace_peek-linux || fail=1
verify_match firemarshal-env "${guest_env_src}" /firemarshal.env || fail=1
if [[ "${gdbserver_expect}" != "0" ]]; then
  extract_from_image /usr/bin/gdbserver "${tmpdir}/gdbserver-from-image" || fail=1
  if [[ ! -s "${tmpdir}/gdbserver-from-image" ]]; then
    echo "[image-freshness] FAIL gdbserver missing or empty in image" >&2
    fail=1
  else
    echo "[image-freshness] OK gdbserver present in image"
  fi
fi
if [[ "${local_gdb_expect}" != "0" ]]; then
  extract_from_image /usr/bin/gdb "${tmpdir}/gdb-from-image" || fail=1
  if [[ ! -s "${tmpdir}/gdb-from-image" ]]; then
    echo "[image-freshness] FAIL gdb missing or empty in image" >&2
    fail=1
  else
    echo "[image-freshness] OK gdb present in image"
  fi
fi

extract_from_image /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux "${tmpdir}/runtime-binary-from-image" || fail=1

if [[ "${fail}" -ne 0 ]]; then
  exit 1
fi

debugfs -R 'cat /firemarshal.env' "${image_path}" | \
  egrep 'PIPELINE_RUNTIME_(PROFILE_ID|UART_LOG_ENABLE|GUEST_LOG_ENABLE|GUEST_DEEP_LOG_ENABLE|YAML_LINE_LOG_ENABLE|AUDIT_LOG_ENABLE|STDIO_CAPTURE_MODE|MLOCKALL_MODE|GDBSERVER_ENABLE|GDBSERVER_BIND_ADDR|GDBSERVER_PORT|GDBSERVER_INFO_PATH|GDBSERVER_LOG_PATH|GDB_MARKER_ENABLE|GDB_MARKER_SITE|GDB_MARKER_SEGMENT|GDB_MARKER_GLOBAL_STAGE|GDB_MARKER_LOCAL_STAGE|GDB_MARKER_SUBBATCH|GDB_MARKER_MANAGER|GDB_MARKER_TENSOR|GDB_MARKER_PAGE|GDB_MARKER_TOKEN|LOCAL_GDB_ENABLE|LOCAL_GDB_INFO_PATH|LOCAL_GDB_LOG_PATH|LOCAL_GDB_TIMEOUT_SECS|LOCAL_GDB_CONTINUE_AFTER_MAIN|NO_DMA_COMPUTE_ENABLE|DMA_FORCE_DIRECT_ENABLE|DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE|DMA_BOUNCE_BYPASS_ENABLE|DMA_EXPORT_PROBE_ENABLE|DMA_EXPORT_PROBE_TOKEN_START|DMA_EXPORT_PROBE_TOKEN_END|DMA_EXPORT_PAGE_START|DMA_EXPORT_PAGE_END|DMA_FIXED_LOAD_PROBE_ENABLE|DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE|DMA_FIXED_LOAD_PRE_SRC_NOPS|DMA_FIXED_LOAD_PROBE_TOKEN_START|DMA_FIXED_LOAD_PROBE_TOKEN_END|DMA_FIXED_LOAD_PAGE_START|DMA_FIXED_LOAD_PAGE_END|DMA_FIXED_LOAD_CHECKPOINT_ENABLE|BREADCRUMB_ENABLE|BREADCRUMB_PATH|BREADCRUMB_SEGMENT|BREADCRUMB_GLOBAL_STAGE|BREADCRUMB_LOCAL_STAGE|BREADCRUMB_SUBBATCH|DEBUG_TRIGGER_ENABLE|DEBUG_TRIGGER_KIND|DEBUG_TRIGGER_SEGMENT|DEBUG_TRIGGER_GLOBAL_STAGE|DEBUG_TRIGGER_LOCAL_STAGE|DEBUG_TRIGGER_SUBBATCH|DEBUG_TRIGGER_MANAGER|DEBUG_TRIGGER_TENSOR_ID|DEBUG_TRIGGER_PAGE|DEBUG_TRIGGER_TOKEN|DEBUG_TRIGGER_PRE_RING|DEBUG_TRIGGER_POST_BUDGET|DEBUG_TRIGGER_MATCH_ONCE|DEBUG_TRIGGER_LOG_PATH)|PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE' || true

echo "[image-freshness] PASS workload=${workload_name} image=${image_path}"
