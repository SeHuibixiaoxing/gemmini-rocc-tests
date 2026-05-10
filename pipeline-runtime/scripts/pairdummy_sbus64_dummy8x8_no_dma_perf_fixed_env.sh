#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Start from the known-good dummy8x8/sbus64 cfg32 noTrace profile, then narrow
# it to a non-interactive no-DMA performance run.
# shellcheck disable=SC1091
source "${script_dir}/pairdummy_sbus64_dummy8x8_fixed_env.sh"

export PIPELINE_RUNTIME_PROFILE_ID="pairdummy-sbus64-dummy8x8-no-dma-perf-v2-cache-disabled"
export METHODS="ours2 gemini2"
export PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE="1"
export TRACE_ENABLE="1"
export PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY="1"
export TRACE_DIR="/root/pipeline-runtime-debug/traces"

export PIPELINE_RUNTIME_GDBSERVER_ENABLE="0"
export PIPELINE_RUNTIME_LOCAL_GDB_ENABLE="0"
export PIPELINE_RUNTIME_GDB_MARKER_ENABLE="0"

export PIPELINE_RUNTIME_LOG_PROFILE="coarse"
# Match the known-good no-DMA cfg32/noTrace profile: keep runner/runtime stdout
# on UART instead of redirecting it into the guest rootfs log file.
export PIPELINE_RUNTIME_STDIO_CAPTURE_MODE="uart"
export PIPELINE_RUNTIME_UART_LOG_ENABLE="0"
export PIPELINE_RUNTIME_GUEST_LOG_ENABLE="0"
export PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="0"
export PIPELINE_RUNTIME_YAML_LINE_LOG_ENABLE="0"
export PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="0"
export PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE="0"
export PIPELINE_RUNTIME_BREADCRUMB_ENABLE="0"
# Keep the F2 no-DMA performance run close to the known-good cfg32 noTrace
# no-DMA profile. Preprocessing time is reported separately and excluded from
# the comparison, so prefer the stable YAML path over the mapping-cache path.
export PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE="${PAIRDUMMY_SBUS64_PERF_DISABLE_MAPPING_CACHE:-${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE:-1}}"
export PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="0"
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE="0"
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE="0"
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE="0"
export PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE="0"
export PIPELINE_RUNTIME_CHILD_PROC_DIAG_ENABLE="0"
export PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE="0"

export CAPTURE_PERIODIC_SYNC_ENABLE="0"
export CAPTURE_PROGRESS_PING_ENABLE="0"
export FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS="7200"
export FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS="10800"
