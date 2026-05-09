#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_no_dma_post_compute_thin.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to a pairdummy cfg32 gdbserver run prepared with no-DMA compute and the
segment1/stage0/subbatch3 worker-export-sync marker:

  PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
  PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-export-sync
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
  PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=1
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
  PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3

This is the low-overhead follow-up to the compute thin frontier. It starts after
prt_gemm_conv_run has returned, prints the sync_stage_export_aliases entry once,
and stops at the first instruction after sync_stage_export_aliases returns.
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
gdb_file="${script_dir}/gdb/sbus64_no_dma_segment1_stage0_post_compute_thin.gdb"

if [[ ! -f "${gdb_file}" ]]; then
  echo "missing GDB command file: ${gdb_file}" >&2
  exit 1
fi

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-1}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-1800}"
export PRT_GDB_FRONTIER_TIMEOUT="${PRT_GDB_FRONTIER_TIMEOUT:-900}"
export PRT_GDB_DETACH="${PRT_GDB_DETACH:-1}"
export PRT_GDB_FRONTIER_GDB_FILE="${PRT_GDB_FRONTIER_GDB_FILE:-${gdb_file}}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_frontier_expect.sh" "$@"
