#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck disable=SC1091
source "${script_dir}/pairdummy_sbus128_fixed_env.sh"

export PIPELINE_RUNTIME_PROFILE_ID="pairdummy-sbus128-g6-fixed-v2-cfg32"
export TARGET_KEY="rerocc_globalnoc_pairmanager_dummy16x16_c4_g6_d6_spad1024kb_dram19_noc64_mac256_sbus128"
export NUM_GEMMINI="6"
export NUM_DMA="6"
