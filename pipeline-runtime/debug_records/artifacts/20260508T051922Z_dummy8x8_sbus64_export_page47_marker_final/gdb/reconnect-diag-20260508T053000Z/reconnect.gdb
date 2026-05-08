set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set remotetimeout 60
set tcp connect-timeout 30
target remote :32346
info threads
thread apply all bt
info registers pc sp ra a0 a1 a2 a3
x/16i $pc-32
detach
quit
