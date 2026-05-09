printf "\n--- sbus64 no-DMA segment1/stage0 post-compute thin frontier ---\n"
printf "Goal: after worker-export-sync at segment=1/global_stage=1/local_stage=0/subbatch=3, verify sync_stage_export_aliases returns under no-DMA.\n"

set breakpoint pending on
set print pretty on
set print elements 32
set print repeats 8

printf "\n--- current marker state ---\n"
print g_prt_gdb_marker_state
print g_prt_debug_state

printf "\n--- arming export-sync return stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4601

printf "\n--- arming export-sync entry milestone ---\n"
break sync_stage_export_aliases
commands
silent
printf "\n--- thin BP sync_stage_export_aliases entry ---\n"
info args
print g_prt_debug_state
continue
end

printf "\n--- thin post-compute frontier breakpoints armed ---\n"
info breakpoints
