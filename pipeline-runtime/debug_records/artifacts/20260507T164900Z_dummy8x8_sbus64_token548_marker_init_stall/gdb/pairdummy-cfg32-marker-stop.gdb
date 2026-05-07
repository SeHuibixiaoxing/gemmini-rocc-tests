set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 120
set tcp connect-timeout 60
target remote :32345
break prt_gdb_marker_stop
continue
printf "\n--- marker state ---\n"
print g_prt_gdb_marker_state
printf "\n--- current bt ---\n"
bt
printf "\n--- all thread bt ---\n"
thread apply all bt
printf "\n--- registers ---\n"
info registers
printf "\n--- pc window ---\n"
x/16i $pc-32
delete 1
printf "\n--- post-marker inline gdb commands ---\n"
printf "
--- post-marker: token548 returned; set next token entry ---
"
break dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 549
continue
printf "
--- next stage0 tensor2 dma wait entry ---
"
print tok->id
print tok->stage_idx
print tok->tensor_id
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
bt
thread apply all bt
set $nexttok = tok->id
printf "
--- trace same token return ---
"
delete 2
break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:3814 if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id == $nexttok
continue
printf "
--- same token dma wait return ---
"
print $nexttok
print tok->id
print tok->stage_idx
print tok->tensor_id
print tok->rr_manager_id
print tok->rr_scope_valid
print tok->rr_scope_external
print tok->hw_done_flag
print tok->done
print tok->status
print/x tok->completion_flag
x/wx tok->completion_flag
print/x tok->debug_src_addr
print/x tok->debug_dst_addr
print/x tok->debug_done_flag_pa
print tok->debug_bytes
bt
thread apply all bt
info registers pc sp ra a0 a1 a2 a3
x/16i $pc
detach
quit
