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
