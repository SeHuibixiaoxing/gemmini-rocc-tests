#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
fixed_env_script="${PAIRDUMMY_FIXED_ENV_SCRIPT:-${script_dir}/pairdummy_sbus128_fixed_env.sh}"
workflow_name="${PAIRDUMMY_WORKFLOW_NAME:-$(basename "${BASH_SOURCE[0]}")}"
workflow_tag="${PAIRDUMMY_WORKFLOW_TAG:-pairdummy-sbus128}"

# shellcheck disable=SC1091
source "${fixed_env_script}"

workload_json="${PAIRDUMMY_WORKLOAD_JSON:-generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy.json}"
runtime_cfg="${PAIRDUMMY_RUNTIME_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml}"
hwdb_cfg="${PAIRDUMMY_HWDB_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini.yaml}"
build_recipes_cfg="${PAIRDUMMY_BUILD_RECIPES_CFG:-/home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml}"
host_entrypoint_src="/home/ubuntu/chipyard/software/firemarshal/boards/firechip/distros/br/overlay/firemarshal.sh"
local_freshness_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firemarshal_image_freshness.sh"
remote_freshness_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/verify_pairdummy_firesim_remote_image_freshness.sh"
network_prepare_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/prepare_firesim_networked_gdbserver.sh"
network_audit_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/audit_pairdummy_networked_gdbserver_target.sh"
render_guest_env_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/render_pairdummy_guest_env.sh"
patch_guest_env_script="/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/apply_guest_env_to_image.sh"
monitor_script="scripts/firesim-prt-host-watchdog.sh"
firemarshal_state_dir="${cy_dir}/tmp/firemarshal-tmux"
firesim_state_dir="${cy_dir}/tmp/firesim-aws-f2/tmux"
effective_guest_env_dir="${cy_dir}/tmp/pipeline-runtime-effective-guest-env"
workload_name="$(basename "${workload_json}" .json)"
local_image_path="${cy_dir}/software/firemarshal/images/firechip/${workload_name}/${workload_name}.img"
effective_guest_env_path="${effective_guest_env_dir}/${workload_name}.firemarshal.env"
run_farm_tag="$(awk '/run_farm_tag:/ { print $2; exit }' "${runtime_cfg}")"
runtime_topology="$(awk '/^[[:space:]]*topology:/ { print $2; exit }' "${runtime_cfg}")"

usage() {
  cat <<EOF
Usage: ${workflow_name} <command> [private-ip]

Canonical fixed workflow for the 12-pair sbus128 dummy-model pipeline-runtime path.
This helper always uses:
  - /firemarshal.sh as the guest entrypoint
  - file-system logs instead of uartlog
  - private-IP-only remote freshness checks
  - the fixed pairdummy semantic profile from pairdummy_sbus128_fixed_env.sh

Commands:
  show                  Print the fixed paths and profile values
  debug-preflight       Check probe tier / trigger conflicts before a rerun
  image-closure         marshal clean -> build -> install -> local freshness
  marshal-clean         Run marshal clean for the fixed workload
  marshal-build         Run marshal build for the fixed workload
  marshal-install       Run marshal install for the fixed workload
  local-freshness       Verify the local FireMarshal image contents
  launch                Launch the FireSim run farm
  infrasetup            Run FireSim infrasetup
  remote-freshness [ip] Verify remote image freshness over a private IP
  network-audit         Audit whether the generated target actually exposes a guest NIC
  network-prepare [ip]  Prepare SSHPort switch/tap0 for example_1config gdbserver attach
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

  require_nonempty "${run_farm_tag}" "run_farm_tag from ${runtime_cfg}"

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

verify_local_freshness_effective() {
  render_effective_guest_env
  "${local_freshness_script}" "${workload_json}"
}

run_firemarshal() {
  local subcmd="$1"
  local session="${workflow_tag}-${subcmd}-$(timestamp)"
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
  local session="${workflow_tag}-${subcmd}-$(timestamp)"
  local exit_file="${firesim_state_dir}/${session}.exitcode"
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
  local session="${workflow_tag}-${subcmd}-$(timestamp)"
  shift
  "${cy_dir}/scripts/firesim-tmux-run.sh" --session-name "${session}" "${subcmd}" \
    -c "${runtime_cfg}" \
    -a "${hwdb_cfg}" \
    -r "${build_recipes_cfg}" \
    "$@"
}

is_networked_gdbserver_topology() {
  [[ "${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}" != "0" && "${runtime_topology}" == "example_1config" ]]
}

maybe_prepare_networked_gdbserver() {
  local private_ip="$1"
  if ! is_networked_gdbserver_topology; then
    return 0
  fi
  "${network_prepare_script}" "${private_ip}"
}

should_enforce_network_audit() {
  [[ "${PAIRDUMMY_SKIP_NETWORK_GDBSERVER_TARGET_AUDIT:-0}" == "0" ]]
}

run_network_audit() {
  "${network_audit_script}"
}

verify_networked_gdbserver_target_prereqs() {
  if ! is_networked_gdbserver_topology; then
    return 0
  fi
  if ! should_enforce_network_audit; then
    echo "network_target_audit_status=skipped"
    echo "network_target_audit_summary=skip-requested-by-PAIRDUMMY_SKIP_NETWORK_GDBSERVER_TARGET_AUDIT"
    return 0
  fi
  "${network_audit_script}"
}

show_config() {
  cat <<EOF
workflow_name=${workflow_name}
workflow_tag=${workflow_tag}
profile_id=${PIPELINE_RUNTIME_PROFILE_ID}
fixed_env_script=${fixed_env_script}
workload_json=${workload_json}
runtime_cfg=${runtime_cfg}
hwdb_cfg=${hwdb_cfg}
build_recipes_cfg=${build_recipes_cfg}
runtime_topology=${runtime_topology}
host_entrypoint_src=${host_entrypoint_src}
run_farm_tag=${run_farm_tag}
local_freshness_script=${local_freshness_script}
remote_freshness_script=${remote_freshness_script}
network_prepare_script=${network_prepare_script}
network_audit_script=${network_audit_script}
network_prepare_mode=$([[ "${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}" != "0" && "${runtime_topology}" == "example_1config" ]] && echo auto || echo disabled)
network_target_audit_enforced=$([[ "${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}" != "0" && "${runtime_topology}" == "example_1config" && "${PAIRDUMMY_SKIP_NETWORK_GDBSERVER_TARGET_AUDIT:-0}" == "0" ]] && echo yes || echo no)
network_target_audit_skip=${PAIRDUMMY_SKIP_NETWORK_GDBSERVER_TARGET_AUDIT:-0}
guest_entrypoint=/firemarshal.sh
note=do-not-infer-entrypoint-from-workload-overlay-root-firemarshal.sh
firesim_live_idle_timeout_seconds=${FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS:-}
methods=${METHODS}
target_batch=${TARGET_BATCH}
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
dma_export_probe_enable=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE}
dma_force_direct_enable=${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE:-0}
dma_blocking_wait_poll_timeout_enable=${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE:-0}
export_dma_timeout_ms=${EXPORT_DMA_TIMEOUT_MS:-0}
dma_export_probe_token_start=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START}
dma_export_probe_token_end=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END}
dma_export_page_start=${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START:-}
dma_export_page_end=${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END:-}
gdbserver_enable=${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}
gdbserver_bind_addr=${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR:-0.0.0.0}
gdbserver_port=${PIPELINE_RUNTIME_GDBSERVER_PORT:-2345}
gdbserver_info_path=${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info}
gdbserver_log_path=${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.log}
gdbserver_console_announce=${PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE:-1}
local_gdb_enable=${PIPELINE_RUNTIME_LOCAL_GDB_ENABLE:-0}
local_gdb_info_path=${PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH:-/root/pipeline-runtime-debug/local-gdb.info}
local_gdb_log_path=${PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH:-/root/pipeline-runtime-debug/local-gdb.log}
local_gdb_timeout_secs=${PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS:-900}
local_gdb_continue_after_main=${PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN:-1}
dma_fixed_load_probe_enable=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE}
dma_fixed_load_probe_stage_id=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_STAGE_ID:-0}
dma_fixed_load_probe_tensor_id=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TENSOR_ID:-1000001}
dma_fixed_load_monitor_probe_enable=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE:-}
dma_fixed_load_pre_src_nops=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PRE_SRC_NOPS:-0}
dma_fixed_load_probe_token_start=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_START:-}
dma_fixed_load_probe_token_end=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_END:-}
dma_fixed_load_page_start=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START:-}
dma_fixed_load_page_end=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END:-}
dma_fixed_load_checkpoint_enable=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_CHECKPOINT_ENABLE}
breadcrumb_enable=${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}
breadcrumb_path=${PIPELINE_RUNTIME_BREADCRUMB_PATH}
breadcrumb_segment=${PIPELINE_RUNTIME_BREADCRUMB_SEGMENT:-any}
breadcrumb_global_stage=${PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE:-any}
breadcrumb_local_stage=${PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE:-any}
breadcrumb_subbatch=${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH:-any}
breadcrumb_stage_radius=${PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS:-0}
breadcrumb_subbatch_radius=${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS:-0}
debug_trigger_enable=${PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE}
debug_trigger_kind=${PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND:-any}
debug_trigger_segment=${PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT:-any}
debug_trigger_global_stage=${PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE:-any}
debug_trigger_local_stage=${PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE:-any}
debug_trigger_subbatch=${PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH:-any}
debug_trigger_manager=${PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER:-any}
debug_trigger_tensor_id=${PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID:-any}
debug_trigger_page=${PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE:-any}
debug_trigger_token=${PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN:-any}
debug_trigger_pre_ring=${PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING:-8}
debug_trigger_post_budget=${PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET:-32}
debug_trigger_match_once=${PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE:-1}
debug_trigger_log_path=${PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.trigger.log}
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

  if [[ "${PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE}" != "0" ]]; then
    if [[ -z "${PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND:-}" ]]; then
      fail=1
      notes+=("debug trigger is enabled but PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND is empty")
    fi
    if [[ "${PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND:-}" == "gemmini-pointwise" && "${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}" == "0" ]]; then
      fail=1
      notes+=("gemmini-pointwise trigger requires breadcrumb-first to stay enabled")
    fi
  fi

  if [[ "${high_risk_count}" -gt 1 ]]; then
    fail=1
    notes+=("multiple high-risk probe families are enabled together")
  fi

  echo "debug_preflight_profile_id=${PIPELINE_RUNTIME_PROFILE_ID}"
  echo "debug_preflight_probe_tier=${probe_tier}"
  echo "debug_preflight_change_kind_required=manual-record"
  echo "debug_preflight_control_rerun_required_if_observability_only=1"
  echo "debug_preflight_trigger_enable=${PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE}"
  echo "debug_preflight_trigger_kind=${PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND:-any}"
  echo "debug_preflight_breadcrumb_enable=${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}"
  echo "debug_preflight_guest_deep_log_enable=${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE}"
  echo "debug_preflight_dma_export_probe_enable=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE}"
  echo "debug_preflight_dma_force_direct_enable=${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE:-0}"
  echo "debug_preflight_dma_blocking_wait_poll_timeout_enable=${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE:-0}"
  echo "debug_preflight_export_dma_timeout_ms=${EXPORT_DMA_TIMEOUT_MS:-0}"
  echo "debug_preflight_dma_fixed_load_probe_enable=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE}"
  echo "debug_preflight_dma_fixed_load_monitor_probe_enable=${PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE}"
  if [[ "${#notes[@]}" -eq 0 ]]; then
    echo "debug_preflight_notes=none"
  else
    printf 'debug_preflight_note=%s\n' "${notes[@]}"
  fi
  echo "debug_preflight_recommended_order=artifact-audit -> debug-preflight -> triage_prt_capture.py --emit-trigger-env -> single-variable rerun"

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
    verify_networked_gdbserver_target_prereqs
    run_firesim_sync launchrunfarm
    ;;
  infrasetup)
    verify_networked_gdbserver_target_prereqs
    patch_local_image_guest_env
    verify_local_freshness_effective
    run_firesim_sync infrasetup
    ;;
  remote-freshness)
    verify_local_freshness_effective
    private_ip="$(resolve_private_ip "${1:-}")"
    "${remote_freshness_script}" "${workload_json}" "${private_ip}"
    ;;
  network-audit)
    run_network_audit
    ;;
  network-prepare)
    verify_networked_gdbserver_target_prereqs
    private_ip="$(resolve_private_ip "${1:-}")"
    maybe_prepare_networked_gdbserver "${private_ip}"
    ;;
  run)
    verify_networked_gdbserver_target_prereqs
    verify_local_freshness_effective
    private_ip="$(resolve_private_ip "${1:-}")"
    "${remote_freshness_script}" "${workload_json}" "${private_ip}"
    maybe_prepare_networked_gdbserver "${private_ip}"
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
