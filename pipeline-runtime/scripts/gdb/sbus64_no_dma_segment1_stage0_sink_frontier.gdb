printf "\n--- sbus64 no-DMA segment1/stage0 sink frontier ---\n"
printf "Goal: after worker-export-sync at segment=1/global_stage=1/local_stage=0/subbatch=3, check whether main thread reaches sink completion, joins workers, and enters segment2.\n"

set breakpoint pending on
set print pretty on
set print elements 32
set print repeats 8

printf "\n--- current marker state ---\n"
print g_prt_gdb_marker_state
print g_prt_debug_state

printf "\n--- arming segment1 worker exit milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4718
commands
silent
printf "\n--- sink BP worker exit reached ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming segment1 sink reached milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:5429
commands
silent
printf "\n--- sink BP main sink target reached ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming segment1 join begin milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:5451
commands
silent
printf "\n--- sink BP main join begin ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming segment1 complete milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:5463
commands
silent
printf "\n--- sink BP segment threaded backend complete ---\n"
print g_prt_debug_state
bt 6
continue
end

printf "\n--- arming next segment begin final stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:5223

printf "\n--- thin sink frontier breakpoints armed ---\n"
info breakpoints
