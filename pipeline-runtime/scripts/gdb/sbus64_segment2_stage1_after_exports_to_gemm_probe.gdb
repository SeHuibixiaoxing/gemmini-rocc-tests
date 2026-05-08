printf "\n--- segment2 stage1 after-exports to gemm probe ---\n"
set pagination off
set confirm off
set print pretty on

python
import gdb

def prt_safe(cmd):
    try:
        out = gdb.execute(cmd, to_string=True)
        if out:
            print(out.rstrip())
    except Exception as exc:
        print("%s => <gdb-error: %s>" % (cmd, exc))

def prt_safe_many(cmds):
    for cmd in cmds:
        prt_safe(cmd)

def prt_where():
    frame = gdb.selected_frame()
    sal = frame.find_sal()
    print("selected_frame=%s %s:%s" % (
        frame.name(),
        sal.symtab.fullname() if sal.symtab else "<no-symtab>",
        sal.line))
end

set $prt_marker_thread = $_thread
printf "\n--- marker thread ---\n"
printf "gdb_thread=%d\n", $prt_marker_thread
bt 8

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4409
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- before build_stage_task_desc marker/call ---\n"
bt 10
python
prt_where()
prt_safe_many([
    "info locals",
    "print rc",
    "print ctx->stage_id",
    "print progress_sbatch",
    "print entry_count",
    "print export_count",
    "print shared_pair_count",
    "print entry_bufs[0]",
    "print *entry_bufs[0]",
    "print export_bufs[0]",
    "print *export_bufs[0]",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4423
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- after build_stage_task_desc return ---\n"
bt 10
python
prt_where()
prt_safe_many([
    "info locals",
    "print rc",
    "print task.stage_id",
    "print task.acc_id",
    "print task.op_kind",
    "print task.split_kind",
    "print task.tile_count",
    "print task.num_managers",
    "print task.manager_ids[0]",
    "print task.manager_ids[1]",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4508
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- before worker gemm-run marker / prt_gemm_conv_run ---\n"
bt 10
python
prt_where()
prt_safe_many([
    "info locals",
    "print rc",
    "print ctx->stage_id",
    "print progress_sbatch",
    "print task.stage_id",
    "print task.acc_id",
    "print task.op_kind",
    "print task.split_kind",
    "print task.tile_count",
    "print task.num_managers",
    "print task.manager_ids[0]",
    "print task.manager_ids[1]",
])
end

printf "\n--- all thread bt at gemm-run frontier ---\n"
thread apply all bt
