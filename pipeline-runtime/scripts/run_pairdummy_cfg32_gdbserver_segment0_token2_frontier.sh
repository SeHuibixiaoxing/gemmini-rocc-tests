#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_segment0_token2_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to a pairdummy cfg32 gdbserver run and narrow the segment0/stage0
entry-DMA frontier immediately after tensor0 token1. The helper starts from the
initial segment-begin marker, waits for token1 wait-return, then uses temporary
software breakpoints to cross the token1 return/cleanup/page-loop path before
waiting for token2 wait-enter and wait-return.

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
cmd_file="$(mktemp "${cy_dir}/tmp/pairdummy-segment0-token2-frontier.XXXXXX.gdb")"

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
set variable g_prt_gdb_marker_filter.tensor_id = 0
set variable g_prt_gdb_marker_filter.page_idx = 4294967295

printf "\n--- token2 frontier: token1 wait-return stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 1
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- token2 frontier: after dma_gdb_marker_wait_return returns ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3832
continue
bt
info threads
info registers pc sp ra
info args
info locals

printf "\n--- token2 frontier: after prt_dma_wait returns to submit_wait ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:2092
continue
bt
info threads
info registers pc sp ra
frame 1
info args
info locals

printf "\n--- token2 frontier: before token cleanup breadcrumb ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:2124
continue
bt
info threads
info registers pc sp ra
info args
info locals

printf "\n--- token2 frontier: back in fixed-load page loop after submitwait ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:814
continue
bt
info threads
info registers pc sp ra
info args
info locals

printf "\n--- token2 frontier: page0 accounting complete ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:840
continue
bt
info threads
info registers pc sp ra
info args
info locals

printf "\n--- token2 frontier: next page submitwait call ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:813
continue
bt
info threads
info registers pc sp ra
info args
info locals

printf "\n--- token2 frontier: token2 wait-enter stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 12
set variable g_prt_gdb_marker_filter.token_id = 2
set variable g_prt_gdb_marker_filter.page_idx = 4294967295
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- token2 frontier: token2 wait-return stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 2
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- token2 frontier: token4 wait-return stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 4
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- token2 frontier: token8 wait-return stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.token_id = 8
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra

printf "\n--- token2 frontier: token16 wait-return stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.token_id = 16
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
