set breakpoint pending on
set print pretty on
set print elements 64
set print repeats 16

break conv_call_for_manager_sync_strided
commands
silent
printf "\n--- BP conv_call_for_manager_sync_strided ---\n"
info args
bt 6
continue
end

break prt_run_pointwise_matmul_fallback_strided_impl
commands
silent
printf "\n--- BP prt_run_pointwise_matmul_fallback_strided_impl ---\n"
info args
info locals
bt 8
continue
end

break tiled_matmul_nn_stride_auto
commands
silent
printf "\n--- BP tiled_matmul_nn_stride_auto ---\n"
info args
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2451
commands
silent
printf "\n--- BP pointwise subcall return line 2451 ---\n"
info args
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2534
commands
silent
printf "\n--- BP pointwise rr_fence begin line 2534 ---\n"
info args
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2545
commands
silent
printf "\n--- BP pointwise gemmini_fence line 2545 ---\n"
info args
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2557
commands
silent
printf "\n--- BP pointwise drain line 2557 ---\n"
info args
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2581
commands
silent
printf "\n--- BP pointwise release line 2581 ---\n"
info args
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c:2593
commands
silent
printf "\n--- BP conv_call_for_manager_sync_strided return line 2593 ---\n"
info args
info locals
bt 8
continue
end

break prt_rr_acquire_scope if stage_id == 0 && opcode_id == 2
commands
silent
printf "\n--- BP prt_rr_acquire_scope opcode2 ---\n"
info args
bt 8
continue
end

break prt_rr_fence_scope
commands
silent
printf "\n--- BP prt_rr_fence_scope ---\n"
info args
print scope ? *scope : *scope
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4515
commands
silent
printf "\n--- BP worker after prt_gemm_conv_run line 4515 ---\n"
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4560
commands
silent
printf "\n--- BP worker export-sync enter line 4560 ---\n"
info locals
bt 8
continue
end

break sync_stage_export_aliases
commands
silent
printf "\n--- BP sync_stage_export_aliases ---\n"
info args
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4571
commands
silent
printf "\n--- BP worker export-sync return line 4571 ---\n"
info locals
bt 8
continue
end

break /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4582
commands
silent
printf "\n--- BP worker compute-done line 4582 ---\n"
info locals
bt 8
continue
end

break prt_dma_submit
commands
silent
printf "\n--- BP prt_dma_submit ---\n"
info args
print tok ? *tok : *tok
bt 8
continue
end

break dma_blocking_wait
commands
silent
printf "\n--- BP dma_blocking_wait ---\n"
info args
print tok ? *tok : *tok
bt 8
continue
end

break dma_gdb_marker_wait_return
commands
silent
printf "\n--- BP dma_gdb_marker_wait_return ---\n"
info args
print tok ? *tok : *tok
bt 8
continue
end

info breakpoints
