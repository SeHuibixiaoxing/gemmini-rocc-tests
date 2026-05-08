printf "\n--- artifact validation stage hit ---\n"
bt 12
info args
info locals
print path
print db->count
print seg_idx
print stage_idx
print *stage
print stage->stage_id
print stage->layer_id
print stage->acc_util
print stage->tensor_id_count
print stage->dram_bypass_count
print stage->spm_bypass_count
print stage->local_spm_tensor_count
print stage->local_spm_page_span
set $stagep = stage
set $dbp = db
x/16i $pc-32
printf "\n--- artifact validation stage finish begins ---\n"
