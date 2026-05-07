#!/usr/bin/env bash
set -euo pipefail

exec > >(tee -a /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.pane.log) 2>&1

watchdog_enabled=true
watchdog_timeout_seconds=10800
watchdog_log=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.watchdog.log
watchdog_pid_file=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.watchdog.pid
watchdog_triggered_file=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.watchdog.triggered
watchdog_runtime_config=/home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml
watchdog_hwdb=/home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml
watchdog_build_recipes=/home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml
watchdog_pid=""
monitor_script=/home/ubuntu/chipyard/scripts/firesim-prt-host-watchdog.sh
monitor_log=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.monitor.log
monitor_pid_file=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.monitor.pid
monitor_pid=""

export DMA_BASE_ID=0
export FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS=3600
export FIRESIM_RUNWORKLOAD_MONITOR_SCRIPT=scripts/firesim-prt-host-watchdog.sh
export GEMMINI_BASE_ID=0
export NUM_CORES=4
export NUM_DMA=12
export NUM_GEMMINI=12
export PAIR_MANAGER_MODE=1
export PIPELINE_RUNTIME_BREADCRUMB_ENABLE=1
export PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE=0
export PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE=0
export PIPELINE_RUNTIME_BREADCRUMB_PATH=/root/pipeline-runtime-debug/bertmini-batch8.breadcrumb.bin
export PIPELINE_RUNTIME_BREADCRUMB_SEGMENT=0
export PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS=0
export PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH=''
export PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS=0
export PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE=0
export PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE=0
export PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND=''
export PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE=0
export PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH=/root/pipeline-runtime-debug/bertmini-batch8.trigger.log
export PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER=''
export PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE=1
export PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE=''
export PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET=32
export PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING=8
export PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT=0
export PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH=''
export PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID=''
export PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN=''
export PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1
export PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=0
export PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END=4294967295
export PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START=4294967295
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE=0
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END=''
export PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START=''
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_CHECKPOINT_ENABLE=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PRE_SRC_NOPS=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_STAGE_ID=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TENSOR_ID=0
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_END=''
export PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_START=''
export PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1
export PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=0

cleanup_watchdog() {
  if [[ -n "${watchdog_pid}" ]]; then
    if [[ -f "${watchdog_triggered_file}" ]]; then
      wait "${watchdog_pid}" || true
    elif kill -0 "${watchdog_pid}" 2>/dev/null; then
      kill "${watchdog_pid}" 2>/dev/null || true
      wait "${watchdog_pid}" || true
    fi
  fi
  rm -f "${watchdog_pid_file}"

  if [[ -n "${monitor_pid}" ]]; then
    if kill -0 "${monitor_pid}" 2>/dev/null; then
      kill "${monitor_pid}" 2>/dev/null || true
      wait "${monitor_pid}" || true
    fi
  fi
  rm -f "${monitor_pid_file}"
}

trap cleanup_watchdog EXIT

cd /home/ubuntu/chipyard/sims/firesim
set +u
source sourceme-manager.sh --skip-ssh-setup
set -u
cd deploy

echo "[firesim-tmux] started at $(date -u +%Y-%m-%dT%H:%M:%SZ)"
echo "[firesim-tmux] working directory: $PWD"
echo "[firesim-tmux] command: firesim runworkload -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml -a /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml"
echo "[firesim-tmux] propagated FIRESIM env vars: DMA_BASE_ID FIRESIM_RUNWORKLOAD_LIVE_IDLE_TIMEOUT_SECONDS FIRESIM_RUNWORKLOAD_MONITOR_SCRIPT GEMMINI_BASE_ID NUM_CORES NUM_DMA NUM_GEMMINI PAIR_MANAGER_MODE PIPELINE_RUNTIME_BREADCRUMB_ENABLE PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE PIPELINE_RUNTIME_BREADCRUMB_PATH PIPELINE_RUNTIME_BREADCRUMB_SEGMENT PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS PIPELINE_RUNTIME_DEBUG_TRIGGER_ENABLE PIPELINE_RUNTIME_DEBUG_TRIGGER_GLOBAL_STAGE PIPELINE_RUNTIME_DEBUG_TRIGGER_KIND PIPELINE_RUNTIME_DEBUG_TRIGGER_LOCAL_STAGE PIPELINE_RUNTIME_DEBUG_TRIGGER_LOG_PATH PIPELINE_RUNTIME_DEBUG_TRIGGER_MANAGER PIPELINE_RUNTIME_DEBUG_TRIGGER_MATCH_ONCE PIPELINE_RUNTIME_DEBUG_TRIGGER_PAGE PIPELINE_RUNTIME_DEBUG_TRIGGER_POST_BUDGET PIPELINE_RUNTIME_DEBUG_TRIGGER_PRE_RING PIPELINE_RUNTIME_DEBUG_TRIGGER_SEGMENT PIPELINE_RUNTIME_DEBUG_TRIGGER_SUBBATCH PIPELINE_RUNTIME_DEBUG_TRIGGER_TENSOR_ID PIPELINE_RUNTIME_DEBUG_TRIGGER_TOKEN PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START PIPELINE_RUNTIME_DMA_FIXED_LOAD_CHECKPOINT_ENABLE PIPELINE_RUNTIME_DMA_FIXED_LOAD_MONITOR_PROBE_ENABLE PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_END PIPELINE_RUNTIME_DMA_FIXED_LOAD_PAGE_START PIPELINE_RUNTIME_DMA_FIXED_LOAD_PRE_SRC_NOPS PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_ENABLE PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_STAGE_ID PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TENSOR_ID PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_END PIPELINE_RUNTIME_DMA_FIXED_LOAD_PROBE_TOKEN_START PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE "

set +e
if [[ "${watchdog_enabled}" == "true" ]]; then
  firesim runworkload -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml -a /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml &
  firesim_pid=$!
  rm -f "${watchdog_triggered_file}"
  echo "[firesim-watchdog] armed timeout=${watchdog_timeout_seconds}s log=${watchdog_log}"

  (
    exec > >(tee -a "${watchdog_log}") 2>&1
    sleep "${watchdog_timeout_seconds}"
    if [[ -f /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.exitcode ]]; then
      exit 0
    fi

    : > "${watchdog_triggered_file}"
    echo "[firesim-watchdog] timeout reached at $(date -u +%Y-%m-%dT%H:%M:%SZ)"

    if kill -0 "${firesim_pid}" 2>/dev/null; then
      echo "[firesim-watchdog] sending SIGTERM to runworkload pid=${firesim_pid}"
      kill -TERM "${firesim_pid}" 2>/dev/null || true
      sleep 15
      if kill -0 "${firesim_pid}" 2>/dev/null; then
        echo "[firesim-watchdog] sending SIGKILL to runworkload pid=${firesim_pid}"
        kill -KILL "${firesim_pid}" 2>/dev/null || true
      fi
    fi

    echo "[firesim-watchdog] calling terminaterunfarm --forceterminate"
    firesim terminaterunfarm --forceterminate       -c "${watchdog_runtime_config}"       -a "${watchdog_hwdb}"       -r "${watchdog_build_recipes}"
    term_status=$?
    echo "[firesim-watchdog] terminaterunfarm exit code ${term_status}"

    WATCHDOG_RUNTIME_CONFIG="${watchdog_runtime_config}" python - <<'PY'
import subprocess
import sys
import yaml
import os

runtime_config = os.environ["WATCHDOG_RUNTIME_CONFIG"]
with open(runtime_config, "r", encoding="utf-8") as handle:
    cfg = yaml.safe_load(handle)

tag = (
    cfg.get("run_farm", {})
       .get("recipe_arg_overrides", {})
       .get("run_farm_tag")
)
if not tag:
    print("[firesim-watchdog] no run_farm_tag in runtime config; skipping aws fallback")
    sys.exit(0)

query = "Reservations[].Instances[].InstanceId"
describe_cmd = [
    "aws", "ec2", "describe-instances",
    "--filters",
    f"Name=tag:fsimcluster,Values={tag}",
    "Name=instance-state-name,Values=pending,running,stopping,stopped",
    "--query", query,
    "--output", "text",
]
proc = subprocess.run(describe_cmd, check=False, text=True, capture_output=True)
if proc.returncode != 0:
    print(f"[firesim-watchdog] aws describe-instances failed rc={proc.returncode}")
    if proc.stdout:
        print(proc.stdout.strip())
    if proc.stderr:
        print(proc.stderr.strip())
    sys.exit(0)

instance_ids = [tok for tok in proc.stdout.split() if tok]
if not instance_ids:
    print(f"[firesim-watchdog] no live instances remain for fsimcluster={tag}")
    sys.exit(0)

print(
    "[firesim-watchdog] forcing aws terminate-instances for "
    + " ".join(instance_ids)
)
subprocess.run(
    ["aws", "ec2", "terminate-instances", "--instance-ids", *instance_ids],
    check=False,
)
PY
  ) &
  watchdog_pid=$!
  echo "${watchdog_pid}" > "${watchdog_pid_file}"

  if [[ -n "${monitor_script}" ]]; then
    echo "[firesim-monitor] starting ${monitor_script}"
    (
      exec > >(tee -a "${monitor_log}") 2>&1
      export FIRESIM_MONITOR_RUNTIME_CONFIG="${watchdog_runtime_config}"
      export FIRESIM_MONITOR_HWDB="${watchdog_hwdb}"
      export FIRESIM_MONITOR_BUILD_RECIPES="${watchdog_build_recipes}"
      export FIRESIM_MONITOR_SESSION_NAME="pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435"
      export FIRESIM_MONITOR_EXIT_CODE_FILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.exitcode
      export FIRESIM_MONITOR_STATE_DIR=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux
      "${monitor_script}"
    ) &
    monitor_pid=$!
    echo "${monitor_pid}" > "${monitor_pid_file}"
  fi

  wait "${firesim_pid}"
  status=$?
else
  firesim runworkload -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml -a /home/ubuntu/chipyard/sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml -r /home/ubuntu/chipyard/sims/firesim-staging/sample_config_build_recipes.yaml
  status=$?
fi
set -e

echo "${status}" > /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-175435.exitcode
echo "[firesim-tmux] finished at $(date -u +%Y-%m-%dT%H:%M:%SZ) with exit code ${status}"
exit "${status}"
