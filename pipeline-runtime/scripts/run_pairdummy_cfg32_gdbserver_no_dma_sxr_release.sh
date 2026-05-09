#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_no_dma_sxr_release.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to a pairdummy cfg32 gdbserver run prepared with no-DMA compute and the
segment1/stage0/subbatch3 worker-before-build-stage-task marker:

  PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
  PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-before-build-stage-task
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
  PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=3

The helper reuses run_pairdummy_cfg32_gdbserver_marker_stop.sh, then runs a
narrow post-marker GDB ladder that stops only on
prt_rr_release_scope(scope={manager=6,cfg=31,opcode=3}) and separates the
release CSR write, post-release CSR readback, opcode restore, and xlate flush
return boundaries.

It does not probe the guest TCP port; the first TCP client remains GDB.
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
gdb_file="${script_dir}/gdb/sbus64_no_dma_segment1_stage0_sxr_release.gdb"

if [[ ! -f "${gdb_file}" ]]; then
  echo "missing GDB command file: ${gdb_file}" >&2
  exit 1
fi

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-1}"
export PRT_GDB_MARKER_DETACH="${PRT_GDB_MARKER_DETACH:-0}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-1800}"
export PRT_GDB_POST_MARKER_GDB_FILE="${PRT_GDB_POST_MARKER_GDB_FILE:-${gdb_file}}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_stop.sh" "$@"
