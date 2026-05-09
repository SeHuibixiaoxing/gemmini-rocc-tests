printf "\n--- sbus64 no-DMA segment1/stage0 SPM xlate release probe ---\n"
printf "Goal: after worker-before-build-stage-task at segment=1/stage=0/subbatch=3, split cfg31 release write from post-release readback.\n"

printf "\n--- deleting initial marker breakpoint and locking selected worker thread ---\n"
delete 1
set scheduler-locking on
set breakpoint pending on

printf "\n--- target release scope breakpoint: manager 6, cfg31, opcode3 ---\n"
break prt_rr_release_scope if scope && scope->valid && scope->cfg_id == 31 && scope->manager_id == 6 && scope->opcode_id == 3
continue

printf "\n--- hit target prt_rr_release_scope entry ---\n"
info args
print *scope
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm after rr_release(cfg31) write breakpoint ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:396 if scope && scope->cfg_id == 31 && scope->manager_id == 6
continue

printf "\n--- rr_release(cfg31) write returned ---\n"
info args
print *scope
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm before release readback breakpoint ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:402 if scope && scope->cfg_id == 31 && scope->manager_id == 6
continue

printf "\n--- about to read CSR_RRCFG31 after release ---\n"
info args
print *scope
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm after release readback breakpoint ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:403 if scope && scope->cfg_id == 31 && scope->manager_id == 6
continue

printf "\n--- release readback returned; cfg31 release path is not the stuck instruction in this run ---\n"
info args
print *scope
bt 10
info registers pc sp ra
x/12i $pc-24

detach
quit
