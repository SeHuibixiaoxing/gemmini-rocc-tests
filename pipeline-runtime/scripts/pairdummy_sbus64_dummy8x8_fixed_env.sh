#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

caller_pipeline_runtime_guest_log_enable="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE-}"
caller_pipeline_runtime_guest_deep_log_enable="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE-}"
caller_pipeline_runtime_breadcrumb_enable="${PIPELINE_RUNTIME_BREADCRUMB_ENABLE-}"

# Reuse the low-noise pairdummy defaults, then pin the hardware-dependent
# fields to the current dummy8x8/sbus64 cfg32 NIC noTrace AGFI.
# shellcheck disable=SC1091
source "${script_dir}/pairdummy_sbus128_fixed_env.sh"

export PIPELINE_RUNTIME_PROFILE_ID="pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly"
export TARGET_KEY="rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64"
export METHODS="ours2"
export TARGET_BATCH="${PAIRDUMMY_SBUS64_TARGET_BATCH:-8}"

export NUM_CORES="4"
export NUM_GEMMINI="12"
export NUM_DMA="12"
export GEMMINI_BASE_ID="0"
export DMA_BASE_ID="0"
export PAIR_MANAGER_MODE="1"
export PAGES_PER_ACC="1024"

# Low-disturbance live-GDB profile:
# - do not write hot runtime progress logs into the guest rootfs;
# - keep wrapper/stdout traffic on UART instead of the block image;
# - avoid background sync, child /proc polling, and breadcrumb mmap writes while
#   GDB is moving the frontier.
export PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PAIRDUMMY_SBUS64_GUEST_LOG_ENABLE:-${caller_pipeline_runtime_guest_log_enable:-0}}"
export PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PAIRDUMMY_SBUS64_GUEST_DEEP_LOG_ENABLE:-${caller_pipeline_runtime_guest_deep_log_enable:-0}}"
export PIPELINE_RUNTIME_STDIO_CAPTURE_MODE="uart"
export CAPTURE_PERIODIC_SYNC_ENABLE="0"
export PIPELINE_RUNTIME_CHILD_PROC_DIAG_ENABLE="0"
export PIPELINE_RUNTIME_BREADCRUMB_ENABLE="${PAIRDUMMY_SBUS64_BREADCRUMB_ENABLE:-${caller_pipeline_runtime_breadcrumb_enable:-0}}"

# Manual live-GDB stops freeze the guest heartbeat, and this low-log profile does
# not produce enough file growth for the host watchdog to infer progress. Keep a
# finite guardrail, but make it long enough for an interactive breakpoint round.
export FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS="${PAIRDUMMY_SBUS64_GDB_IDLE_TIMEOUT_SECONDS:-${FIRESIM_RUNWORKLOAD_IDLE_TIMEOUT_SECONDS:-7200}}"
if [[ "${FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS:-}" == "3600" ]]; then
  export FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS="${PAIRDUMMY_SBUS64_GDB_LIVE_IDLE_TIMEOUT_SECONDS:-10800}"
else
  export FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS="${PAIRDUMMY_SBUS64_GDB_LIVE_IDLE_TIMEOUT_SECONDS:-${FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS:-10800}}"
fi

if [[ -n "${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE+x}" ]]; then
  export PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE="${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE}"
fi
