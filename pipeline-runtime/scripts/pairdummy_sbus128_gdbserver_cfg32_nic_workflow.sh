#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export PAIRDUMMY_RUNTIME_CFG="${PAIRDUMMY_RUNTIME_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic.yaml}"
export PAIRDUMMY_HWDB_CFG="${PAIRDUMMY_HWDB_CFG:-/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy16x16_4c12p12_sbus128_bertmini_cfg32_nic.yaml}"
export PAIRDUMMY_WORKFLOW_NAME="${PAIRDUMMY_WORKFLOW_NAME:-$(basename "${BASH_SOURCE[0]}")}"
export PAIRDUMMY_WORKFLOW_TAG="${PAIRDUMMY_WORKFLOW_TAG:-pairdummy-sbus128-gdbserver-cfg32-nic}"
export PAIRDUMMY_NETWORK_AUDIT_TARGET_GLOB="${PAIRDUMMY_NETWORK_AUDIT_TARGET_GLOB:-*WithNIC*GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128:*FireSimGemminiReRoCCPairDummy16x16C4P12Sbus128NICNoTraceConfig}"

exec "${script_dir}/pairdummy_sbus128_gdbserver_workflow.sh" "$@"
