printf "\n--- synthetic prefault marker hit ---\n"
print g_prt_gdb_marker_state
bt 16
printf "\n--- marker caller frame ---\n"
frame 1
info args
info locals
print kind
print path
print buf
print blob_size
print host_page_bytes
print step
print total_pages
print touched_pages
print next_progress_bytes
x/16i $pc-32
frame 0
