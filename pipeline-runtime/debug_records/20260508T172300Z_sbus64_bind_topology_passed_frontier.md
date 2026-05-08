# 2026-05-08 17:23 UTC - sbus64 gdbserver bind-topology frontier

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload:
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Runworkload tmux session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260508-170002`
- Run host: `192.168.2.14`
- EC2 instance terminated after this round: `i-0effaf19e7ba75d3b`

## Important setup correction

The GDB session used the FireMarshal-packaged ELF:

```sh
.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

The earlier non-`build/` ELF is not valid for this workload image.

GDB confirmed the expected symbols:

```text
info address main
main = 0x12060

x/8i main
0x12060 <main>: addi sp,sp,-16
...
0x1206a <main+10>: j 0x1214a <prt_main_entry>

info address prt_main_entry
prt_main_entry = 0x1214a
```

## Breakpoints used

Only function-entry breakpoints were used. This avoids the previous dense-code
line-breakpoint failure where a software breakpoint was inserted in the middle
of a 4-byte RISC-V instruction.

```gdb
break main
break prt_main_entry
break prt_runtime_run
break prt_action_bind_topology
break exit
break abort
continue
```

## Observed progress

The program reached `main`, `prt_main_entry`, and `prt_runtime_run`.

Runtime arguments observed at `prt_runtime_run`:

```text
model_yaml = /root/rerocc-linux-tests/pipeline-runtime/bertmini/model.layers.yaml
layer_mapping_yaml = /root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml
pipeline_yaml = /root/rerocc-linux-tests/pipeline-runtime/bertmini/pipeline_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.ours2.yaml
skip_model_bin_load = 1
skip_input_load = 1
skip_golden_check = 1
batch = 8
```

`prt_action_bind_topology()` was hit three times and returned success each time:

```text
action_id=1 segment_idx=0 state=PRT_ACTION_ALLOCATED
spm_source.in_stage_count=2
spm_source.weight_count=2
return rc=0

action_id=2 segment_idx=1 state=PRT_ACTION_ALLOCATED
spm_source.in_stage_count=2
spm_source.weight_count=2
return rc=0

action_id=3 segment_idx=2 state=PRT_ACTION_ALLOCATED
spm_source.in_stage_count=8
spm_source.weight_count=4
alias_page_count=1482
return rc=0
```

## Updated conclusion

The previous hypothesis that segment 1 is stuck or failing inside
`prt_action_bind_topology()` is no longer supported by the corrected-ELF GDB
run. Segment 1 bind-topology returned `PRT_OK`, and segment 2 bind-topology also
returned `PRT_OK`.

The current frontier is after `prt_action_bind_topology()` for
`action_id=3 / segment_idx=2`, before the next action's bind-topology entry or
normal process exit/abort.

Static code position after the successful bind is in `prt_runtime_run()`:

- `runtime_flush_spm_xlate()`
- sink selection
- target subbatch computation
- `pthread_attr_init()` / `pthread_attr_setstacksize()`
- per-stage `pthread_create()`
- main thread wait loop polling `min_sink_sbatch_offset()`
- worker-side waits, compute dispatch, export alias sync, and DMA paths

## Ctrl-C behavior

After continuing past the third successful bind-topology, no further breakpoint
was hit for about 60 seconds. A first GDB Ctrl-C did not regain control within
about 40 seconds. A second Ctrl-C caused:

```text
Disconnected from target.
```

No live thread backtrace was obtained in this round. This is consistent with
earlier observations that once the runtime is in some accelerator/wait regions,
gdbserver Ctrl-C is not a reliable way to recover a stack.

## Next debugging action

Start a fresh `gdbserver --once` run and place breakpoints before entering the
post-bind threaded execution region, instead of waiting until the process is
already hard to interrupt.

Recommended next breakpoints:

```gdb
break prt_action_bind_topology
break runtime_flush_spm_xlate
break pthread_create
break stage_worker_main
break min_sink_sbatch_offset
break prt_pipebuf_wait_full
break prt_ring_wait_ready
break stage_wait_exports_ready
break sync_stage_export_aliases
break exit
break abort
```

At each hit, inspect `rt->active_action->action_id`, `segment_idx`,
`stage_id` where available, `exec->stage_thread_count`, `created_threads`,
`target_subbatch`, and the relevant pipe/ring buffer offsets.

Do not add guest file breadcrumbs for this step. The useful evidence should
come from GDB breakpoints, GDB variables, and UART wrapper metadata only.
