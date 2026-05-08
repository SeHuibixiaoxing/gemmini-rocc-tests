printf "\n--- segment2 stage1 follow: set breakpoints ---\n"
set pagination off
set confirm off
set print pretty on

break build_stage_task_desc if stage_id == 1
continue
printf "\n--- hit build_stage_task_desc stage1 ---\n"
bt 10
info args
print stage_id
print task->stage_id
print task->acc_id
print task->tile_count
print task->num_managers
print task->manager_ids[0]
print task->manager_ids[1]
finish
printf "\n--- returned build_stage_task_desc stage1 ---\n"
print/d $a0
clear build_stage_task_desc

break prt_gemm_conv_run if task && task->stage_id == 1
continue
printf "\n--- hit prt_gemm_conv_run stage1 ---\n"
bt 10
info args
print task->stage_id
print task->op_kind
print task->split_kind
print task->tile_count
print task->num_managers
print task->manager_ids[0]
print task->manager_ids[1]
finish
printf "\n--- returned prt_gemm_conv_run stage1 ---\n"
print/d $a0
clear prt_gemm_conv_run

break sync_stage_export_aliases if segment_idx == 2 && stage_id == 1 && global_stage_id == 3 && subbatch_id == 0
continue
printf "\n--- hit sync_stage_export_aliases segment2 stage1 subbatch0 ---\n"
bt 10
info args
print segment_idx
print stage_id
print global_stage_id
print subbatch_id
finish
printf "\n--- returned sync_stage_export_aliases segment2 stage1 subbatch0 ---\n"
print/d $a0
clear sync_stage_export_aliases

break prt_process_c4 if pair && pair->pre_export && pair->pre_export->segment_idx == 2 && pair->pre_export->stage_idx == 1 && pair->pre_export->tensor_id == 6
continue
printf "\n--- hit prt_process_c4 segment2 stage1 tensor6 export ---\n"
bt 10
info args
set $pre = pair->pre_export
set $nxt = pair->nxt_entry
set $tagp = pair->tag
print pair->buffer_idx
print *pair->tag
print $pre->buffer_id
print $pre->tensor_id
print $pre->stage_idx
print $pre->segment_idx
print $pre->full[0]
print $pre->full[1]
print $pre->in_use_idx
print $nxt->buffer_id
print $nxt->tensor_id
print $nxt->stage_idx
print $nxt->segment_idx
print $nxt->full[0]
print $nxt->full[1]
print $nxt->in_use_idx
finish
printf "\n--- returned prt_process_c4 segment2 stage1 tensor6 export ---\n"
print/d $a0
print $pre->full[0]
print $pre->full[1]
print $nxt->full[0]
print $nxt->full[1]
print *$tagp

printf "\n--- all thread bt after c4 return ---\n"
thread apply all bt
