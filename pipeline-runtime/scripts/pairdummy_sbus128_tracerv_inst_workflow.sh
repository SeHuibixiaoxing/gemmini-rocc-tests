#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"

# shellcheck disable=SC1091
source "${script_dir}/pairdummy_sbus128_fixed_env.sh"
export PIPELINE_RUNTIME_FIRESIM_TRACERV_WORKER_MARKERS="${PIPELINE_RUNTIME_FIRESIM_TRACERV_WORKER_MARKERS:-1}"
export PIPELINE_RUNTIME_FIRESIM_TRACERV_DMA_WINDOW_MARKERS="${PIPELINE_RUNTIME_FIRESIM_TRACERV_DMA_WINDOW_MARKERS:-1}"

workload_json="generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-tracerv-inst.json"
runtime_cfg_base="/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_tracerv_inst.yaml"
hwdb_cfg="/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml"
build_recipes_cfg="/home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml"
host_entrypoint_src="/home/ubuntu/chipyard/software/firemarshal/boards/firechip/distros/br/overlay/firemarshal.sh"
local_freshness_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh"
remote_freshness_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh"
render_guest_env_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/render_pairdummy_guest_env.sh"
patch_guest_env_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/apply_guest_env_to_image.sh"
monitor_script="scripts/firesim-prt-host-watchdog.sh"
firemarshal_state_dir="${cy_dir}/tmp/firemarshal-tmux"
firesim_state_dir="${cy_dir}/tmp/firesim-aws-f2/tmux"
effective_guest_env_dir="${cy_dir}/tmp/pipeline-runtime-effective-guest-env"
effective_runtime_cfg_dir="${cy_dir}/tmp/pipeline-runtime-effective-runtime-config"
workload_name="$(basename "${workload_json}" .json)"
local_image_path="${cy_dir}/software/firemarshal/images/firechip/${workload_name}/${workload_name}.img"
effective_guest_env_path="${effective_guest_env_dir}/${workload_name}.firemarshal.env"
effective_runtime_cfg_path="${effective_runtime_cfg_dir}/$(basename "${runtime_cfg_base}")"
tracing_start_inst="${PAIRDUMMY_TRACERV_START_INST:-ffffffff00008013}"
tracing_end_inst="${PAIRDUMMY_TRACERV_END_INST:-ffffffff00010013}"
run_farm_tag="$(awk '/run_farm_tag:/ { print $2; exit }' "${runtime_cfg_base}")"

usage() {
  cat <<'EOF'
Usage: pairdummy_sbus128_tracerv_inst_workflow.sh <command> [private-ip]

Instruction-triggered TracerV debug workflow for the 12-pair sbus128 dummy-model path.
This helper keeps the canonical guest/runtime semantics, but:
  - enables instruction-triggered TracerV capture
  - emits worker-local TracerV trigger instructions from the runtime binary
  - copies back TRACEFILE*
  - uses an isolated workload name and run_farm_tag

Commands:
  show                  Print the fixed paths, profile values, and TracerV settings
  debug-preflight       Check probe tier / trigger conflicts before a rerun
  image-closure         marshal clean -> build -> install -> local freshness
  marshal-clean         Run marshal clean for the fixed workload
  marshal-build         Run marshal build for the fixed workload
  marshal-install       Run marshal install for the fixed workload
  local-freshness       Verify the local FireMarshal image contents
  launch                Launch the FireSim run farm
  infrasetup            Run FireSim infrasetup
  remote-freshness [ip] Verify remote image freshness over a private IP
  run [ip]              Remote freshness, then runworkload with the host watchdog
  terminate             Force-terminate the run farm
  current-private-ip    Print the sole running private IP if exactly one exists
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

require_nonempty() {
  local value="$1"
  local label="$2"
  if [[ -z "${value}" ]]; then
    echo "missing required value: ${label}" >&2
    exit 1
  fi
}

resolve_private_ip() {
  if [[ $# -ge 1 && -n "${1}" ]]; then
    printf '%s\n' "$1"
    return 0
  fi

  require_nonempty "${run_farm_tag}" "run_farm_tag from ${runtime_cfg_base}"

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

render_effective_guest_env() {
  mkdir -p "${effective_guest_env_dir}"
  "${render_guest_env_script}" "${effective_guest_env_path}"
  export PAIRDUMMY_GUEST_ENV_OVERRIDE_PATH="${effective_guest_env_path}"
}

patch_local_image_guest_env() {
  render_effective_guest_env
  "${patch_guest_env_script}" "${local_image_path}" "${effective_guest_env_path}"
}

render_effective_runtime_cfg() {
  mkdir -p "${effective_runtime_cfg_dir}"
  python3 - "${runtime_cfg_base}" "${effective_runtime_cfg_path}" "${tracing_start_inst}" "${tracing_end_inst}" <<'PY'
from pathlib import Path
import re
import sys

src = Path(sys.argv[1]).read_text()
dst = Path(sys.argv[2])
start = sys.argv[3]
end = sys.argv[4]

src = re.sub(r'(?m)^  start:\s*".*"$', f'  start: "{start}"', src, count=1)
src = re.sub(r'(?m)^  end:\s*".*"$', f'  end: "{end}"', src, count=1)
dst.write_text(src)
PY
}

verify_local_freshness_effective() {
  render_effective_guest_env
  "${local_freshness_script}" "${workload_json}"
}

run_firemarshal() {
  local subcmd="$1"
  local session="pairdummy-sbus128-tracerv-inst-${subcmd}-$(timestamp)"
  local exit_file="${firemarshal_state_dir}/${session}.exitcode"
  "${cy_dir}/scripts/firemarshal-tmux-run.sh" --session-name "${session}" "${subcmd}" "${workload_json}"
  wait_for_exit_code "${session}" "${exit_file}"
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
  local session="pairdummy-sbus128-tracerv-inst-${subcmd}-$(timestamp)"
  local exit_file="${firesim_state_dir}/${session}.exitcode"
  shift
  render_effective_runtime_cfg
  "${cy_dir}/scripts/firesim-tmux-run.sh" --session-name "${session}" "${subcmd}" \
    -c "${effective_runtime_cfg_path}" \
    -a "${hwdb_cfg}" \
    -r "${build_recipes_cfg}" \
    "$@"
  wait_for_exit_code "${session}" "${exit_file}"
}

run_firesim_async() {
  local subcmd="$1"
  local session="pairdummy-sbus128-tracerv-inst-${subcmd}-$(timestamp)"
  shift
  render_effective_runtime_cfg
  "${cy_dir}/scripts/firesim-tmux-run.sh" --session-name "${session}" "${subcmd}" \
    -c "${effective_runtime_cfg_path}" \
    -a "${hwdb_cfg}" \
    -r "${build_recipes_cfg}" \
    "$@"
}

show_config() {
  cat <<EOF
profile_id=${PIPELINE_RUNTIME_PROFILE_ID}
workload_json=${workload_json}
runtime_cfg_base=${runtime_cfg_base}
runtime_cfg_effective=${effective_runtime_cfg_path}
hwdb_cfg=${hwdb_cfg}
build_recipes_cfg=${build_recipes_cfg}
host_entrypoint_src=${host_entrypoint_src}
run_farm_tag=${run_farm_tag}
local_freshness_script=${local_freshness_script}
remote_freshness_script=${remote_freshness_script}
guest_entrypoint=/firemarshal.sh
note=instruction-triggered tracerv via worker-local runtime markers
tracing_enable=yes
tracing_output_format=0
tracing_selector=3
tracing_start_inst=${tracing_start_inst}
tracing_end_inst=${tracing_end_inst}
firesim_live_idle_timeout_seconds=${FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS:-}
runtime_worker_markers=${PIPELINE_RUNTIME_FIRESIM_TRACERV_WORKER_MARKERS}
runtime_dma_window_markers=${PIPELINE_RUNTIME_FIRESIM_TRACERV_DMA_WINDOW_MARKERS}
runtime_dma_page_start=${PIPELINE_RUNTIME_DMA_TRACERV_PAGE_START:-unset}
runtime_dma_page_end=${PIPELINE_RUNTIME_DMA_TRACERV_PAGE_END:-unset}
methods=${METHODS}
dummy_gemmini_mode=${DUMMY_GEMMINI_MODE}
skip_model_bin_load=${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}
skip_input_load=${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}
skip_golden_check=${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}
num_cores=${NUM_CORES}
num_gemmini=${NUM_GEMMINI}
num_dma=${NUM_DMA}
pair_manager_mode=${PAIR_MANAGER_MODE}
mlockall_mode=${PIPELINE_RUNTIME_MLOCKALL_MODE}
log_profile=${PIPELINE_RUNTIME_LOG_PROFILE}
stdio_capture_mode=${PIPELINE_RUNTIME_STDIO_CAPTURE_MODE}
uart_log_enable=${PIPELINE_RUNTIME_UART_LOG_ENABLE}
guest_log_enable=${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}
guest_deep_log_enable=${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}
checkpoint_log_enable=${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE}
disable_mapping_cache=${PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE}
breadcrumb_enable=${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}
debug_trigger_enable=${PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE}
debug_trigger_kind=${PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND:-any}
EOF
}

debug_preflight() {
  local probe_tier="1"
  local high_risk_count=0
  local fail=0
  local notes=()

  if [[ "${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}" != "0" || "${PIPELINE_RUNTIME_GUEST_LOG_ENABLE}" != "0" ]]; then
    probe_tier="2"
  fi

  if [[ "${PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE}" != "0" ]]; then
    probe_tier="3"
    high_risk_count=$((high_risk_count + 1))
  fi
  if [[ "${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}" != "0" || "${DEEP_LOG_ENABLE:-0}" != "0" ]]; then
    probe_tier="3"
    high_risk_count=$((high_risk_count + 1))
  fi
  if [[ "${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE}" != "0" || "${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE}" != "0" || "${PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE}" != "0" ]]; then
    probe_tier="3"
    high_risk_count=$((high_risk_count + 1))
  fi
  if [[ "${PIPELINE_RUNTIME_CRITICAL_UART_PROBE}" != "0" ]]; then
    probe_tier="3"
    notes+=("critical-uart-probe is enabled")
  fi

  if [[ "${high_risk_count}" -gt 1 ]]; then
    fail=1
    notes+=("multiple high-risk probe families are enabled together")
  fi

  echo "debug_preflight_profile_id=${PIPELINE_RUNTIME_PROFILE_ID}"
  echo "debug_preflight_probe_tier=${probe_tier}"
  echo "debug_preflight_change_kind_required=manual-record"
  echo "debug_preflight_control_rerun_required_if_observability_only=1"
  if [[ "${#notes[@]}" -eq 0 ]]; then
    echo "debug_preflight_notes=none"
  else
    printf 'debug_preflight_note=%s\n' "${notes[@]}"
  fi
  echo "debug_preflight_recommended_order=artifact-audit -> debug-preflight -> instruction-trigger-tracerv-rerun -> trace-analysis"

  if [[ "${fail}" -ne 0 ]]; then
    echo "debug_preflight_status=fail" >&2
    return 1
  fi
  echo "debug_preflight_status=pass"
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
  debug-preflight)
    debug_preflight
    ;;
  image-closure)
    run_firemarshal clean
    run_firemarshal build
    run_firemarshal install
    patch_local_image_guest_env
    verify_local_freshness_effective
    ;;
  marshal-clean)
    run_firemarshal clean
    ;;
  marshal-build)
    run_firemarshal build
    ;;
  marshal-install)
    run_firemarshal install
    ;;
  local-freshness)
    verify_local_freshness_effective
    ;;
  launch)
    run_firesim_sync launchrunfarm
    ;;
  infrasetup)
    patch_local_image_guest_env
    verify_local_freshness_effective
    run_firesim_sync infrasetup
    ;;
  remote-freshness)
    verify_local_freshness_effective
    private_ip="$(resolve_private_ip "${1:-}")"
    "${remote_freshness_script}" "${workload_json}" "${private_ip}"
    ;;
  run)
    verify_local_freshness_effective
    private_ip="$(resolve_private_ip "${1:-}")"
    "${remote_freshness_script}" "${workload_json}" "${private_ip}"
    export FIRESIM_RUNWORKLOAD_MONITOR_SCRIPT="${monitor_script}"
    run_firesim_async runworkload
    ;;
  terminate)
    run_firesim_sync terminaterunfarm --forceterminate
    ;;
  current-private-ip)
    resolve_private_ip "${1:-}"
    ;;
  *)
    usage >&2
    exit 2
    ;;
esac
