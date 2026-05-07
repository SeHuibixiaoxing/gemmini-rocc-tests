#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
fixed_env_script="${PAIRDUMMY_FIXED_ENV_SCRIPT:-${script_dir}/pairdummy_sbus128_fixed_env.sh}"

# shellcheck disable=SC1091
source "${fixed_env_script}"

export PIPELINE_RUNTIME_GDBSERVER_ENABLE="${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-1}"
export PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR="${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR:-0.0.0.0}"
export PIPELINE_RUNTIME_GDBSERVER_PORT="${PIPELINE_RUNTIME_GDBSERVER_PORT:-2345}"
export PIPELINE_RUNTIME_GDBSERVER_INFO_PATH="${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info}"
export PIPELINE_RUNTIME_GDBSERVER_LOG_PATH="${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.log}"
export PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE="${PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE:-1}"
export PAIRDUMMY_WORKLOAD_JSON="${PAIRDUMMY_WORKLOAD_JSON:-generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json}"
export PAIRDUMMY_RUNTIME_CFG="${PAIRDUMMY_RUNTIME_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver.yaml}"
export PAIRDUMMY_WORKFLOW_NAME="${PAIRDUMMY_WORKFLOW_NAME:-$(basename "${BASH_SOURCE[0]}")}"
export PAIRDUMMY_WORKFLOW_TAG="${PAIRDUMMY_WORKFLOW_TAG:-pairdummy-sbus128-gdbserver}"

exec "${script_dir}/pairdummy_sbus128_workflow.sh" "$@"
