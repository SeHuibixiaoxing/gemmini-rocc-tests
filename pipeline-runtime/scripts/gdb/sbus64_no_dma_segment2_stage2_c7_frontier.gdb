printf "\n--- sbus64 no-DMA segment2/stage2 C7 ring frontier ---\n"
printf "Goal: after worker-entry at segment=2/global_stage=4/local_stage=2/subbatch=any, check the tensor4 ALL_RINGBUFFER producer/consumer handoff.\n"

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

printf "\n--- arming tensor4 C8 producer observer ---\n"
break prt_process_c8
commands
silent
if buf && buf->segment_idx == 2 && buf->stage_idx == 0 && buf->tensor_id == 4
  printf "\n--- no-DMA s2/stage0 tensor4 C8 producer ---\n"
  print *buf
  if buf->ring
    print *buf->ring
  end
end
continue
end

printf "\n--- arming stage2 C7 wait-enter milestone ---\n"
tbreak prt_ring_wait_ready
condition $bpnum $_thread == $prt_marker_thread
commands
silent
printf "\n--- no-DMA s2/stage2 tensor4 C7 wait-enter ---\n"
print offset
print *rb
bt 6
continue
end

printf "\n--- arming stage2 C7 process-return milestone ---\n"
tbreak prt_process_c7
condition $bpnum $_thread == $prt_marker_thread
commands
silent
printf "\n--- no-DMA s2/stage2 C7 process enter ---\n"
print *buf
print *buf->ring
bt 6
continue
end

printf "\n--- arming stage2 GEMM run final stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4566
condition $bpnum $_thread == $prt_marker_thread

printf "\n--- thin segment2/stage2 C7 frontier breakpoints armed ---\n"
info breakpoints
