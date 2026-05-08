printf "\n--- artifact validation stage returned ---\n"
bt 12
info args
info locals
print $_return
print $stagep->stage_id
print $stagep->layer_id
print $stagep->local_spm_tensor_count
print $stagep->local_spm_page_span
x/16i $pc-32
detach
