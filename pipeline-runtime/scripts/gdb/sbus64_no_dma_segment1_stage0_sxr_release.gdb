printf "\n--- sbus64 no-DMA segment1/stage0 SPM xlate release probe ---\n"
printf "Goal: after worker-before-build-stage-task at segment=1/stage=0/subbatch=3, split cfg31 release write, post-release readback, opcode restore, and flush return.\n"

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
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:403
continue

printf "\n--- release readback returned; cfg31 release CSR readback is not the stuck instruction in this run ---\n"
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm release-end log boundary after prt_rr_release_scope returns ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:181
continue

printf "\n--- prt_rr_release_scope returned to prt_spm_xlate_release_scope ---\n"
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm opcode restore write boundary ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:194
continue

printf "\n--- about to restore opcode3 binding after SPM xlate release ---\n"
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm after opcode restore boundary ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:195
continue

printf "\n--- opcode restore returned ---\n"
bt 10
info registers pc sp ra
x/12i $pc-24

printf "\n--- arm return from prt_gemmini_spm_xlate_flush ---\n"
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_rerocc.c:531
continue

printf "\n--- prt_gemmini_spm_xlate_flush returned from release/restore path ---\n"
bt 10
info registers pc sp ra
x/12i $pc-24

detach
quit
