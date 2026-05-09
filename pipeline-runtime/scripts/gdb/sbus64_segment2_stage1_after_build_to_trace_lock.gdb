printf "\n--- sbus64 segment2 stage1 after-build to trace-lock probe ---\n"
printf "Goal: after worker-after-build-stage-task, test whether control reaches prt_trace_on_gemm_issue()'s state_lock acquisition before worker-gemm-run.\n"

printf "\n--- deleting marker breakpoint before follow ---\n"
delete 1

printf "\n--- setting trace-lock address breakpoint ---\n"
tbreak *0x3054e

python
import gdb
import threading
import time

def _prt_interrupt_after_delay():
    time.sleep(120)
    try:
        gdb.post_event(lambda: gdb.execute("interrupt"))
    except Exception as exc:
        try:
            gdb.write("[prt-gdb] interrupt timer failed: %s\n" % exc)
        except Exception:
            pass

threading.Thread(target=_prt_interrupt_after_delay, daemon=True).start()
end

printf "\n--- continuing from after-build marker toward trace-lock ---\n"
continue

printf "\n--- after-build follow stop ---\n"
x/i $pc
bt

printf "\n--- all thread bt at follow stop ---\n"
thread apply all bt

printf "\n--- debug states at follow stop ---\n"
print g_prt_debug_state
print g_prt_debug_tls_state
print g_prt_gdb_marker_state

printf "\n--- registers at follow stop ---\n"
info registers
