#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_no_dma_segment2_stage1_gemm_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to a pairdummy cfg32 gdbserver run prepared with no-DMA compute and the
segment2/stage1/subbatch0 worker-gemm-run marker:

  PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
  PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
  PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=3
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=1
  PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0

If the marker is missed, the generic expect wrapper captures all thread stacks
at timeout. If the marker hits, this helper follows only that worker thread
through GEMM return, export-sync return, and final worker-done.
EOF
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
gdb_file="${script_dir}/gdb/sbus64_no_dma_segment2_stage1_gemm_frontier.gdb"

if [[ ! -f "${gdb_file}" ]]; then
  echo "missing GDB command file: ${gdb_file}" >&2
  exit 1
fi

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-1}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-2400}"
export PRT_GDB_FRONTIER_TIMEOUT="${PRT_GDB_FRONTIER_TIMEOUT:-1800}"
export PRT_GDB_DETACH="${PRT_GDB_DETACH:-1}"
export PRT_GDB_FRONTIER_GDB_FILE="${PRT_GDB_FRONTIER_GDB_FILE:-${gdb_file}}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_frontier_expect.sh" "$@"
