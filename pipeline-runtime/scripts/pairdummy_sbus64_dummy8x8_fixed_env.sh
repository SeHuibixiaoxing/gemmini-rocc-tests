#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Reuse the low-noise pairdummy defaults, then pin the hardware-dependent
# fields to the current dummy8x8/sbus64 cfg32 NIC noTrace AGFI.
# shellcheck disable=SC1091
source "${script_dir}/pairdummy_sbus128_fixed_env.sh"

export PIPELINE_RUNTIME_PROFILE_ID="pairdummy-sbus64-dummy8x8-fixed-v4-nolog"
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
# - avoid background sync and child /proc polling while GDB is moving the frontier.
export PIPELINE_RUNTIME_GUEST_LOG_ENABLE="0"
export PIPELINE_RUNTIME_STDIO_CAPTURE_MODE="uart"
export CAPTURE_PERIODIC_SYNC_ENABLE="0"
export PIPELINE_RUNTIME_CHILD_PROC_DIAG_ENABLE="0"

if [[ -n "${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE+x}" ]]; then
  export PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE="${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE}"
fi
