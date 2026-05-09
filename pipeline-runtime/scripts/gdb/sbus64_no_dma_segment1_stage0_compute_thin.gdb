printf "\n--- sbus64 no-DMA segment1/stage0 compute thin frontier ---\n"
printf "Goal: after worker-gemm-run at segment=1/global_stage=1/local_stage=0/subbatch=3, follow manager 7 with minimal GDB overhead.\n"

set breakpoint pending on
set print pretty on
set print elements 32
set print repeats 8

printf "\n--- current marker state ---\n"
print g_prt_gdb_marker_state
print g_prt_debug_state

printf "\n--- arming final compute return stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4545

printf "\n--- arming manager 7 issue milestones ---\n"
break conv_call_for_manager_sync_strided
ignore $bpnum 7
commands
silent
printf "\n--- thin BP manager7 conv_call_for_manager_sync_strided entry ---\n"
info args
print g_prt_debug_state
continue
end

break prt_run_pointwise_matmul_fallback_strided_impl
ignore $bpnum 7
commands
silent
printf "\n--- thin BP manager7 pointwise strided impl entry ---\n"
info args
print g_prt_debug_state
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:1029
ignore $bpnum 7
commands
silent
printf "\n--- thin BP manager7 tiled_matmul_nn_stride_auto call begin ---\n"
info args
print g_prt_debug_state
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:1036
ignore $bpnum 7
commands
silent
printf "\n--- thin BP manager7 tiled_matmul_nn_stride_auto returned ---\n"
info args
print g_prt_debug_state
continue
end

printf "\n--- arming manager 7 post-issue milestones ---\n"
break prt_rr_fence_scope if scope != 0 && scope->manager_id == 7
commands
silent
printf "\n--- thin BP manager7 prt_rr_fence_scope entry ---\n"
printf "scope valid=%u cfg=%u opcode=%u manager=%u\n", scope->valid, scope->cfg_id, scope->opcode_id, scope->manager_id
print g_prt_debug_state
continue
end

break flush_scope_after_drain if scope != 0 && scope->manager_id == 7
commands
silent
printf "\n--- thin BP manager7 flush_scope_after_drain entry ---\n"
printf "scope valid=%u cfg=%u opcode=%u manager=%u\n", scope->valid, scope->cfg_id, scope->opcode_id, scope->manager_id
print g_prt_debug_state
continue
end

break prt_rr_release_scope if scope != 0 && scope->manager_id == 7
commands
silent
printf "\n--- thin BP manager7 prt_rr_release_scope entry ---\n"
printf "scope valid=%u cfg=%u opcode=%u manager=%u\n", scope->valid, scope->cfg_id, scope->opcode_id, scope->manager_id
print g_prt_debug_state
continue
end

printf "\n--- thin compute frontier breakpoints armed ---\n"
info breakpoints
