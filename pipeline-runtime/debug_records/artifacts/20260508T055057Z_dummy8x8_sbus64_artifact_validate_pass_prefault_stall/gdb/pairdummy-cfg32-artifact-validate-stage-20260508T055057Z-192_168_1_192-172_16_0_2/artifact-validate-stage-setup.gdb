set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 120
set tcp connect-timeout 60
target remote :32345
break validate_stage_against_mapping_entries if seg_idx == 14 && stage_idx == 0
continue
