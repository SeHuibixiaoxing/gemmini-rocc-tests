#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: render_pairdummy_guest_env.sh <output-path>

Render the effective pairdummy guest /firemarshal.env by starting from the
checked-in baseline and applying only the temporary GDB marker,
PIPELINE_RUNTIME_DEBUG_TRIGGER_*, and selected
PIPELINE_RUNTIME_BREADCRUMB_*/PIPELINE_RUNTIME_DEBUG_FILTER_* overrides from
the current shell environment.
EOF
}

if [[ $# -ne 1 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
out_path="$1"
baseline_path="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/overlay/firemarshal.env"

if [[ ! -f "${baseline_path}" ]]; then
  echo "[pairdummy-guest-env] missing baseline env: ${baseline_path}" >&2
  exit 1
fi

mkdir -p "$(dirname "${out_path}")"
cp "${baseline_path}" "${out_path}"

sh_single_quote() {
  printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"
}

apply_override() {
  local env_name="$1"
  local env_value="$2"
  local replacement=""
  local tmp_path

  tmp_path="$(mktemp)"
  if [[ -n "${env_value}" ]]; then
    replacement="export ${env_name}=$(sh_single_quote "${env_value}")"
  fi

  awk -v env_name="${env_name}" -v replacement="${replacement}" '
    BEGIN { replaced = 0 }
    {
      if ($0 ~ ("^export " env_name "=")) {
        if (replacement != "") print replacement
        replaced = 1
        next
      }
      print
    }
    END {
      if (!replaced && replacement != "") print replacement
    }
  ' "${out_path}" > "${tmp_path}"
  mv "${tmp_path}" "${out_path}"
}

profile_env_names=(
  PIPELINE_RUNTIME_PROFILE_ID
  TARGET_KEY
  METHODS
  TARGET_BATCH
  NUM_CORES
  NUM_GEMMINI
  NUM_DMA
  GEMMINI_BASE_ID
  DMA_BASE_ID
  PAIR_MANAGER_MODE
  PAGES_PER_ACC
  WATCHDOG_MS
  EXPORT_DMA_TIMEOUT_MS
  TRACE_ENABLE
  GOLDEN_CHECK_ENABLE
  DUMMY_GEMMINI_MODE
  PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD
  PIPELINE_RUNTIME_SKIP_INPUT_LOAD
  PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK
  PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE
  PIPELINE_RUNTIME_UART_LOG_ENABLE
  PIPELINE_RUNTIME_GUEST_LOG_ENABLE
  PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE
  PIPELINE_RUNTIME_AUDIT_LOG_ENABLE
  PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE
  PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH
  PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE
  PIPELINE_RUNTIME_LOG_PROFILE
  PIPELINE_RUNTIME_STDIO_CAPTURE_MODE
  PIPELINE_RUNTIME_MLOCKALL_MODE
  PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE
  DEEP_LOG_ENABLE
  CAPTURE_AUTO_POWEROFF
  CAPTURE_PERIODIC_SYNC_ENABLE
  CAPTURE_PERIODIC_SYNC_SECONDS
  CAPTURE_PROGRESS_PING_ENABLE
  CAPTURE_PROGRESS_PING_SECONDS
  PIPELINE_RUNTIME_CHILD_PROC_DIAG_ENABLE
  PIPELINE_RUNTIME_CHILD_PROC_POLL_SECONDS
  HUGETLB_PAGES
  HUGETLB_MOUNT
)

trigger_env_names=(
  PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE
  PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND
  PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT
  PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE
  PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE
  PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH
  PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER
  PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID
  PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE
  PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN
  PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING
  PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET
  PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE
  PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH
)

breadcrumb_env_names=(
  PIPELINE_RUNTIME_BREADCRUMB_ENABLE
  PIPELINE_RUNTIME_BREADCRUMB_PATH
  PIPELINE_RUNTIME_BREADCRUMB_SEGMENT
  PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE
  PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE
  PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH
  PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS
  PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS
)

runner_env_names=(
  PIPELINE_RUNTIME_RUNNER_BIN_PROC_POLL_SECONDS
  PIPELINE_RUNTIME_RUNNER_BIN_MAPS_ONCE_ENABLE
  PIPELINE_RUNTIME_RUNNER_BIN_MAPS_ONCE_TARGET
  PIPELINE_RUNTIME_RUNNER_PROC_DETAIL_ENABLE
  PIPELINE_RUNTIME_RUNNER_PROC_DETAIL_LABEL_REGEX
  PIPELINE_RUNTIME_RUNNER_BIN_DELAY_PROBE_ENABLE
  PIPELINE_RUNTIME_RUNNER_BIN_DELAY_PROBE_DELAYS
  PIPELINE_RUNTIME_RUNNER_PTRACE_PROBE_ENABLE
  PIPELINE_RUNTIME_RUNNER_PTRACE_PROBE_TOOL
  PIPELINE_RUNTIME_RUNNER_PTRACE_PROBE_DETAIL_BYTES
)

dma_env_names=(
  PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE
  PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE
  PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE
)

gdbserver_env_names=(
  PIPELINE_RUNTIME_GDBSERVER_ENABLE
  PIPELINE_RUNTIME_GDBSERVER_TOOL
  PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR
  PIPELINE_RUNTIME_GDBSERVER_PORT
  PIPELINE_RUNTIME_GDBSERVER_INFO_PATH
  PIPELINE_RUNTIME_GDBSERVER_LOG_PATH
  PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE
  PIPELINE_RUNTIME_GDBSERVER_NET_DEV
  PIPELINE_RUNTIME_GDBSERVER_ONCE
  PIPELINE_RUNTIME_GDBSERVER_CONSOLE_PATH
)

gdb_marker_env_names=(
  PIPELINE_RUNTIME_GDB_MARKER_ENABLE
  PIPELINE_RUNTIME_GDB_MARKER_SITE
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT
  PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE
  PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH
  PIPELINE_RUNTIME_GDB_MARKER_MANAGER
  PIPELINE_RUNTIME_GDB_MARKER_TENSOR
  PIPELINE_RUNTIME_GDB_MARKER_PAGE
  PIPELINE_RUNTIME_GDB_MARKER_TOKEN
)

local_gdb_env_names=(
  PIPELINE_RUNTIME_LOCAL_GDB_ENABLE
  PIPELINE_RUNTIME_LOCAL_GDB_TOOL
  PIPELINE_RUNTIME_LOCAL_GDB_BIN
  PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH
  PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH
  PIPELINE_RUNTIME_LOCAL_GDB_CMDS
  PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS
  PIPELINE_RUNTIME_LOCAL_GDB_STATUS_INTERVAL_SECS
  PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN
  PIPELINE_RUNTIME_LOCAL_GDB_EXTRA_BREAKPOINTS
  PIPELINE_RUNTIME_LOCAL_GDB_TRACE_BREAKPOINTS
  PIPELINE_RUNTIME_LOCAL_GDB_TRACE_IGNORE_COUNTS
  PIPELINE_RUNTIME_LOCAL_GDB_TRACE_CONDITIONS
  PIPELINE_RUNTIME_LOCAL_GDB_TRACE_STATE
  PIPELINE_RUNTIME_LOCAL_GDB_STREAM_CONSOLE
  PIPELINE_RUNTIME_LOCAL_GDB_METHOD
)

debug_filter_env_names=(
  PIPELINE_RUNTIME_DEBUG_FILTER_SEGMENT
  PIPELINE_RUNTIME_DEBUG_FILTER_GLOBAL_STAGE
  PIPELINE_RUNTIME_DEBUG_FILTER_LOCAL_STAGE
  PIPELINE_RUNTIME_DEBUG_FILTER_SUBBATCH
  PIPELINE_RUNTIME_DEBUG_FILTER_PHASE
  PIPELINE_RUNTIME_DEBUG_FILTER_WAIT_PHASE
  PIPELINE_RUNTIME_DEBUG_FILTER_TENSOR
  PIPELINE_RUNTIME_DEBUG_FILTER_MANAGER
  PIPELINE_RUNTIME_DEBUG_FILTER_OPCODE
  PIPELINE_RUNTIME_DEBUG_FILTER_RR_STAGE
  PIPELINE_RUNTIME_DEBUG_FILTER_RR_MANAGER
  PIPELINE_RUNTIME_DEBUG_FILTER_RR_OPCODE
  PIPELINE_RUNTIME_DEBUG_FILTER_RR_CFG
)

for env_name in "${profile_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${trigger_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${breadcrumb_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${runner_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${dma_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${gdbserver_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${gdb_marker_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${local_gdb_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

for env_name in "${debug_filter_env_names[@]}"; do
  if [[ -n "${!env_name+x}" ]]; then
    apply_override "${env_name}" "${!env_name}"
  fi
done

sha="$(sha256sum "${out_path}" | awk '{print $1}')"
echo "[pairdummy-guest-env] rendered ${out_path} sha256=${sha}"
