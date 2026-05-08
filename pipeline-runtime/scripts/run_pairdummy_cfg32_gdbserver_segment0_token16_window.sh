#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_segment0_token16_window.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to a pairdummy cfg32 gdbserver run and inspect the segment0/stage0
tensor0 fixed-load window after the last proven token16/page15 marker.

The helper starts from the initial segment-begin marker, jumps to token16
wait-return, verifies that GDB can finish out of prt_gdb_marker_stop(), then
walks token16 cleanup plus page16..page23 markers. It avoids source-line
temporary breakpoints and optimized locals; all target stops are semantic GDB
marker sites.

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
cmd_file="$(mktemp "${cy_dir}/tmp/pairdummy-segment0-token16-window.XXXXXX.gdb")"

cleanup() {
  rm -f "${cmd_file}"
}
trap cleanup EXIT

cat > "${cmd_file}" <<'EOF'
define prt_show_marker
  print g_prt_gdb_marker_state
  bt
  info threads
  info registers pc sp ra
end

set variable g_prt_gdb_marker_filter.segment_idx = 0
set variable g_prt_gdb_marker_filter.global_stage_id = 4294967295
set variable g_prt_gdb_marker_filter.local_stage_id = 0
set variable g_prt_gdb_marker_filter.subbatch_id = 4294967295
set variable g_prt_gdb_marker_filter.manager_id = 4294967295
set variable g_prt_gdb_marker_filter.tensor_id = 0
set variable g_prt_gdb_marker_filter.page_idx = 4294967295

printf "\n--- token16 window: token16/page15 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 16
set variable g_prt_gdb_marker_filter.page_idx = 15
continue
prt_show_marker

printf "\n--- token16 window: finish out of marker stop ---\n"
finish
bt
info registers pc sp ra

printf "\n--- token16 window: token16 after wait ---\n"
set variable g_prt_gdb_marker_filter.site_id = 25
set variable g_prt_gdb_marker_filter.token_id = 16
set variable g_prt_gdb_marker_filter.page_idx = 15
continue
prt_show_marker

printf "\n--- token16 window: token16 after cleanup ---\n"
set variable g_prt_gdb_marker_filter.site_id = 26
set variable g_prt_gdb_marker_filter.token_id = 16
set variable g_prt_gdb_marker_filter.page_idx = 15
continue
prt_show_marker

printf "\n--- token16 window: page15 submitwait end ---\n"
set variable g_prt_gdb_marker_filter.site_id = 28
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 15
continue
prt_show_marker

printf "\n--- token16 window: page15 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 15
continue
prt_show_marker

printf "\n--- token16 window: page16 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 16
continue
prt_show_marker

printf "\n--- token16 window: token17/page16 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 17
set variable g_prt_gdb_marker_filter.page_idx = 16
continue
prt_show_marker

printf "\n--- token16 window: page16 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 16
continue
prt_show_marker

printf "\n--- token16 window: page17 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 17
continue
prt_show_marker

printf "\n--- token16 window: token18/page17 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 18
set variable g_prt_gdb_marker_filter.page_idx = 17
continue
prt_show_marker

printf "\n--- token16 window: page17 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 17
continue
prt_show_marker

printf "\n--- token16 window: page18 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 18
continue
prt_show_marker

printf "\n--- token16 window: token19/page18 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 19
set variable g_prt_gdb_marker_filter.page_idx = 18
continue
prt_show_marker

printf "\n--- token16 window: page18 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 18
continue
prt_show_marker

printf "\n--- token16 window: page19 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 19
continue
prt_show_marker

printf "\n--- token16 window: token20/page19 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 20
set variable g_prt_gdb_marker_filter.page_idx = 19
continue
prt_show_marker

printf "\n--- token16 window: page19 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 19
continue
prt_show_marker

printf "\n--- token16 window: page20 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 20
continue
prt_show_marker

printf "\n--- token16 window: token21/page20 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 21
set variable g_prt_gdb_marker_filter.page_idx = 20
continue
prt_show_marker

printf "\n--- token16 window: page20 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 20
continue
prt_show_marker

printf "\n--- token16 window: page21 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 21
continue
prt_show_marker

printf "\n--- token16 window: token22/page21 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 22
set variable g_prt_gdb_marker_filter.page_idx = 21
continue
prt_show_marker

printf "\n--- token16 window: page21 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 21
continue
prt_show_marker

printf "\n--- token16 window: page22 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 22
continue
prt_show_marker

printf "\n--- token16 window: token23/page22 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 23
set variable g_prt_gdb_marker_filter.page_idx = 22
continue
prt_show_marker

printf "\n--- token16 window: page22 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 22
continue
prt_show_marker

printf "\n--- token16 window: page23 submitwait begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 27
set variable g_prt_gdb_marker_filter.page_idx = 23
continue
prt_show_marker

printf "\n--- token16 window: token24/page23 wait-return ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 24
set variable g_prt_gdb_marker_filter.page_idx = 23
continue
prt_show_marker

printf "\n--- token16 window: page23 accounted ---\n"
set variable g_prt_gdb_marker_filter.site_id = 29
set variable g_prt_gdb_marker_filter.token_id = 4294967295
set variable g_prt_gdb_marker_filter.page_idx = 23
continue
prt_show_marker
EOF

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-0}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-540}"
export PRT_GDB_POST_MARKER_GDB_FILE="${cmd_file}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_stop.sh" "$@"
