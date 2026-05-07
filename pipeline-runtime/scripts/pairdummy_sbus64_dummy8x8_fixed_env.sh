#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Reuse the low-noise pairdummy defaults, then pin the hardware-dependent
# fields to the current dummy8x8/sbus64 cfg32 NIC noTrace AGFI.
# shellcheck disable=SC1091
source "${script_dir}/pairdummy_sbus128_fixed_env.sh"

export PIPELINE_RUNTIME_PROFILE_ID="pairdummy-sbus64-dummy8x8-fixed-v2"
export TARGET_KEY="rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64"
export METHODS="ours2"
export TARGET_BATCH="8"

export NUM_CORES="4"
export NUM_GEMMINI="12"
export NUM_DMA="12"
export GEMMINI_BASE_ID="0"
export DMA_BASE_ID="0"
export PAIR_MANAGER_MODE="1"
export PAGES_PER_ACC="1024"

if [[ -n "${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE+x}" ]]; then
  export PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE="${PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE}"
fi
