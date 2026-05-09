printf "\n--- sbus64 no-DMA segment1/stage0 post-pipebuf thin frontier ---\n"
printf "Goal: after worker-export-sync at segment=1/global_stage=1/local_stage=0/subbatch=3, walk post-compute pipebuf release/publish.\n"

set breakpoint pending on
set print pretty on
set print elements 32
set print repeats 8

printf "\n--- current marker state ---\n"
print g_prt_gdb_marker_state
print g_prt_debug_state

printf "\n--- arming post-sync return milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4609 if ctx != 0 && ctx->stage_id == 0 && progress_sbatch == 3
commands
silent
printf "\n--- thin BP post-sync returned; about to wrk-postcmp ---\n"
printf "stage=%u subbatch=%u rc=%d entry_count=%u export_count=%u\n", ctx->stage_id, progress_sbatch, rc, entry_count, export_count
print g_prt_debug_state
continue
end

printf "\n--- arming p1 entry-release milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4622 if ctx != 0 && ctx->stage_id == 0 && progress_sbatch == 3
commands
silent
printf "\n--- thin BP p1 entry-release begin ---\n"
printf "stage=%u subbatch=%u entry_i=%u kind=%u idx=%u tensor=%u full0=%d full1=%d sbatch_off=%u\n", ctx->stage_id, progress_sbatch, i, (unsigned)b->kind, idx, b->tensor_id, b->full[0], b->full[1], b->subbatch_offset
continue
end

printf "\n--- arming p2 export-publish milestone ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4650 if ctx != 0 && ctx->stage_id == 0 && progress_sbatch == 3
commands
silent
printf "\n--- thin BP p2 export-publish begin ---\n"
printf "stage=%u subbatch=%u export_i=%u kind=%u idx=%u tensor=%u full0=%d full1=%d sbatch_off=%u fanout=%u/%u ring=%p\n", ctx->stage_id, progress_sbatch, i, (unsigned)b->kind, idx, b->tensor_id, b->full[0], b->full[1], b->subbatch_offset, b->fanout_pending, b->fanout_total, b->ring
continue
end

printf "\n--- arming final wrk-done stop ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4712 if ctx != 0 && ctx->stage_id == 0 && progress_sbatch == 3

printf "\n--- thin post-pipebuf frontier breakpoints armed ---\n"
info breakpoints
