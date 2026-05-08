printf "\n--- segment2 stage1 stage_wait_exports_ready probe ---\n"
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

tbreak stage_wait_exports_ready
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- entered stage_wait_exports_ready on marker thread ---\n"
bt 10
python
prt_where()
prt_safe_many([
    "info args",
    "info locals",
    "print stage_id",
    "print subbatch",
    "print export_count",
    "print shared_pair_count",
    "print export_bufs[0]",
    "print *export_bufs[0]",
])
end

tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:3592
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- stage_wait export-kind dispatch ---\n"
bt 10
python
prt_where()
prt_safe_many([
    "info locals",
    "print i",
    "print b",
    "print b->buffer_id",
    "print b->tensor_id",
    "print b->stage_idx",
    "print b->segment_idx",
    "print b->kind",
    "print b->in_use_idx",
    "print b->full[0]",
    "print b->full[1]",
    "print b->subbatch_offset",
    "print b->shared_tag[0]",
    "print b->shared_tag[1]",
    "print shared_pair_count",
    "print shared_pairs[0]",
])
end

tbreak prt_process_c4
condition $bpnum $_thread == $prt_marker_thread
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:3609
condition $bpnum $_thread == $prt_marker_thread
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:3615
condition $bpnum $_thread == $prt_marker_thread
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:3621
condition $bpnum $_thread == $prt_marker_thread
tbreak /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c:4360
condition $bpnum $_thread == $prt_marker_thread
continue
printf "\n--- first stage_wait branch/return stop ---\n"
bt 12
python
prt_where()
prt_safe_many([
    "info args",
    "info locals",
    "print rc",
    "print b",
    "print b->buffer_id",
    "print b->tensor_id",
    "print b->kind",
    "print b->in_use_idx",
    "print b->full[0]",
    "print b->full[1]",
    "print b->subbatch_offset",
    "print b->shared_tag[0]",
    "print b->shared_tag[1]",
])
end

printf "\n--- all thread bt at stage_wait probe stop ---\n"
thread apply all bt
