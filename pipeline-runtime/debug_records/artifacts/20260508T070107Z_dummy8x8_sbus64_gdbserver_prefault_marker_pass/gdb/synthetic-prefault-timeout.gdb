printf "\n--- synthetic prefault timeout diagnostic ---\n"
printf "min_pages=3328\n"
bt full
printf "\n--- all thread bt full ---\n"
thread apply all bt full
printf "\n--- current frame args/locals ---\n"
info args
info locals
printf "\n--- marker state ---\n"
print g_prt_gdb_marker_state
printf "\n--- debug state ---\n"
print g_prt_debug_state
print g_prt_debug_tls_state
printf "\n--- registers ---\n"
info registers
printf "\n--- pc window ---\n"
x/32i $pc-64
detach
