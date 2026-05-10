#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_no_dma_segment2_stage2_c7_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to a pairdummy cfg32 gdbserver run prepared with no-DMA compute and the
segment2/stage2 worker-entry marker:

  PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
  PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-entry
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=2
  PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=4
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=2
  PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=any

This helper follows the stage2 worker through the tensor4 ALL_RINGBUFFER C7
wait and also observes the stage0 tensor4 C8 producer if it runs in the same
window. The final stop is the stage2 GEMM-run marker line.

Do not use PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=0 with worker-entry. That
marker fires before the worker loop installs a concrete subbatch id in TLS, so
an exact subbatch filter can silently filter out the intended entry marker.
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
gdb_file="${script_dir}/gdb/sbus64_no_dma_segment2_stage2_c7_frontier.gdb"

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
