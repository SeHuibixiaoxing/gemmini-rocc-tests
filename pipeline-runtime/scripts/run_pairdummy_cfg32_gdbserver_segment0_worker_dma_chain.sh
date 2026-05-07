#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Run the pairdummy cfg32 marker helper and, after the first segment-begin marker,
advance the in-guest marker filter through the segment0/stage0 worker, compute,
export, and first DMA page path in the same GDB session.

The guest image should be built with at least:

  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=segment-begin
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=0

This helper does not probe the gdbserver port; the first TCP client remains GDB.
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
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"
cmd_file="$(mktemp "${cy_dir}/tmp/pairdummy-segment0-worker-dma-chain.XXXXXX.gdb")"

cleanup() {
  rm -f "${cmd_file}"
}
trap cleanup EXIT

cat > "${cmd_file}" <<'EOF'
set variable g_prt_gdb_marker_filter.segment_idx = 0
set variable g_prt_gdb_marker_filter.global_stage_id = 4294967295
set variable g_prt_gdb_marker_filter.local_stage_id = 0
set variable g_prt_gdb_marker_filter.subbatch_id = 4294967295
set variable g_prt_gdb_marker_filter.manager_id = 4294967295
set variable g_prt_gdb_marker_filter.tensor_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 4294967295
set variable g_prt_gdb_marker_filter.token_id = 4294967295

printf "\n--- segment0 chain: worker-create stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 3
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: worker-entry stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 4
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: first dma-wait-enter stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 12
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: first dma-wait-return stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: worker-gemm-run stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 5
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: worker-export-sync stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 6
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: export-sync-tensor stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 7
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: export-alias-target-begin stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 8
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: dma-export-page-submit-begin stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 10
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: export dma-wait-enter stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 12
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: export dma-wait-return stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: dma-export-page-submit-end stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 11
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- segment0 chain: export-alias-target-end stage0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 9
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra
EOF

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-0}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-2400}"
export PRT_GDB_POST_MARKER_GDB_FILE="${cmd_file}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_stop.sh" "$@"
