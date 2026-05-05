#!/usr/bin/env bash
set -euo pipefail

exec > >(tee -a /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/rocket-singlecore-nic-gdbserver-repeat-terminate-20260505-121316.pane.log) 2>&1

watchdog_enabled=false
watchdog_timeout_seconds=10800
watchdog_log=''
watchdog_pid_file=''
watchdog_triggered_file=''
watchdog_runtime_config=''
watchdog_hwdb=''
watchdog_build_recipes=''
watchdog_pid=""
monitor_script=''
monitor_log=''
monitor_pid_file=''
monitor_pid=""



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
echo "[firesim-tmux] command: firesim terminaterunfarm --forceterminate -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_clean1bp_recovereddriver_30mhz.yaml -a /home/ubuntu/chipyard/tmp/firesim-aws-f2/hwdb/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz_agfi03d951_timingholdfixpcisreg1bp_driver.yaml -r /home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml"
echo "[firesim-tmux] propagated FIRESIM env vars:  "

set +e
if [[ "${watchdog_enabled}" == "true" ]]; then
  firesim terminaterunfarm --forceterminate -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_clean1bp_recovereddriver_30mhz.yaml -a /home/ubuntu/chipyard/tmp/firesim-aws-f2/hwdb/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz_agfi03d951_timingholdfixpcisreg1bp_driver.yaml -r /home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml &
  firesim_pid=$!
  rm -f "${watchdog_triggered_file}"
  echo "[firesim-watchdog] armed timeout=${watchdog_timeout_seconds}s log=${watchdog_log}"

  (
    exec > >(tee -a "${watchdog_log}") 2>&1
    sleep "${watchdog_timeout_seconds}"
    if [[ -f /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/rocket-singlecore-nic-gdbserver-repeat-terminate-20260505-121316.exitcode ]]; then
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
      export FIRESIM_MONITOR_SESSION_NAME="rocket-singlecore-nic-gdbserver-repeat-terminate-20260505-121316"
      export FIRESIM_MONITOR_EXIT_CODE_FILE=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/rocket-singlecore-nic-gdbserver-repeat-terminate-20260505-121316.exitcode
      export FIRESIM_MONITOR_STATE_DIR=/home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux
      "${monitor_script}"
    ) &
    monitor_pid=$!
    echo "${monitor_pid}" > "${monitor_pid_file}"
  fi

  wait "${firesim_pid}"
  status=$?
else
  firesim terminaterunfarm --forceterminate -c /home/ubuntu/chipyard/sims/firesim/deploy/config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_clean1bp_recovereddriver_30mhz.yaml -a /home/ubuntu/chipyard/tmp/firesim-aws-f2/hwdb/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz_agfi03d951_timingholdfixpcisreg1bp_driver.yaml -r /home/ubuntu/chipyard/sims/firesim/deploy/config_build_recipes_f2_rocket_singlecore_nic_notrace_30mhz.yaml
  status=$?
fi
set -e

echo "${status}" > /home/ubuntu/chipyard/tmp/firesim-aws-f2/tmux/rocket-singlecore-nic-gdbserver-repeat-terminate-20260505-121316.exitcode
echo "[firesim-tmux] finished at $(date -u +%Y-%m-%dT%H:%M:%SZ) with exit code ${status}"
exit "${status}"
