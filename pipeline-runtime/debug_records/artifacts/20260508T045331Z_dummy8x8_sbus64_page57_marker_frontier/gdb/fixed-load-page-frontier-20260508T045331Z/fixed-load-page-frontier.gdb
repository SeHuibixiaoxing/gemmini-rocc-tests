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
if g_prt_gdb_marker_state.tensor_id != 0
  printf "WARNING: marker tensor mismatch\n"
end
if g_prt_gdb_marker_state.page_idx != 57
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
x/10i $pc

printf "\n--- before hw_dma_fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3683
continue
bt 8
info args
print tok->id
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
x/10i $pc

printf "\n--- after hw_dma_fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3687
continue
bt 8
info args
print fence_status
print tok->id
print tok->debug_page_idx
print tok->hw_done_flag
print/x tok->completion_flag
x/wx tok->completion_flag
x/10i $pc

printf "\n--- before shared fence ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3762
continue
bt 8
info args
print tok->id
print tok->debug_page_idx
print tok->rr_manager_id
print tok->rr_scope_valid
print tok->rr_scope_external
x/10i $pc

printf "\n--- wait return marker ---\n"
tbreak dma_gdb_marker_wait_return
continue
bt 8
info args
print rc
print tok->id
print tok->debug_page_idx
print tok->rr_manager_id
print tok->rr_scope_valid
print tok->rr_scope_external
print tok->hw_done_flag
print tok->done
print tok->status
x/10i $pc
