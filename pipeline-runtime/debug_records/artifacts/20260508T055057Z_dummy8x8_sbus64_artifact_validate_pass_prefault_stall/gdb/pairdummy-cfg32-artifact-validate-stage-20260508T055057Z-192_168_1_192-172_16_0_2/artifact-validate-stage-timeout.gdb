printf "\n--- artifact validation timeout diagnostic ---\n"
printf "expected seg=14 stage=0\n"
bt full
printf "\n--- all thread bt full ---\n"
thread apply all bt full
printf "\n--- registers ---\n"
info registers
printf "\n--- pc window ---\n"
x/24i $pc-48
printf "\n--- stage/db convenience pointers ---\n"
print $stagep
print $dbp
print g_prt_gdb_marker_state
detach
