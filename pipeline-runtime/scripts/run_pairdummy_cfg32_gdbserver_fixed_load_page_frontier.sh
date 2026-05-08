#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_fixed_load_page_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach to the cfg32 NIC gdbserver workload after the guest-side GDB marker has
been configured to stop at one fixed-load DMA page. The helper breaks only on
prt_gdb_marker_stop(), then arms one-shot breakpoints for the immediate
dma_submit_wait_annotated_scoped() path. This avoids a hot conditional
breakpoint on every DMA page.

The guest image must be prepared with matching marker env, for example:

  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-fixed-load-submitwait-begin
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
  PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
  PIPELINE_RUNTIME_GDB_MARKER_TENSOR=1000001
  PIPELINE_RUNTIME_GDB_MARKER_PAGE=63

Environment:
  PRT_GDB_FIXED_LOAD_STAGE    Expected local stage. Default: 0.
  PRT_GDB_FIXED_LOAD_MANAGER  Expected DMA manager. Default: 0.
  PRT_GDB_FIXED_LOAD_TENSOR   Expected tensor id. Default: 1000001.
  PRT_GDB_FIXED_LOAD_PAGE     Expected page index. Default: 63.
  PRT_GDB_MARKER_TIMEOUT      Whole GDB session timeout. Default: 1800.
  PRT_GDB_STATIC_NEIGH_MAC    Optional static neighbor MAC for the guest.
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

stage="${PRT_GDB_FIXED_LOAD_STAGE:-0}"
manager="${PRT_GDB_FIXED_LOAD_MANAGER:-0}"
tensor="${PRT_GDB_FIXED_LOAD_TENSOR:-1000001}"
page="${PRT_GDB_FIXED_LOAD_PAGE:-63}"
for numeric in stage manager tensor page; do
  value="${!numeric}"
  if [[ ! "${value}" =~ ^[0-9]+$ ]]; then
    echo "invalid ${numeric}=${value}" >&2
    exit 2
  fi
done

dma_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c"
rr_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c"
runtime_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c"
stamp="$(date -u +%Y%m%dT%H%M%SZ)"
cmd_dir="${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests/fixed-load-page-frontier-${stamp}"
post_cmds="${cmd_dir}/fixed-load-page-frontier.gdb"

mkdir -p "${cmd_dir}"

cat > "${post_cmds}" <<EOF
printf "\\n--- fixed-load marker expectation ---\\n"
print g_prt_gdb_marker_state.site_id
print g_prt_gdb_marker_state.local_stage_id
print g_prt_gdb_marker_state.manager_id
print g_prt_gdb_marker_state.tensor_id
print g_prt_gdb_marker_state.page_idx
print g_prt_gdb_marker_state.hit_count
if g_prt_gdb_marker_state.local_stage_id != ${stage}
  printf "WARNING: marker local_stage mismatch\\n"
end
if g_prt_gdb_marker_state.manager_id != ${manager}
  printf "WARNING: marker manager mismatch\\n"
end
if g_prt_gdb_marker_state.tensor_id != ${tensor}
  printf "WARNING: marker tensor mismatch\\n"
end
if g_prt_gdb_marker_state.page_idx != ${page}
  printf "WARNING: marker page mismatch\\n"
end

printf "\\n--- immediate submitwait entry ---\\n"
tbreak dma_submit_wait_annotated_scoped
continue
bt 8
info args
print stage_idx
print tensor_id
print debug_page_idx
print/x req
print *req
x/10i \$pc

printf "\\n--- prt_dma_submit entry ---\\n"
tbreak prt_dma_submit
continue
bt 8
info args
print/x req
print *req
print/x tok
x/10i \$pc

printf "\\n--- dma_blocking_wait entry ---\\n"
tbreak dma_blocking_wait
continue
bt 8
info args
print tok->id
print tok->stage_idx
print tok->tensor_id
print tok->debug_page_idx
print tok->rr_manager_id
print tok->rr_scope_valid
print tok->rr_scope_external
print tok->hw_done_flag
print/x tok->completion_flag
x/wx tok->completion_flag
print/x tok->debug_src_addr
print/x tok->debug_dst_addr
print/x tok->debug_done_flag_pa
print tok->debug_bytes
set \$tokp = tok
x/10i \$pc

printf "\\n--- before hw_dma_fence ---\\n"
tbreak ${dma_src}:3695
continue
bt 8
info args
print \$tokp->id
print \$tokp->debug_page_idx
print \$tokp->rr_manager_id
print \$tokp->rr_scope_valid
print \$tokp->rr_scope_external
print \$tokp->hw_done_flag
print/x \$tokp->completion_flag
x/wx \$tokp->completion_flag
print/x \$tokp->debug_src_addr
print/x \$tokp->debug_dst_addr
print/x \$tokp->debug_done_flag_pa
print \$tokp->debug_bytes
x/10i \$pc

printf "\\n--- after hw_dma_fence ---\\n"
tbreak ${dma_src}:3699
continue
bt 8
info args
print fence_status
print \$tokp->id
print \$tokp->debug_page_idx
print \$tokp->hw_done_flag
print/x \$tokp->completion_flag
x/wx \$tokp->completion_flag
x/10i \$pc

printf "\\n--- before shared fence ---\\n"
tbreak ${dma_src}:3774
continue
bt 8
info args
print \$tokp->id
print \$tokp->debug_page_idx
print \$tokp->rr_manager_id
print \$tokp->rr_scope_valid
print \$tokp->rr_scope_external
x/10i \$pc

printf "\\n--- wait return marker ---\\n"
tbreak dma_gdb_marker_wait_return
continue
bt 8
info args
print rc
print \$tokp->id
print \$tokp->debug_page_idx
print \$tokp->rr_manager_id
print \$tokp->rr_scope_valid
print \$tokp->rr_scope_external
print \$tokp->hw_done_flag
print \$tokp->done
print \$tokp->status
x/10i \$pc

printf "\\n--- fixed-load submitwait end marker call ---\\n"
tbreak ${dma_src}:867
continue
bt 8
info locals
x/10i \$pc

printf "\\n--- fixed-load page accounted marker call ---\\n"
tbreak ${dma_src}:898
continue
bt 8
info locals
x/10i \$pc

printf "\\n--- fixed-load batch scope release call-site ---\\n"
tbreak ${dma_src}:905
continue
bt 8
info locals
x/10i \$pc

printf "\\n--- dma_batch_scope_release entry ---\\n"
tbreak dma_batch_scope_release
continue
bt 8
info args
print *scope
x/10i \$pc

printf "\\n--- prt_rr_release_scope entry ---\\n"
tbreak prt_rr_release_scope
continue
bt 8
info args
print *scope
x/10i \$pc

printf "\\n--- prt_rr_release_scope after readback ---\\n"
tbreak ${rr_src}:404
continue
bt 8
info args
print *scope
x/10i \$pc

printf "\\n--- fixed-load return from batch scope release ---\\n"
tbreak ${dma_src}:906
continue
bt 8
info locals
x/10i \$pc

printf "\\n--- stage_prepare_exec_views after fixed-load copy call ---\\n"
tbreak ${runtime_src}:2231
continue
bt 8
info locals
x/10i \$pc

printf "\\n--- stage-fixed-load-sparse end log call-site ---\\n"
tbreak ${runtime_src}:2240
continue
bt 8
info locals
x/10i \$pc
EOF

echo "[pairdummy-fixed-load-frontier] post_cmds=${post_cmds}"
echo "[pairdummy-fixed-load-frontier] expected stage=${stage} manager=${manager} tensor=${tensor} page=${page}"

export PRT_GDB_MARKER_DELETE_AFTER_HIT="${PRT_GDB_MARKER_DELETE_AFTER_HIT:-1}"
export PRT_GDB_MARKER_TIMEOUT="${PRT_GDB_MARKER_TIMEOUT:-1800}"
export PRT_GDB_POST_MARKER_GDB_FILE="${post_cmds}"

exec "${script_dir}/run_pairdummy_cfg32_gdbserver_marker_stop.sh" "$@"
