#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PAIRDUMMY_WORKLOAD_JSON="${PAIRDUMMY_WORKLOAD_JSON:-generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-local-gdb.json}"
export PAIRDUMMY_RUNTIME_CFG="${PAIRDUMMY_RUNTIME_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_local_gdb_cfg32_nic.yaml}"
export PAIRDUMMY_HWDB_CFG="${PAIRDUMMY_HWDB_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml}"
export PAIRDUMMY_WORKFLOW_NAME="${PAIRDUMMY_WORKFLOW_NAME:-$(basename "${BASH_SOURCE[0]}")}"
export PAIRDUMMY_WORKFLOW_TAG="${PAIRDUMMY_WORKFLOW_TAG:-pairdummy-sbus128-local-gdb-cfg32-nic}"

export PIPELINE_RUNTIME_GDBSERVER_ENABLE="${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}"
export PIPELINE_RUNTIME_LOCAL_GDB_ENABLE="${PIPELINE_RUNTIME_LOCAL_GDB_ENABLE:-1}"
export PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH="${PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH:-/root/pipeline-runtime-debug/local-gdb.info}"
export PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH="${PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH:-/root/pipeline-runtime-debug/local-gdb.log}"
export PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS="${PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS:-900}"
export PIPELINE_RUNTIME_LOCAL_GDB_STATUS_INTERVAL_SECS="${PIPELINE_RUNTIME_LOCAL_GDB_STATUS_INTERVAL_SECS:-30}"
export PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN="${PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN:-1}"
export PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE:-1}"
export PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE="${PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE:-0}"

exec "${script_dir}/pairdummy_sbus128_workflow.sh" "$@"
