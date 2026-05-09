set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 120
set tcp connect-timeout 60
target remote :32345
break prt_gdb_marker_stop
set $prt_initial_continue_timeout = 0
continue
if $prt_initial_continue_timeout
  printf "\n--- initial continue timeout before marker ---\n"
  x/i $pc
  printf "\n--- current bt at initial-timeout stop ---\n"
  bt
  printf "\n--- all thread bt at initial-timeout stop ---\n"
  thread apply all bt
  printf "\n--- debug states at initial-timeout stop ---\n"
  print g_prt_debug_state
  print g_prt_debug_tls_state
  print g_prt_gdb_marker_state
  printf "\n--- registers at initial-timeout stop ---\n"
  info registers
  printf "\n--- pc window at initial-timeout stop ---\n"
  x/16i $pc-32
  detach
  quit 20
end
printf "\n--- marker state ---\n"
print g_prt_gdb_marker_state
printf "\n--- current bt ---\n"
bt
printf "\n--- all thread bt ---\n"
thread apply all bt
printf "\n--- registers ---\n"
info registers
printf "\n--- pc window ---\n"
x/16i $pc-32
delete 1
printf "\n--- post-marker gdb command file ---\n"
source /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/gdb/sbus64_no_dma_segment1_stage0_sxr_release.gdb
