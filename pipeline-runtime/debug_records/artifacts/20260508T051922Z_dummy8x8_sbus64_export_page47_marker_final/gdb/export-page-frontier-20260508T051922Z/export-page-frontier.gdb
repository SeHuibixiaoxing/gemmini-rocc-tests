printf "\n--- export marker expectation ---\n"
print g_prt_gdb_marker_state.site_id
print g_prt_gdb_marker_state.local_stage_id
print g_prt_gdb_marker_state.manager_id
print g_prt_gdb_marker_state.tensor_id
print g_prt_gdb_marker_state.page_idx
print g_prt_gdb_marker_state.token_id
print/x g_prt_gdb_marker_state.aux0
print/x g_prt_gdb_marker_state.aux1
print g_prt_gdb_marker_state.hit_count
if g_prt_gdb_marker_state.local_stage_id != 0
  printf "WARNING: marker local_stage mismatch\n"
end
if g_prt_gdb_marker_state.manager_id != 0
  printf "WARNING: marker manager mismatch\n"
end
if g_prt_gdb_marker_state.tensor_id != 2
  printf "WARNING: marker tensor mismatch\n"
end
if g_prt_gdb_marker_state.page_idx != 47
  printf "WARNING: marker page mismatch\n"
end

printf "\n--- immediate submitwait entry ---\n"
tbreak dma_submit_wait_annotated_scoped
continue
bt 10
info args
print stage_idx
print tensor_id
print debug_page_idx
print/x req
print *req
x/12i $pc

printf "\n--- prt_dma_submit entry ---\n"
tbreak prt_dma_submit
continue
bt 10
info args
print/x req
print *req
print/x tok
x/12i $pc

printf "\n--- dma_blocking_wait entry ---\n"
tbreak dma_blocking_wait
continue
bt 10
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
x/12i $pc

printf "\n--- before hw_dma_fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3683
continue
bt 10
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
x/12i $pc

printf "\n--- after hw_dma_fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3687
continue
bt 10
info args
print $tokp->id
print $tokp->debug_page_idx
print $tokp->hw_done_flag
print/x $tokp->completion_flag
x/wx $tokp->completion_flag
x/12i $pc

printf "\n--- before shared fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3762
continue
bt 10
info args
print $tokp->id
print $tokp->debug_page_idx
print $tokp->rr_manager_id
print $tokp->rr_scope_valid
print $tokp->rr_scope_external
x/12i $pc

printf "\n--- wait return marker ---\n"
tbreak dma_gdb_marker_wait_return
continue
bt 10
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
x/12i $pc
