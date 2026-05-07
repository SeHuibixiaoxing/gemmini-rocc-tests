#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_init_marker_chain.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Run the pairdummy cfg32 marker helper and, after the first
artifact-mapping-parse-done marker, advance the in-guest marker filter through
runtime initialization boundaries in the same GDB session.

The guest image must have been built with:

  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=artifact-mapping-parse-done

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
cmd_file="$(mktemp "${cy_dir}/tmp/pairdummy-init-marker-chain.XXXXXX.gdb")"

cleanup() {
  rm -f "${cmd_file}"
}
trap cleanup EXIT

cat > "${cmd_file}" <<'EOF'
printf "\n--- init marker chain: artifact-validate-done ---\n"
set variable g_prt_gdb_marker_filter.site_id = 15
continue
print g_prt_gdb_marker_state
bt
info registers pc sp ra

printf "\n--- init marker chain: synthetic-model-prefault-begin ---\n"
set variable g_prt_gdb_marker_filter.site_id = 16
continue
print g_prt_gdb_marker_state
bt
info registers pc sp ra

printf "\n--- init marker chain: synthetic-model-prefault-end ---\n"
set variable g_prt_gdb_marker_filter.site_id = 17
continue
print g_prt_gdb_marker_state
bt
info registers pc sp ra

printf "\n--- init marker chain: synthetic-model-ready ---\n"
set variable g_prt_gdb_marker_filter.site_id = 18
continue
print g_prt_gdb_marker_state
bt
info registers pc sp ra

printf "\n--- init marker chain: runtime-ready ---\n"
set variable g_prt_gdb_marker_filter.site_id = 1
continue
print g_prt_gdb_marker_state
bt
info registers pc sp ra
EOF

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-0}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-2400}"
export PRT_GDB_POST_MARKER_GDB_FILE="${cmd_file}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_stop.sh" "$@"
