#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PAIRDUMMY_FIXED_ENV_SCRIPT="${PAIRDUMMY_FIXED_ENV_SCRIPT:-${script_dir}/pairdummy_sbus64_dummy8x8_g6_fixed_env.sh}"
export PAIRDUMMY_WORKLOAD_JSON="${PAIRDUMMY_WORKLOAD_JSON:-generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-sbus64-dummy8x8-g6.json}"
export PAIRDUMMY_RUNTIME_CFG="${PAIRDUMMY_RUNTIME_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_2c6p6_sbus64_nic_hwdebug_hostdebug_linux_bertmini_pipeline_runtime_batch8_fileonly_sync.yaml}"
export PAIRDUMMY_HWDB_CFG="${PAIRDUMMY_HWDB_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_2c6p6_sbus64_nic_hwdebug_hostdebug.yaml}"
export PAIRDUMMY_BUILD_RECIPES_CFG="${PAIRDUMMY_BUILD_RECIPES_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_pairmanager_dummy8x8_2c6p6_sbus64_nic_hwdebug_hostdebug.yaml}"
export PAIRDUMMY_WORKFLOW_NAME="${PAIRDUMMY_WORKFLOW_NAME:-$(basename "${BASH_SOURCE[0]}")}"
export PAIRDUMMY_WORKFLOW_TAG="${PAIRDUMMY_WORKFLOW_TAG:-pairdummy-sbus64-dummy8x8-g6-hwdebug}"
export PAIRDUMMY_NETWORK_AUDIT_TARGET_GLOB="${PAIRDUMMY_NETWORK_AUDIT_TARGET_GLOB:-*FireSimGemminiReRoCCPairDummy8x8C2P6Sbus64NICDebugConfig}"

exec "${script_dir}/pairdummy_sbus128_workflow.sh" "$@"
