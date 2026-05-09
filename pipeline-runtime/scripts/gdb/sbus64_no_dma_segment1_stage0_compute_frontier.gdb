printf "\n--- sbus64 no-DMA segment1/stage0 compute frontier ---\n"
printf "Goal: after worker-gemm-run at segment=1/global_stage=1/local_stage=0/subbatch=3, split the compute path through Gemmini issue/fence/drain/release.\n"

set breakpoint pending on
set print pretty on
set print elements 64
set print repeats 16
set scheduler-locking on

printf "\n--- current marker state ---\n"
print g_prt_gdb_marker_state

printf "\n--- arming compute return stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4545

printf "\n--- arming task dispatch milestones ---\n"
break prt_gemm_conv_run
commands
silent
printf "\n--- BP prt_gemm_conv_run ---\n"
info args
if task != 0
  print *task
end
bt 8
continue
end

break gemm_async_conv_run
commands
silent
printf "\n--- BP gemm_async_conv_run ---\n"
info args
if task != 0
  print *task
end
bt 8
continue
end

break gemm_issue_task
commands
silent
printf "\n--- BP gemm_issue_task ---\n"
info args
if task != 0
  print *task
end
bt 8
continue
end

break gemm_issue_conv_task
commands
silent
printf "\n--- BP gemm_issue_conv_task ---\n"
info args
if task != 0
  print *task
end
if conv != 0
  print *conv
end
bt 8
continue
end

break run_conv_oc_split
commands
silent
printf "\n--- BP run_conv_oc_split ---\n"
info args
if task != 0
  print *task
end
if conv != 0
  print *conv
end
bt 8
continue
end

printf "\n--- arming manager compute milestones ---\n"
break conv_call_for_manager_sync_strided
commands
silent
printf "\n--- BP conv_call_for_manager_sync_strided ---\n"
info args
if conv != 0
  print *conv
end
bt 10
continue
end

break conv_call_for_manager_sync
commands
silent
printf "\n--- BP conv_call_for_manager_sync ---\n"
info args
if conv != 0
  print *conv
end
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2451
commands
silent
printf "\n--- BP pointwise scoped subcall returned ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2534
commands
silent
printf "\n--- BP conv-sync rr_fence_scope begin ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2545
commands
silent
printf "\n--- BP conv-sync gemmini_fence begin; rr_fence returned ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2557
commands
silent
printf "\n--- BP conv-sync drain begin; gemmini_fence returned ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2581
commands
silent
printf "\n--- BP conv-sync release begin; drain returned ---\n"
info args
info locals
bt 10
continue
end

printf "\n--- arming pointwise inner issue milestones ---\n"
break prt_run_pointwise_matmul_fallback_strided_impl
commands
silent
printf "\n--- BP pointwise strided impl entry ---\n"
info args
if conv != 0
  print *conv
end
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:1029
commands
silent
printf "\n--- BP tiled_matmul_nn_stride_auto call begin ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:1036
commands
silent
printf "\n--- BP tiled_matmul_nn_stride_auto returned ---\n"
info args
info locals
bt 10
continue
end

printf "\n--- arming ReRoCC acquire/fence/release split points ---\n"
break prt_rr_acquire_scope
commands
silent
printf "\n--- BP prt_rr_acquire_scope entry ---\n"
info args
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:256
commands
silent
printf "\n--- BP rr-acquire before CSR read; acquire CSR write returned ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:308
commands
silent
printf "\n--- BP rr-acquire before rr_set_opc; acquire read returned acquired ---\n"
info args
info locals
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:323
commands
silent
printf "\n--- BP rr-acquire scope valid; rr_set_opc returned ---\n"
info args
info locals
bt 10
continue
end

break prt_rr_fence_scope
commands
silent
printf "\n--- BP prt_rr_fence_scope entry ---\n"
info args
if scope != 0
  print *scope
end
bt 10
continue
end

break prt_rr_release_scope
commands
silent
printf "\n--- BP prt_rr_release_scope entry ---\n"
info args
if scope != 0
  print *scope
end
bt 10
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:402
commands
silent
printf "\n--- BP rr_release readback begin; release CSR write returned ---\n"
info args
if scope != 0
  print *scope
end
bt 10
continue
end

break flush_scope_after_drain
commands
silent
printf "\n--- BP flush_scope_after_drain entry ---\n"
info args
if scope != 0
  print *scope
end
bt 10
continue
end

printf "\n--- compute frontier breakpoints armed ---\n"
info breakpoints
