printf "\n--- segment2 stage1 before-exports-ready follow: thread-local call-site trace ---\n"
set pagination off
set confirm off
set print pretty on

python
import gdb

def prt_safe(cmd):
    try:
        out = gdb.execute(cmd, to_string=True)
        if out:
            print(out.rstrip())
    except Exception as exc:
        print("%s => <gdb-error: %s>" % (cmd, exc))

def prt_safe_many(cmds):
    for cmd in cmds:
        prt_safe(cmd)
end

set $prt_marker_thread = $_thread
printf "\n--- marker thread ---\n"
printf "gdb_thread=%d\n", $prt_marker_thread

printf "\n--- return from marker helper stack to stage_worker_main ---\n"
finish
finish
finish
bt 8
python
prt_safe_many([
    "info locals",
    "print rc",
    "print ctx->stage_id",
    "print progress_sbatch",
    "print export_count",
    "print shared_pair_count",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4360
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- returned stage_wait_exports_ready stage1 subbatch0 ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print rc",
    "print ctx->stage_id",
    "print progress_sbatch",
    "print export_count",
    "print shared_pair_count",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4423
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- returned build_stage_task_desc stage1 ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print rc",
    "print task.stage_id",
    "print task.acc_id",
    "print task.op_kind",
    "print task.split_kind",
    "print task.tile_count",
    "print task.num_managers",
    "print task.manager_ids[0]",
    "print task.manager_ids[1]",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4508
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- before prt_gemm_conv_run stage1 ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print task.stage_id",
    "print task.acc_id",
    "print task.op_kind",
    "print task.split_kind",
    "print task.tile_count",
    "print task.num_managers",
    "print task.manager_ids[0]",
    "print task.manager_ids[1]",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4515
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- returned prt_gemm_conv_run stage1 ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print rc",
    "print task.stage_id",
    "print task.op_kind",
    "print task.tile_count",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4564
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- before sync_stage_export_aliases segment2 stage1 subbatch0 ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print rc",
    "print ctx->stage_id",
    "print progress_sbatch",
    "print export_count",
    "print shared_pair_count",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4571
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- returned sync_stage_export_aliases segment2 stage1 subbatch0 ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print rc",
])
end

break prt_process_c4
condition $bpnum $_thread == $prt_marker_thread && pair && pair->pre_export && pair->pre_export->segment_idx == 2 && pair->pre_export->stage_idx == 1 && pair->pre_export->tensor_id == 6
continue
printf "\n--- hit prt_process_c4 segment2 stage1 tensor6 export ---\n"
bt 10
python
prt_safe_many([
    "info args",
    "set $pre = pair->pre_export",
    "set $nxt = pair->nxt_entry",
    "set $tagp = pair->tag",
    "print pair->buffer_idx",
    "print *pair->tag",
    "print $pre->buffer_id",
    "print $pre->tensor_id",
    "print $pre->stage_idx",
    "print $pre->segment_idx",
    "print $pre->full[0]",
    "print $pre->full[1]",
    "print $pre->in_use_idx",
    "print $pre->subbatch_offset",
    "print $pre->state_epoch",
    "print $nxt->buffer_id",
    "print $nxt->tensor_id",
    "print $nxt->stage_idx",
    "print $nxt->segment_idx",
    "print $nxt->full[0]",
    "print $nxt->full[1]",
    "print $nxt->in_use_idx",
    "print $nxt->subbatch_offset",
    "print $nxt->state_epoch",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:666
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- prt_process_c4 before state transition ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print idx",
    "print nxt_count",
    "print any_full",
    "print all_empty",
    "print *$tagp",
    "print $pre->full[0]",
    "print $pre->full[1]",
    "print $nxt->full[0]",
    "print $nxt->full[1]",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_scheduler.c:681
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- prt_process_c4 after state transition block ---\n"
bt 8
python
prt_safe_many([
    "info locals",
    "print idx",
    "print nxt_count",
    "print any_full",
    "print all_empty",
    "print *$tagp",
    "print $pre->full[0]",
    "print $pre->full[1]",
    "print $nxt->full[0]",
    "print $nxt->full[1]",
    "print $pre->subbatch_offset",
    "print $nxt->subbatch_offset",
    "print $pre->state_epoch",
    "print $nxt->state_epoch",
])
end

finish
printf "\n--- returned prt_process_c4 segment2 stage1 tensor6 export ---\n"
python
prt_safe_many([
    "print/d $a0",
    "print $pre->full[0]",
    "print $pre->full[1]",
    "print $nxt->full[0]",
    "print $nxt->full[1]",
    "print *$tagp",
])
end

printf "\n--- all thread bt after c4 return ---\n"
thread apply all bt
