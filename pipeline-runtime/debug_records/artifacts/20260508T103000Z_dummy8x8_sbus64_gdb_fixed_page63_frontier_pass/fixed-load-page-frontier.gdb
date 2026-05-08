printf "\n--- fixed-load marker expectation ---\n"
print g_prt_gdb_marker_state.site_id
print g_prt_gdb_marker_state.local_stage_id
print g_prt_gdb_marker_state.manager_id
print g_prt_gdb_marker_state.tensor_id
print g_prt_gdb_marker_state.page_idx
print g_prt_gdb_marker_state.hit_count
if g_prt_gdb_marker_state.local_stage_id != 0
  printf "WARNING: marker local_stage mismatch\n"
end
if g_prt_gdb_marker_state.manager_id != 0
  printf "WARNING: marker manager mismatch\n"
end
if g_prt_gdb_marker_state.tensor_id != 1000001
  printf "WARNING: marker tensor mismatch\n"
end
if g_prt_gdb_marker_state.page_idx != 63
  printf "WARNING: marker page mismatch\n"
end

printf "\n--- immediate submitwait entry ---\n"
tbreak dma_submit_wait_annotated_scoped
continue
bt 8
info args
print stage_idx
print tensor_id
print debug_page_idx
print/x req
print *req
x/10i $pc

printf "\n--- prt_dma_submit entry ---\n"
tbreak prt_dma_submit
continue
bt 8
info args
print/x req
print *req
print/x tok
x/10i $pc

printf "\n--- dma_blocking_wait entry ---\n"
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
set $tokp = tok
x/10i $pc

printf "\n--- before hw_dma_fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3695
continue
bt 8
info args
print $tokp->id
print $tokp->debug_page_idx
print $tokp->rr_manager_id
print $tokp->rr_scope_valid
print $tokp->rr_scope_external
print $tokp->hw_done_flag
print/x $tokp->completion_flag
x/wx $tokp->completion_flag
print/x $tokp->debug_src_addr
print/x $tokp->debug_dst_addr
print/x $tokp->debug_done_flag_pa
print $tokp->debug_bytes
x/10i $pc

printf "\n--- after hw_dma_fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3699
continue
bt 8
info args
print $tokp->id
print $tokp->debug_page_idx
print $tokp->hw_done_flag
print/x $tokp->completion_flag
x/wx $tokp->completion_flag
x/10i $pc

printf "\n--- before shared fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3774
continue
bt 8
info args
print $tokp->id
print $tokp->debug_page_idx
print $tokp->rr_manager_id
print $tokp->rr_scope_valid
print $tokp->rr_scope_external
x/10i $pc

printf "\n--- wait return marker ---\n"
tbreak dma_gdb_marker_wait_return
continue
bt 8
info args
print rc
print $tokp->id
print $tokp->debug_page_idx
print $tokp->rr_manager_id
print $tokp->rr_scope_valid
print $tokp->rr_scope_external
print $tokp->hw_done_flag
print $tokp->done
print $tokp->status
x/10i $pc

printf "\n--- fixed-load submitwait end marker call ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:867
continue
bt 8
info locals
x/10i $pc

printf "\n--- fixed-load page accounted marker call ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:898
continue
bt 8
info locals
x/10i $pc

printf "\n--- fixed-load batch scope release call-site ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:905
continue
bt 8
info locals
x/10i $pc

printf "\n--- prt_rr_release_scope entry ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:388
continue
bt 8
info args
print *scope
x/10i $pc

printf "\n--- prt_rr_release_scope after readback ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:404
continue
bt 8
info args
print *scope
x/10i $pc

printf "\n--- fixed-load return from batch scope release ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:906
continue
bt 8
info locals
x/10i $pc

printf "\n--- stage_prepare_exec_views after fixed-load copy call ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2231
continue
bt 8
info locals
x/10i $pc

printf "\n--- stage-fixed-load-sparse end log call-site ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:2240
continue
bt 8
info locals
x/10i $pc
