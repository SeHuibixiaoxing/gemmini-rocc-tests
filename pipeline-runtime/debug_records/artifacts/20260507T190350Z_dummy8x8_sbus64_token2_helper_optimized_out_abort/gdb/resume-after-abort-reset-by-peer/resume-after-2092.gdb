set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 60
set tcp connect-timeout 30
target remote :32346
printf "\n--- reconnect state ---\n"
bt
info threads
info registers pc sp ra
printf "\n--- resume frontier: before token cleanup breadcrumb ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:2124
continue
bt
info threads
info registers pc sp ra
info args
info locals
printf "\n--- resume frontier: back in fixed-load page loop after submitwait ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:814
continue
bt
info threads
info registers pc sp ra
info args
info locals
printf "\n--- resume frontier: page0 accounting complete ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:840
continue
bt
info threads
info registers pc sp ra
info args
info locals
printf "\n--- resume frontier: next page submitwait call ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c:813
continue
bt
info threads
info registers pc sp ra
info args
info locals
printf "\n--- resume frontier: token2 wait-enter stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 12
set variable g_prt_gdb_marker_filter.segment_idx = 0
set variable g_prt_gdb_marker_filter.global_stage_id = 4294967295
set variable g_prt_gdb_marker_filter.local_stage_id = 0
set variable g_prt_gdb_marker_filter.subbatch_id = 4294967295
set variable g_prt_gdb_marker_filter.manager_id = 4294967295
set variable g_prt_gdb_marker_filter.tensor_id = 0
set variable g_prt_gdb_marker_filter.page_idx = 4294967295
set variable g_prt_gdb_marker_filter.token_id = 2
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra
printf "\n--- resume frontier: token2 wait-return stage0 tensor0 ---\n"
set variable g_prt_gdb_marker_filter.site_id = 13
set variable g_prt_gdb_marker_filter.token_id = 2
continue
print g_prt_gdb_marker_state
bt
info threads
info registers pc sp ra
detach
quit
