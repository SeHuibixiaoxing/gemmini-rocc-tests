set pagination off
set confirm off
set print pretty on
set print thread-events off
set debuginfod enabled off
set remotetimeout 120
set tcp connect-timeout 60
target remote :32345
break prt_gdb_marker_stop
continue
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
printf "\n--- post-marker gdb command file ---\n"
source /home/ubuntu/chipyard/tmp/pairdummy-segment0-worker-dma-chain.E2J3CM.gdb
detach
quit
