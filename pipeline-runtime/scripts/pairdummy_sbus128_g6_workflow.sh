#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PAIRDUMMY_FIXED_ENV_SCRIPT="${script_dir}/pairdummy_sbus128_g6_fixed_env.sh"
export PAIRDUMMY_WORKLOAD_JSON="generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-g6.json"
export PAIRDUMMY_RUNTIME_CFG="/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_g6_cfg32.yaml"
export PAIRDUMMY_HWDB_CFG="/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32.yaml"

exec "${script_dir}/pairdummy_sbus128_workflow.sh" "$@"
