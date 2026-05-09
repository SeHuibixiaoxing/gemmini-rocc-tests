printf "\n--- sbus64 no-DMA segment2/stage1 GEMM frontier ---\n"
printf "Goal: after worker-gemm-run at segment=2/global_stage=3/local_stage=1/subbatch=0, follow that worker to post-GEMM, export-sync, and worker-done.\n"

set breakpoint pending on
set print pretty on
set print elements 32
set print repeats 8

printf "\n--- current marker state ---\n"
print g_prt_gdb_marker_state
print g_prt_debug_state

set $prt_marker_thread = $_thread
printf "\n--- marker thread ---\n"
printf "gdb_thread=%d\n", $prt_marker_thread

printf "\n--- arming stage1 GEMM return milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4545
condition $bpnum $_thread == $prt_marker_thread
commands
silent
printf "\n--- no-DMA s2/stage1 GEMM returned ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming stage1 export-sync enter milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4590
condition $bpnum $_thread == $prt_marker_thread
commands
silent
printf "\n--- no-DMA s2/stage1 export-sync enter ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming stage1 export-sync returned milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4601
condition $bpnum $_thread == $prt_marker_thread
commands
silent
printf "\n--- no-DMA s2/stage1 export-sync returned ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming stage1 worker done final stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4712
condition $bpnum $_thread == $prt_marker_thread

printf "\n--- thin segment2/stage1 GEMM frontier breakpoints armed ---\n"
info breakpoints
