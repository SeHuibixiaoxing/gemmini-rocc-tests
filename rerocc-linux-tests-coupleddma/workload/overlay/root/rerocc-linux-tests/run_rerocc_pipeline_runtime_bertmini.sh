#!/bin/sh
set -eu

RUNNER_EARLY_STAGE_PATH="${PIPELINE_RUNTIME_RUNNER_EARLY_STAGE_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.runner-early.stage}"
RUNNER_STAGE_PATH="${PIPELINE_RUNTIME_RUNNER_STAGE_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.runner.stage}"
RUNNER_PROC_STAGE_PATH="${PIPELINE_RUNTIME_RUNNER_PROC_STAGE_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.runner-proc.stage}"
RUNNER_BIN_PROC_POLL_SECONDS="${PIPELINE_RUNTIME_RUNNER_BIN_PROC_POLL_SECONDS:-5}"
RUNNER_STAGE_SYNC_ENABLE="${PIPELINE_RUNTIME_RUNNER_STAGE_SYNC_ENABLE:-0}"
RUNNER_SKIP_GUEST_ENV_SOURCE="${PIPELINE_RUNTIME_SKIP_GUEST_ENV_SOURCE:-0}"
RUNNER_BIN_MAPS_ONCE_ENABLE="${PIPELINE_RUNTIME_RUNNER_BIN_MAPS_ONCE_ENABLE:-0}"
RUNNER_BIN_MAPS_ONCE_TARGET="${PIPELINE_RUNTIME_RUNNER_BIN_MAPS_ONCE_TARGET:-/root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux}"
RUNNER_PROC_DETAIL_ENABLE="${PIPELINE_RUNTIME_RUNNER_PROC_DETAIL_ENABLE:-0}"
RUNNER_PROC_DETAIL_LABEL_REGEX="${PIPELINE_RUNTIME_RUNNER_PROC_DETAIL_LABEL_REGEX:-}"
RUNNER_BIN_DELAY_PROBE_ENABLE="${PIPELINE_RUNTIME_RUNNER_BIN_DELAY_PROBE_ENABLE:-0}"
RUNNER_BIN_DELAY_PROBE_DELAYS="${PIPELINE_RUNTIME_RUNNER_BIN_DELAY_PROBE_DELAYS:-}"
RUNNER_BIN_DELAY_PROBE_FORCE_SYNC="${PIPELINE_RUNTIME_RUNNER_BIN_DELAY_PROBE_FORCE_SYNC:-0}"
RUNNER_PTRACE_PROBE_ENABLE="${PIPELINE_RUNTIME_RUNNER_PTRACE_PROBE_ENABLE:-0}"
RUNNER_PTRACE_PROBE_TOOL="${PIPELINE_RUNTIME_RUNNER_PTRACE_PROBE_TOOL:-/root/rerocc-linux-tests/rerocc_ptrace_peek-linux}"
RUNNER_PTRACE_PROBE_DETAIL_BYTES="${PIPELINE_RUNTIME_RUNNER_PTRACE_PROBE_DETAIL_BYTES:-32}"
RUNNER_PROC_WCHAN_ENABLE="${PIPELINE_RUNTIME_RUNNER_PROC_WCHAN_ENABLE:-0}"
GDBSERVER_ENABLE="${PIPELINE_RUNTIME_GDBSERVER_ENABLE:-0}"
GDBSERVER_TOOL="${PIPELINE_RUNTIME_GDBSERVER_TOOL:-/usr/bin/gdbserver}"
GDBSERVER_BIND_ADDR="${PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR:-0.0.0.0}"
GDBSERVER_PORT="${PIPELINE_RUNTIME_GDBSERVER_PORT:-2345}"
GDBSERVER_INFO_PATH="${PIPELINE_RUNTIME_GDBSERVER_INFO_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.info}"
GDBSERVER_LOG_PATH="${PIPELINE_RUNTIME_GDBSERVER_LOG_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.gdbserver.log}"
GDBSERVER_CONSOLE_ANNOUNCE="${PIPELINE_RUNTIME_GDBSERVER_CONSOLE_ANNOUNCE:-1}"
GDBSERVER_NET_DEV="${PIPELINE_RUNTIME_GDBSERVER_NET_DEV:-}"
GDBSERVER_ONCE="${PIPELINE_RUNTIME_GDBSERVER_ONCE:-1}"
GDBSERVER_CONSOLE_PATH="${PIPELINE_RUNTIME_GDBSERVER_CONSOLE_PATH:-/dev/console}"
GDBSERVER_ENDPOINT="${GDBSERVER_BIND_ADDR}:${GDBSERVER_PORT}"
RUNNER_EARLY_STAGE_DIR="$(dirname "${RUNNER_EARLY_STAGE_PATH}")"
RUNNER_STAGE_DIR="$(dirname "${RUNNER_STAGE_PATH}")"
RUNNER_PROC_STAGE_DIR="$(dirname "${RUNNER_PROC_STAGE_PATH}")"
GDBSERVER_INFO_DIR="$(dirname "${GDBSERVER_INFO_PATH}")"
GDBSERVER_LOG_DIR="$(dirname "${GDBSERVER_LOG_PATH}")"
RUNNER_STAGE_SYNC_PID=""
RUNNER_BIN_MAPS_CAPTURED="0"
RUNNER_DELAY_PROBE_PIDS=""

mkdir -p \
  "${RUNNER_EARLY_STAGE_DIR}" \
  "${RUNNER_STAGE_DIR}" \
  "${RUNNER_PROC_STAGE_DIR}" \
  "${GDBSERVER_INFO_DIR}" \
  "${GDBSERVER_LOG_DIR}" 2>/dev/null || true

runner_stage_sync() {
  if [ "${RUNNER_STAGE_SYNC_ENABLE}" = "0" ]; then
    return 0
  fi
  if [ -n "${RUNNER_STAGE_SYNC_PID}" ]; then
    if kill -0 "${RUNNER_STAGE_SYNC_PID}" 2>/dev/null; then
      return 0
    fi
    wait "${RUNNER_STAGE_SYNC_PID}" 2>/dev/null || true
    RUNNER_STAGE_SYNC_PID=""
  fi

  (
    sync >/dev/null 2>&1 || true
  ) &
  RUNNER_STAGE_SYNC_PID=$!
}

write_runner_early_stage() {
  printf '[bertmini-early] %s ts=%s pid=%s ppid=%s\n' \
    "$*" \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    "$$" \
    "${PPID:-}" >> "${RUNNER_EARLY_STAGE_PATH}" 2>/dev/null || true
  runner_stage_sync
}

write_runner_stage() {
  printf '[bertmini-stage] %s ts=%s pid=%s\n' \
    "$*" \
    "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
    "$$" >> "${RUNNER_STAGE_PATH}" 2>/dev/null || true
  runner_stage_sync
}

append_proc_stage() {
  proc_pid="$1"
  label="$2"
  detail_capture=0
  if [ "${RUNNER_PROC_DETAIL_ENABLE}" != "0" ]; then
    if [ -z "${RUNNER_PROC_DETAIL_LABEL_REGEX}" ] || \
       printf '%s\n' "${label}" | grep -Eq "${RUNNER_PROC_DETAIL_LABEL_REGEX}"; then
      detail_capture=1
    fi
  fi
  {
    printf 'label=%s ts=%s pid=%s ppid=%s\n' \
      "${label}" \
      "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
      "${proc_pid}" \
      "${PPID:-}"
    printf 'cmdline='
    tr '\0' ' ' < "/proc/${proc_pid}/cmdline" 2>/dev/null || true
    printf '\n'
    printf 'wchan='
    if [ "${RUNNER_PROC_WCHAN_ENABLE}" != "0" ]; then
      cat "/proc/${proc_pid}/wchan" 2>/dev/null || true
    else
      printf 'skipped'
    fi
    printf '\n'
    printf 'status:\n'
    cat "/proc/${proc_pid}/status" 2>/dev/null || true
    if [ "${detail_capture}" = "1" ]; then
      printf 'syscall:\n'
      cat "/proc/${proc_pid}/syscall" 2>/dev/null || true
      printf '\n'
      printf 'stack:\n'
      cat "/proc/${proc_pid}/stack" 2>/dev/null || true
      printf '\n'
      printf 'sched:\n'
      sed -n '1,40p' "/proc/${proc_pid}/sched" 2>/dev/null || true
    fi
    if [ "${RUNNER_BIN_MAPS_ONCE_ENABLE}" != "0" ] && \
       [ "${RUNNER_BIN_MAPS_CAPTURED}" = "0" ] && \
       [ "${proc_pid}" != "$$" ]; then
      printf 'exe='
      readlink "/proc/${proc_pid}/exe" 2>/dev/null || true
      printf '\n'
      printf 'maps:\n'
      cat "/proc/${proc_pid}/maps" 2>/dev/null || true
    fi
    printf -- '---\n'
  } >> "${RUNNER_PROC_STAGE_PATH}" 2>/dev/null || true
  if [ "${RUNNER_BIN_MAPS_ONCE_ENABLE}" != "0" ] && \
     [ "${RUNNER_BIN_MAPS_CAPTURED}" = "0" ] && \
     [ "${proc_pid}" != "$$" ] && \
     grep -Fq "${RUNNER_BIN_MAPS_ONCE_TARGET}" "/proc/${proc_pid}/maps" 2>/dev/null; then
    RUNNER_BIN_MAPS_CAPTURED="1"
  fi
  runner_stage_sync
}

append_ptrace_probe() {
  proc_pid="$1"
  label="$2"

  if [ "${RUNNER_PTRACE_PROBE_ENABLE}" = "0" ] || \
     [ ! -x "${RUNNER_PTRACE_PROBE_TOOL}" ]; then
    return 0
  fi

  {
    printf 'ptrace-label=%s ts=%s pid=%s tool=%s bytes=%s\n' \
      "${label}" \
      "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
      "${proc_pid}" \
      "${RUNNER_PTRACE_PROBE_TOOL}" \
      "${RUNNER_PTRACE_PROBE_DETAIL_BYTES}"
    "${RUNNER_PTRACE_PROBE_TOOL}" "${proc_pid}" "${RUNNER_PTRACE_PROBE_DETAIL_BYTES}" 2>&1 || true
    printf -- '---\n'
  } >> "${RUNNER_PROC_STAGE_PATH}" 2>/dev/null || true
  runner_stage_sync
}

runner_console_notice() {
  if [ "${GDBSERVER_CONSOLE_ANNOUNCE}" = "0" ]; then
    return 0
  fi
  if [ -w "${GDBSERVER_CONSOLE_PATH}" ]; then
    printf '%s\n' "$*" > "${GDBSERVER_CONSOLE_PATH}" 2>/dev/null || true
  fi
}

guest_ipv4_for_dev() {
  net_dev="$1"
  ip_tool=""
  if command -v ip >/dev/null 2>&1; then
    ip_tool="$(command -v ip)"
  elif [ -x /sbin/ip ]; then
    ip_tool="/sbin/ip"
  elif [ -x /bin/ip ]; then
    ip_tool="/bin/ip"
  fi
  if [ -n "${ip_tool}" ]; then
    "${ip_tool}" -o -4 addr show dev "${net_dev}" 2>/dev/null | \
      awk 'NR == 1 { split($4, parts, "/"); print parts[1] }'
    return 0
  fi

  ifconfig_tool=""
  if command -v ifconfig >/dev/null 2>&1; then
    ifconfig_tool="$(command -v ifconfig)"
  elif [ -x /sbin/ifconfig ]; then
    ifconfig_tool="/sbin/ifconfig"
  elif [ -x /bin/ifconfig ]; then
    ifconfig_tool="/bin/ifconfig"
  fi
  if [ -n "${ifconfig_tool}" ]; then
    "${ifconfig_tool}" "${net_dev}" 2>/dev/null | awk '
      /inet addr:/ {
        sub("addr:", "", $2)
        print $2
        exit
      }
      /inet / {
        print $2
        exit
      }'
    return 0
  fi

  if [ -r "/sys/class/net/${net_dev}/address" ]; then
    IFS=: read -r _ _ _ _ machigh maclow < "/sys/class/net/${net_dev}/address" || true
    if [ -n "${machigh:-}" ] && [ -n "${maclow:-}" ]; then
      printf '172.16.%d.%d\n' "$((16#${machigh}))" "$((16#${maclow}))"
      return 0
    fi
  fi

  return 0
}

select_gdbserver_net_dev() {
  is_gdbserver_net_dev() {
    net_dev="$1"
    if [ "${net_dev}" = "lo" ]; then
      return 1
    fi
    if [ ! -d "/sys/class/net/${net_dev}" ] || \
       [ ! -r "/sys/class/net/${net_dev}/type" ] || \
       [ ! -r "/sys/class/net/${net_dev}/address" ]; then
      return 1
    fi
    if [ "$(cat "/sys/class/net/${net_dev}/type" 2>/dev/null)" != "1" ]; then
      return 1
    fi
    if [ "$(cat "/sys/class/net/${net_dev}/address" 2>/dev/null)" = "00:00:00:00:00:00" ]; then
      return 1
    fi
    return 0
  }

  if [ -n "${GDBSERVER_NET_DEV}" ] && is_gdbserver_net_dev "${GDBSERVER_NET_DEV}"; then
    printf '%s\n' "${GDBSERVER_NET_DEV}"
    return 0
  fi
  if is_gdbserver_net_dev eth0; then
    printf '%s\n' eth0
    return 0
  fi
  for net_path in /sys/class/net/*; do
    net_dev="$(basename "${net_path}")"
    if is_gdbserver_net_dev "${net_dev}"; then
      printf '%s\n' "${net_dev}"
      return 0
    fi
  done
  return 0
}

write_gdbserver_info() {
  phase="$1"
  method="$2"
  proc_pid="$3"
  extra_line="${4:-}"
  net_dev="$(select_gdbserver_net_dev || true)"
  guest_ipv4=""
  if [ -n "${net_dev}" ]; then
    guest_ipv4="$(guest_ipv4_for_dev "${net_dev}" || true)"
  fi
  {
    printf 'phase=%s\n' "${phase}"
    printf 'ts=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    printf 'method=%s\n' "${method}"
    printf 'enable=%s\n' "${GDBSERVER_ENABLE}"
    printf 'tool=%s\n' "${GDBSERVER_TOOL}"
    printf 'bind_addr=%s\n' "${GDBSERVER_BIND_ADDR}"
    printf 'port=%s\n' "${GDBSERVER_PORT}"
    printf 'endpoint=%s\n' "${GDBSERVER_ENDPOINT}"
    printf 'net_dev=%s\n' "${net_dev}"
    printf 'guest_ipv4=%s\n' "${guest_ipv4}"
    printf 'pid=%s\n' "${proc_pid}"
    if [ -n "${extra_line}" ]; then
      printf '%s\n' "${extra_line}"
    fi
  } > "${GDBSERVER_INFO_PATH}" 2>/dev/null || true
  runner_stage_sync
  runner_console_notice \
    "[gdbserver] phase=${phase} method=${method} guest_ipv4=${guest_ipv4:-unknown} endpoint=${guest_ipv4:-unknown}:${GDBSERVER_PORT} pid=${proc_pid}"
}

gdbserver_inferior_pid() {
  parent_pid="$1"
  if [ ! -r "/proc/${parent_pid}/task/${parent_pid}/children" ]; then
    return 0
  fi
  awk '{ print $1; exit }' "/proc/${parent_pid}/task/${parent_pid}/children" 2>/dev/null || true
}

runner_delay_probe_force_sync() {
  if [ "${RUNNER_BIN_DELAY_PROBE_FORCE_SYNC}" = "0" ]; then
    return 0
  fi
  sync >/dev/null 2>&1 || true
}

schedule_delayed_proc_probes() {
  proc_pid="$1"
  method="$2"

  if [ "${RUNNER_BIN_DELAY_PROBE_ENABLE}" = "0" ] || \
     [ -z "${RUNNER_BIN_DELAY_PROBE_DELAYS}" ]; then
    return 0
  fi

  old_ifs="${IFS}"
  IFS=', '
  for delay_s in ${RUNNER_BIN_DELAY_PROBE_DELAYS}; do
    [ -n "${delay_s}" ] || continue
    (
      sleep "${delay_s}"
      if kill -0 "${proc_pid}" 2>/dev/null; then
        append_proc_stage "${proc_pid}" "bin-delay-probe-${delay_s}s method=${method}"
        append_ptrace_probe "${proc_pid}" "bin-delay-probe-${delay_s}s method=${method}"
        runner_delay_probe_force_sync
      fi
    ) &
    RUNNER_DELAY_PROBE_PIDS="${RUNNER_DELAY_PROBE_PIDS} $!"
  done
  IFS="${old_ifs}"
}

cleanup_delayed_proc_probes() {
  for bg_pid in ${RUNNER_DELAY_PROBE_PIDS}; do
    if kill -0 "${bg_pid}" 2>/dev/null; then
      kill "${bg_pid}" 2>/dev/null || true
      wait "${bg_pid}" 2>/dev/null || true
    fi
  done
  RUNNER_DELAY_PROBE_PIDS=""
}

append_runner_proc_stage() {
  label="$1"
  append_proc_stage "$$" "${label}"
}

write_runner_early_stage "script-entry"
append_runner_proc_stage "script-entry"

GUEST_ENV_PATH="${PIPELINE_RUNTIME_GUEST_ENV_PATH:-/firemarshal.env}"
if [ "${RUNNER_SKIP_GUEST_ENV_SOURCE}" = "1" ]; then
  write_runner_early_stage "skip-guest-env inherited path=${GUEST_ENV_PATH}"
elif [ -r "${GUEST_ENV_PATH}" ]; then
  write_runner_early_stage "before-guest-env path=${GUEST_ENV_PATH}"
  # shellcheck disable=SC1090
  . "${GUEST_ENV_PATH}"
  write_runner_early_stage "after-guest-env path=${GUEST_ENV_PATH}"
else
  write_runner_early_stage "skip-guest-env path=${GUEST_ENV_PATH}"
fi

ROOT_DIR="/root/rerocc-linux-tests/pipeline-runtime"
BERT_DIR="${ROOT_DIR}/bertmini"
BIN="${ROOT_DIR}/rerocc_pipeline_runtime-linux"
METHODS="${METHODS:-ours2 gemini2 tangram2}"
TARGET_KEY="${TARGET_KEY:-rerocc_globalnoc_coupleddma_c2_g2_d2_spad1024kb_dram19_noc64_mac1024}"
TARGET_BATCH="${TARGET_BATCH:-16}"
NUM_CORES="${NUM_CORES:-2}"
NUM_GEMMINI="${NUM_GEMMINI:-2}"
NUM_DMA="${NUM_DMA:-}"
GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
DMA_BASE_ID="${DMA_BASE_ID:-}"
PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-0}"
PAGES_PER_ACC="${PAGES_PER_ACC:-1024}"
WATCHDOG_MS="${WATCHDOG_MS:-600000}"
EXPORT_DMA_TIMEOUT_MS="${EXPORT_DMA_TIMEOUT_MS:-0}"
TRACE_DIR="${TRACE_DIR:-/tmp/pipeline-runtime-traces}"
TRACE_ENABLE="${TRACE_ENABLE:-1}"
GOLDEN_CHECK_ENABLE="${GOLDEN_CHECK_ENABLE:-1}"
DUMMY_GEMMINI_MODE="${DUMMY_GEMMINI_MODE:-0}"
PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD="${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_INPUT_LOAD="${PIPELINE_RUNTIME_SKIP_INPUT_LOAD:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK="${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK:-${DUMMY_GEMMINI_MODE}}"
PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE="${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE:-0}"
PIPELINE_RUNTIME_LOG_PROFILE="${PIPELINE_RUNTIME_LOG_PROFILE:-manual}"
AUTO_POWEROFF="${AUTO_POWEROFF:-1}"
HUGETLB_PAGES="${HUGETLB_PAGES:-1}"
HUGETLB_MOUNT="${HUGETLB_MOUNT:-/dev/hugepages}"
DEEP_LOG_ENABLE="${DEEP_LOG_ENABLE:-1}"
PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE="${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE:-0}"
PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START:-}"
PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END:-}"
PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START:-}"
PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END:-}"
PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE="${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE:-32}"
PIPELINE_RUNTIME_BREADCRUMB_ENABLE="${PIPELINE_RUNTIME_BREADCRUMB_ENABLE:-0}"
PIPELINE_RUNTIME_BREADCRUMB_PATH="${PIPELINE_RUNTIME_BREADCRUMB_PATH:-/root/pipeline-runtime-debug/bertmini-batch8.breadcrumb.bin}"
PIPELINE_RUNTIME_BREADCRUMB_SEGMENT="${PIPELINE_RUNTIME_BREADCRUMB_SEGMENT:-}"
PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE:-}"
PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE:-}"
PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH:-}"
PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS:-}"
PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS:-}"
PIPELINE_RUNTIME_GDB_MARKER_ENABLE="${PIPELINE_RUNTIME_GDB_MARKER_ENABLE:-0}"
PIPELINE_RUNTIME_GDB_MARKER_SITE="${PIPELINE_RUNTIME_GDB_MARKER_SITE:-}"
PIPELINE_RUNTIME_GDB_MARKER_SEGMENT="${PIPELINE_RUNTIME_GDB_MARKER_SEGMENT:-}"
PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE="${PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE:-}"
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE="${PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE:-}"
PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH="${PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH:-}"
PIPELINE_RUNTIME_GDB_MARKER_MANAGER="${PIPELINE_RUNTIME_GDB_MARKER_MANAGER:-}"
PIPELINE_RUNTIME_GDB_MARKER_TENSOR="${PIPELINE_RUNTIME_GDB_MARKER_TENSOR:-}"
PIPELINE_RUNTIME_GDB_MARKER_PAGE="${PIPELINE_RUNTIME_GDB_MARKER_PAGE:-}"
PIPELINE_RUNTIME_GDB_MARKER_TOKEN="${PIPELINE_RUNTIME_GDB_MARKER_TOKEN:-}"
DEEP_LOG_SEGMENT="${DEEP_LOG_SEGMENT:-31}"
DEEP_LOG_GLOBAL_STAGE="${DEEP_LOG_GLOBAL_STAGE:-39}"
DEEP_LOG_LOCAL_STAGE="${DEEP_LOG_LOCAL_STAGE:-0}"
DEEP_LOG_SUBBATCH="${DEEP_LOG_SUBBATCH-}"
DEEP_LOG_STAGE_RADIUS="${DEEP_LOG_STAGE_RADIUS:-}"
DEEP_LOG_SUBBATCH_RADIUS="${DEEP_LOG_SUBBATCH_RADIUS:-}"
MODEL_YAML="${BERT_DIR}/model.layers.yaml"
LAYER_MAPPING_YAML="${BERT_DIR}/gemmini_layer_mapping.${TARGET_KEY}.yaml"
MODEL_BIN="${BERT_DIR}/runtime_model.bin"
INPUT_BIN="${BERT_DIR}/runtime_input.${TARGET_KEY}.bin"

apply_log_profile() {
  case "${PIPELINE_RUNTIME_LOG_PROFILE}" in
    manual|'')
      ;;
    coarse)
      DEEP_LOG_ENABLE=0
      PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE=0
      ;;
    fine_segment)
      DEEP_LOG_ENABLE=1
      : "${DEEP_LOG_SEGMENT:=0}"
      ;;
    *)
      echo "unknown PIPELINE_RUNTIME_LOG_PROFILE: ${PIPELINE_RUNTIME_LOG_PROFILE}" >&2
      exit 2
      ;;
  esac
}

apply_log_profile
if [ "${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE}" != "0" ]; then
  PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK=1
fi

while [ "$#" -gt 0 ]; do
  case "$1" in
    --batch)
      if [ "$#" -lt 2 ]; then
        echo "missing value for --batch" >&2
        exit 2
      fi
      TARGET_BATCH="$2"
      shift 2
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

write_runner_early_stage "after-arg-parse batch=${TARGET_BATCH}"
append_runner_proc_stage "after-arg-parse"

meminfo_value() {
  awk -v key="$1" '$1 == key ":" { print $2; exit }' /proc/meminfo 2>/dev/null
}

runner_log() {
  echo "[bertmini] $*"
}

resolve_manager_layout() {
  if [ "${PAIR_MANAGER_MODE}" = "1" ]; then
    [ -n "${NUM_DMA}" ] || NUM_DMA="${NUM_GEMMINI}"
    if [ "${NUM_DMA}" != "${NUM_GEMMINI}" ]; then
      echo "pair-manager mode requires NUM_DMA (${NUM_DMA}) to match NUM_GEMMINI (${NUM_GEMMINI})" >&2
      exit 2
    fi
    if [ -n "${DMA_BASE_ID}" ] && [ "${DMA_BASE_ID}" != "${GEMMINI_BASE_ID}" ]; then
      echo "pair-manager mode requires DMA_BASE_ID (${DMA_BASE_ID}) to match GEMMINI_BASE_ID (${GEMMINI_BASE_ID})" >&2
      exit 2
    fi
    DMA_BASE_ID="${GEMMINI_BASE_ID}"
    return
  fi

  [ -n "${NUM_DMA}" ] || NUM_DMA="${NUM_GEMMINI}"
  [ -n "${DMA_BASE_ID}" ] || DMA_BASE_ID=$((GEMMINI_BASE_ID + NUM_GEMMINI))
}

prepare_hugetlb() {
  before_total=""
  before_free=""
  before_size=""
  after_total=""
  after_free=""
  after_size=""

  runner_log "hugetlb-probe-begin mount=${HUGETLB_MOUNT} pages=${HUGETLB_PAGES}"
  before_total="$(meminfo_value HugePages_Total || true)"
  before_free="$(meminfo_value HugePages_Free || true)"
  before_size="$(meminfo_value Hugepagesize || true)"
  runner_log "hugetlb before total=${before_total:-0} free=${before_free:-0} size_kb=${before_size:-0}"

  if [ -w /proc/sys/vm/nr_hugepages ]; then
    runner_log "hugetlb-set-begin pages=${HUGETLB_PAGES}"
    echo "${HUGETLB_PAGES}" > /proc/sys/vm/nr_hugepages || true
    runner_log "hugetlb-set-end pages=${HUGETLB_PAGES}"
  fi

  mkdir -p "${HUGETLB_MOUNT}"
  if ! grep -qs " ${HUGETLB_MOUNT} " /proc/mounts; then
    runner_log "hugetlb-mount-begin mount=${HUGETLB_MOUNT}"
    mount -t hugetlbfs none "${HUGETLB_MOUNT}" || true
    runner_log "hugetlb-mount-end mount=${HUGETLB_MOUNT}"
  fi

  after_total="$(meminfo_value HugePages_Total || true)"
  after_free="$(meminfo_value HugePages_Free || true)"
  after_size="$(meminfo_value Hugepagesize || true)"
  runner_log "hugetlb after total=${after_total:-0} free=${after_free:-0} size_kb=${after_size:-0} mount=${HUGETLB_MOUNT}"
}

finish_run() {
  rc="$1"

  if [ "${rc}" -eq 0 ]; then
    echo "BERTMINI_PIPELINE_RUNTIME_PASS"
  else
    echo "BERTMINI_PIPELINE_RUNTIME_FAIL"
  fi

  sync || true

  if [ "${AUTO_POWEROFF}" = "0" ]; then
    echo "[bertmini] auto poweroff disabled; guest left running for manual inspection"
    echo "[bertmini] when ready, run: sync; poweroff"
    exit "${rc}"
  fi

  poweroff -f
  exit "${rc}"
}

if [ ! -x "${BIN}" ]; then
  write_runner_early_stage "missing-bin path=${BIN}"
  echo "missing runtime binary: ${BIN}"
  exit 1
fi
if [ "${GDBSERVER_ENABLE}" != "0" ] && [ ! -x "${GDBSERVER_TOOL}" ]; then
  write_runner_early_stage "missing-gdbserver path=${GDBSERVER_TOOL}"
  echo "missing gdbserver tool: ${GDBSERVER_TOOL}"
  exit 1
fi

write_runner_early_stage "after-bin-check path=${BIN}"
write_runner_early_stage "before-runner-enter"
runner_log "runner-enter batch=${TARGET_BATCH} methods=${METHODS}"
write_runner_stage "after-runner-enter batch=${TARGET_BATCH}"
append_runner_proc_stage "after-runner-enter"
resolve_manager_layout
write_runner_stage "after-resolve-manager-layout gemmini=${NUM_GEMMINI} dma=${NUM_DMA} pair=${PAIR_MANAGER_MODE}"
RUN_GOLDEN_CHECK_ENABLE="${GOLDEN_CHECK_ENABLE}"
if [ "${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}" != "0" ]; then
  RUN_GOLDEN_CHECK_ENABLE=0
fi
runner_log "runner-config pair=${PAIR_MANAGER_MODE} gemmini_base_id=${GEMMINI_BASE_ID} dma_base_id=${DMA_BASE_ID} trace_dir=${TRACE_DIR} trace_enable=${TRACE_ENABLE} export_dma_timeout_ms=${EXPORT_DMA_TIMEOUT_MS}"
runner_log "runner-dummy-config dummy=${DUMMY_GEMMINI_MODE} skip_model=${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD} skip_input=${PIPELINE_RUNTIME_SKIP_INPUT_LOAD} skip_golden=${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK} no_dma_compute=${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE}"
runner_log "runner-golden-check requested=${GOLDEN_CHECK_ENABLE} effective=${RUN_GOLDEN_CHECK_ENABLE}"
runner_log "runner-log-config profile=${PIPELINE_RUNTIME_LOG_PROFILE} deep_log_enable=${DEEP_LOG_ENABLE} dma_submit_trace_enable=${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE} dma_force_direct_enable=${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE} dma_blocking_wait_poll_timeout_enable=${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE} dma_export_chunk_log_stride=${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}"
runner_log "runner-gdb-config enable=${GDBSERVER_ENABLE} tool=${GDBSERVER_TOOL} endpoint=${GDBSERVER_ENDPOINT} info_path=${GDBSERVER_INFO_PATH} log_path=${GDBSERVER_LOG_PATH}"
runner_log "runner-gdb-marker-config enable=${PIPELINE_RUNTIME_GDB_MARKER_ENABLE} site=${PIPELINE_RUNTIME_GDB_MARKER_SITE:-any} segment=${PIPELINE_RUNTIME_GDB_MARKER_SEGMENT:-any} global_stage=${PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE:-any} local_stage=${PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE:-any} subbatch=${PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH:-any} manager=${PIPELINE_RUNTIME_GDB_MARKER_MANAGER:-any} tensor=${PIPELINE_RUNTIME_GDB_MARKER_TENSOR:-any} page=${PIPELINE_RUNTIME_GDB_MARKER_PAGE:-any} token=${PIPELINE_RUNTIME_GDB_MARKER_TOKEN:-any}"
mkdir -p "${TRACE_DIR}"
if [ "${TRACE_ENABLE}" != "0" ]; then
  runner_log "trace-dir-ready path=${TRACE_DIR}"
else
  runner_log "trace-disabled path=${TRACE_DIR}"
fi
runner_log "before-prepare-hugetlb"
write_runner_stage "before-prepare-hugetlb"
prepare_hugetlb
runner_log "after-prepare-hugetlb"
write_runner_stage "after-prepare-hugetlb"

fail=0
for method in ${METHODS}; do
  PIPELINE_YAML="${BERT_DIR}/pipeline_mapping.${TARGET_KEY}.${method}.yaml"
  GOLDEN_BIN="${BERT_DIR}/golden.${TARGET_KEY}.${method}.bin"
  TRACE_PATH="${TRACE_DIR}/${method}.trace"
  : > "${TRACE_PATH}"
  if [ "${GDBSERVER_ENABLE}" != "0" ]; then
    : > "${GDBSERVER_LOG_PATH}"
    write_gdbserver_info "prelaunch" "${method}" "0"
  fi
  set -- \
    --backend fpga \
    --model-yaml "${MODEL_YAML}" \
    --layer-mapping-yaml "${LAYER_MAPPING_YAML}" \
    --pipeline-yaml "${PIPELINE_YAML}" \
    --batch "${TARGET_BATCH}" \
    --num-cores "${NUM_CORES}" \
    --num-gemmini-mgrs "${NUM_GEMMINI}" \
    --num-dma-mgrs "${NUM_DMA}" \
    --pages-per-acc "${PAGES_PER_ACC}" \
    --gemmini-base-id "${GEMMINI_BASE_ID}" \
    --dma-base-id "${DMA_BASE_ID}" \
    --pair-manager-mode "${PAIR_MANAGER_MODE}" \
    --spm-pt-require-hugetlb 1 \
    --watchdog-ms "${WATCHDOG_MS}" \
    --export-dma-timeout-ms "${EXPORT_DMA_TIMEOUT_MS}"
  if [ "${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE}" != "0" ]; then
    set -- "$@" --no-dma-compute
  fi
  if [ "${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD}" != "0" ]; then
    set -- "$@" --skip-model-bin-load
  else
    set -- "$@" --model-bin "${MODEL_BIN}"
  fi
  if [ "${PIPELINE_RUNTIME_SKIP_INPUT_LOAD}" != "0" ]; then
    set -- "$@" --skip-input-load
  else
    set -- "$@" --input "${INPUT_BIN}"
  fi
  if [ "${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK}" != "0" ]; then
    set -- "$@" --skip-golden-check
  elif [ "${RUN_GOLDEN_CHECK_ENABLE}" != "0" ]; then
    set -- "$@" --golden "${GOLDEN_BIN}"
  fi
  if [ "${TRACE_ENABLE}" != "0" ]; then
    set -- "$@" --trace "${TRACE_PATH}"
  fi
  if [ "${DEEP_LOG_ENABLE}" != "0" ]; then
    set -- "$@" --deep-log-enable 1
    [ -n "${DEEP_LOG_SEGMENT}" ] && set -- "$@" --deep-log-segment "${DEEP_LOG_SEGMENT}"
    [ -n "${DEEP_LOG_GLOBAL_STAGE}" ] && set -- "$@" --deep-log-global-stage "${DEEP_LOG_GLOBAL_STAGE}"
    [ -n "${DEEP_LOG_LOCAL_STAGE}" ] && set -- "$@" --deep-log-local-stage "${DEEP_LOG_LOCAL_STAGE}"
    [ -n "${DEEP_LOG_SUBBATCH}" ] && set -- "$@" --deep-log-subbatch "${DEEP_LOG_SUBBATCH}"
    [ -n "${DEEP_LOG_STAGE_RADIUS}" ] && set -- "$@" --deep-log-stage-radius "${DEEP_LOG_STAGE_RADIUS}"
    [ -n "${DEEP_LOG_SUBBATCH_RADIUS}" ] && set -- "$@" --deep-log-subbatch-radius "${DEEP_LOG_SUBBATCH_RADIUS}"
  fi
  echo "[bertmini] method=${method} cores=${NUM_CORES} gemmini=${NUM_GEMMINI} dma=${NUM_DMA} pair=${PAIR_MANAGER_MODE}"
  echo "[bertmini] dummy-mode=${DUMMY_GEMMINI_MODE} skip-model=${PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD} skip-input=${PIPELINE_RUNTIME_SKIP_INPUT_LOAD} skip-golden=${PIPELINE_RUNTIME_SKIP_GOLDEN_CHECK} no-dma-compute=${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE}"
  echo "[bertmini] golden-check requested=${GOLDEN_CHECK_ENABLE} effective=${RUN_GOLDEN_CHECK_ENABLE}"
  echo "[bertmini] trace enable=${TRACE_ENABLE}"
  echo "[bertmini] dma submit trace enable=${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE} force direct enable=${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE} blocking wait poll timeout enable=${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE} export probe enable=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE} export probe token start=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START:-unset} export probe token end=${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END:-unset} export page start=${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START:-unset} export page end=${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END:-unset} export chunk stride=${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}"
  echo "[bertmini] deep-log enable=${DEEP_LOG_ENABLE} segment=${DEEP_LOG_SEGMENT:-any} global_stage=${DEEP_LOG_GLOBAL_STAGE:-any} local_stage=${DEEP_LOG_LOCAL_STAGE:-any} subbatch=${DEEP_LOG_SUBBATCH:-any} stage_radius=${DEEP_LOG_STAGE_RADIUS:-0} subbatch_radius=${DEEP_LOG_SUBBATCH_RADIUS:-0}"
  echo "[bertmini] breadcrumb enable=${PIPELINE_RUNTIME_BREADCRUMB_ENABLE} path=${PIPELINE_RUNTIME_BREADCRUMB_PATH} segment=${PIPELINE_RUNTIME_BREADCRUMB_SEGMENT:-any} global_stage=${PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE:-any} local_stage=${PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE:-any} subbatch=${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH:-any} stage_radius=${PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS:-0} subbatch_radius=${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS:-0}"
  echo "[bertmini] gdbserver enable=${GDBSERVER_ENABLE} endpoint=${GDBSERVER_ENDPOINT} info_path=${GDBSERVER_INFO_PATH} log_path=${GDBSERVER_LOG_PATH}"
  echo "[bertmini] gdb-marker enable=${PIPELINE_RUNTIME_GDB_MARKER_ENABLE} site=${PIPELINE_RUNTIME_GDB_MARKER_SITE:-any} segment=${PIPELINE_RUNTIME_GDB_MARKER_SEGMENT:-any} global_stage=${PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE:-any} local_stage=${PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE:-any} subbatch=${PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH:-any} manager=${PIPELINE_RUNTIME_GDB_MARKER_MANAGER:-any} tensor=${PIPELINE_RUNTIME_GDB_MARKER_TENSOR:-any} page=${PIPELINE_RUNTIME_GDB_MARKER_PAGE:-any} token=${PIPELINE_RUNTIME_GDB_MARKER_TOKEN:-any}"
  write_runner_stage "before-bin method=${method}"
  append_runner_proc_stage "before-bin method=${method}"
  if [ "${GDBSERVER_ENABLE}" != "0" ]; then
    PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE:-1}" \
      PIPELINE_RUNTIME_GUEST_LOG_PATH="${PIPELINE_RUNTIME_GUEST_LOG_PATH:-}" \
      PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE:-0}" \
      PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH:-}" \
      PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE:-0}" \
      PIPELINE_RUNTIME_AUDIT_LOG_PATH="${PIPELINE_RUNTIME_AUDIT_LOG_PATH:-}" \
      PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE="${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE:-0}" \
      PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH="${PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH:-}" \
    PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE="${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE}" \
      PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE}" \
      PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}" \
      PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE="${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END}" \
      PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE="${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}" \
      PIPELINE_RUNTIME_BREADCRUMB_ENABLE="${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}" \
      PIPELINE_RUNTIME_BREADCRUMB_PATH="${PIPELINE_RUNTIME_BREADCRUMB_PATH}" \
      PIPELINE_RUNTIME_BREADCRUMB_SEGMENT="${PIPELINE_RUNTIME_BREADCRUMB_SEGMENT}" \
      PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE}" \
      PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE}" \
      PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH}" \
      PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS}" \
      PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS}" \
      PIPELINE_RUNTIME_GDB_MARKER_ENABLE="${PIPELINE_RUNTIME_GDB_MARKER_ENABLE}" \
      PIPELINE_RUNTIME_GDB_MARKER_SITE="${PIPELINE_RUNTIME_GDB_MARKER_SITE}" \
      PIPELINE_RUNTIME_GDB_MARKER_SEGMENT="${PIPELINE_RUNTIME_GDB_MARKER_SEGMENT}" \
      PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE="${PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE}" \
      PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE="${PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE}" \
      PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH="${PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH}" \
      PIPELINE_RUNTIME_GDB_MARKER_MANAGER="${PIPELINE_RUNTIME_GDB_MARKER_MANAGER}" \
      PIPELINE_RUNTIME_GDB_MARKER_TENSOR="${PIPELINE_RUNTIME_GDB_MARKER_TENSOR}" \
      PIPELINE_RUNTIME_GDB_MARKER_PAGE="${PIPELINE_RUNTIME_GDB_MARKER_PAGE}" \
      PIPELINE_RUNTIME_GDB_MARKER_TOKEN="${PIPELINE_RUNTIME_GDB_MARKER_TOKEN}" \
      sh -c '
        once_flag="$1"
        tool_path="$2"
        endpoint="$3"
        bin_path="$4"
        shift 4
        if [ "${once_flag}" != "0" ]; then
          exec "${tool_path}" --once "${endpoint}" "${bin_path}" "$@"
        fi
        exec "${tool_path}" "${endpoint}" "${bin_path}" "$@"
      ' sh \
      "${GDBSERVER_ONCE}" \
      "${GDBSERVER_TOOL}" \
      "${GDBSERVER_ENDPOINT}" \
      "${BIN}" "$@" \
      >> "${GDBSERVER_LOG_PATH}" 2>&1 &
  else
    PIPELINE_RUNTIME_GUEST_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_LOG_ENABLE:-1}" \
      PIPELINE_RUNTIME_GUEST_LOG_PATH="${PIPELINE_RUNTIME_GUEST_LOG_PATH:-}" \
      PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE:-0}" \
      PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH="${PIPELINE_RUNTIME_GUEST_DEEP_LOG_PATH:-}" \
      PIPELINE_RUNTIME_AUDIT_LOG_ENABLE="${PIPELINE_RUNTIME_AUDIT_LOG_ENABLE:-0}" \
      PIPELINE_RUNTIME_AUDIT_LOG_PATH="${PIPELINE_RUNTIME_AUDIT_LOG_PATH:-}" \
      PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE="${PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE:-0}" \
      PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH="${PIPELINE_RUNTIME_CHECKPOINT_LOG_PATH:-}" \
    PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE="${PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE}" \
      PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE="${PIPELINE_RUNTIME_DMA_SUBMIT_TRACE_ENABLE}" \
      PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE="${PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE}" \
      PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE="${PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_ENABLE}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_START}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END="${PIPELINE_RUNTIME_DMA_EXPORT_PROBE_TOKEN_END}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_START}" \
      PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END="${PIPELINE_RUNTIME_DMA_EXPORT_PAGE_END}" \
      PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE="${PIPELINE_RUNTIME_DMA_EXPORT_CHUNK_LOG_STRIDE}" \
      PIPELINE_RUNTIME_BREADCRUMB_ENABLE="${PIPELINE_RUNTIME_BREADCRUMB_ENABLE}" \
      PIPELINE_RUNTIME_BREADCRUMB_PATH="${PIPELINE_RUNTIME_BREADCRUMB_PATH}" \
      PIPELINE_RUNTIME_BREADCRUMB_SEGMENT="${PIPELINE_RUNTIME_BREADCRUMB_SEGMENT}" \
      PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_GLOBAL_STAGE}" \
      PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE="${PIPELINE_RUNTIME_BREADCRUMB_LOCAL_STAGE}" \
      PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH}" \
      PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_STAGE_RADIUS}" \
      PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS="${PIPELINE_RUNTIME_BREADCRUMB_SUBBATCH_RADIUS}" \
      PIPELINE_RUNTIME_GDB_MARKER_ENABLE="${PIPELINE_RUNTIME_GDB_MARKER_ENABLE}" \
      PIPELINE_RUNTIME_GDB_MARKER_SITE="${PIPELINE_RUNTIME_GDB_MARKER_SITE}" \
      PIPELINE_RUNTIME_GDB_MARKER_SEGMENT="${PIPELINE_RUNTIME_GDB_MARKER_SEGMENT}" \
      PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE="${PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE}" \
      PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE="${PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE}" \
      PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH="${PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH}" \
      PIPELINE_RUNTIME_GDB_MARKER_MANAGER="${PIPELINE_RUNTIME_GDB_MARKER_MANAGER}" \
      PIPELINE_RUNTIME_GDB_MARKER_TENSOR="${PIPELINE_RUNTIME_GDB_MARKER_TENSOR}" \
      PIPELINE_RUNTIME_GDB_MARKER_PAGE="${PIPELINE_RUNTIME_GDB_MARKER_PAGE}" \
      PIPELINE_RUNTIME_GDB_MARKER_TOKEN="${PIPELINE_RUNTIME_GDB_MARKER_TOKEN}" \
      "${BIN}" "$@" &
  fi
  bin_pid=$!
  tracked_pid="${bin_pid}"
  tracked_kind="launcher"
  if [ "${GDBSERVER_ENABLE}" != "0" ]; then
    tracked_kind="gdbserver"
    write_gdbserver_info "listening" "${method}" "${bin_pid}"
  fi
  write_runner_stage "after-bin-spawn method=${method} pid=${bin_pid} kind=${tracked_kind}"
  append_proc_stage "${tracked_pid}" "after-bin-spawn method=${method} kind=${tracked_kind}"
  append_ptrace_probe "${tracked_pid}" "after-bin-spawn method=${method} kind=${tracked_kind}"
  schedule_delayed_proc_probes "${bin_pid}" "${method}"
  bin_poll_idx=0
  while kill -0 "${bin_pid}" 2>/dev/null; do
    if [ "${GDBSERVER_ENABLE}" != "0" ]; then
      inferior_pid="$(gdbserver_inferior_pid "${bin_pid}")"
      if [ -n "${inferior_pid}" ] && [ "${inferior_pid}" != "${tracked_pid}" ]; then
        tracked_pid="${inferior_pid}"
        tracked_kind="inferior"
        write_runner_stage "gdbserver-inferior method=${method} pid=${tracked_pid} parent=${bin_pid}"
        append_proc_stage "${tracked_pid}" "gdbserver-inferior method=${method} parent=${bin_pid}"
        append_ptrace_probe "${tracked_pid}" "gdbserver-inferior method=${method} parent=${bin_pid}"
        write_gdbserver_info "inferior" "${method}" "${tracked_pid}" "gdbserver_pid=${bin_pid}"
      fi
    fi
    bin_poll_idx=$((bin_poll_idx + 1))
    append_proc_stage "${tracked_pid}" "bin-poll-${bin_poll_idx} method=${method} kind=${tracked_kind}"
    sleep "${RUNNER_BIN_PROC_POLL_SECONDS}"
  done
  cleanup_delayed_proc_probes
  if ! wait "${bin_pid}"; then
    fail=1
    append_proc_stage "${tracked_pid}" "after-bin-wait method=${method} kind=${tracked_kind} rc=nonzero"
    append_ptrace_probe "${tracked_pid}" "after-bin-wait method=${method} kind=${tracked_kind} rc=nonzero"
    if [ "${GDBSERVER_ENABLE}" != "0" ]; then
      write_gdbserver_info "exit-nonzero" "${method}" "${tracked_pid}" "gdbserver_pid=${bin_pid}"
    fi
    write_runner_stage "after-bin method=${method} rc=nonzero"
    echo "[bertmini] FAIL method=${method}"
    break
  fi
  append_proc_stage "${tracked_pid}" "after-bin-wait method=${method} kind=${tracked_kind} rc=0"
  append_ptrace_probe "${tracked_pid}" "after-bin-wait method=${method} kind=${tracked_kind} rc=0"
  if [ "${GDBSERVER_ENABLE}" != "0" ]; then
    write_gdbserver_info "exit-0" "${method}" "${tracked_pid}" "gdbserver_pid=${bin_pid}"
  fi
  write_runner_stage "after-bin method=${method} rc=0"
  echo "[bertmini] PASS method=${method}"
done

if [ "${fail}" -eq 0 ]; then
  finish_run 0
fi

finish_run 1
