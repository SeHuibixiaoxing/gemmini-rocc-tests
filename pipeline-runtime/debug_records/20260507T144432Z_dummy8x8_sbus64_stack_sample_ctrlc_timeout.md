# 20260507T144432Z dummy8x8/sbus64 gdbserver stack sample Ctrl-C timeout

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Type: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow:
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Run host: `i-07efa20cd54397c08`, private IP `192.168.1.113`
- GDB endpoint: `172.16.0.2:2345` through local tunnel `localhost:32345`
- Top-level commit before this record: `eb5bcd04`
- `generators/gemmini` commit before this record: `a23deed`
- `gemmini-rocc-tests` commit before this record: `cc52b8a`

## Command

The workload had already reached:

```text
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=222
```

Then stack sampling was started with GDB as the first TCP client:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_SAMPLE_COUNT=3 \
PRT_GDB_SAMPLE_SECONDS=75 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_stack_sample.sh \
  192.168.1.113 172.16.0.2:2345 32345
```

## Result

The initial attach succeeded.  The first timed `Ctrl-C` also succeeded, proving
that this run could dynamically stop the inferior after `continue`:

```text
Thread 1 (Thread 235.235 "rerocc_pipeline"):
#0  0x0000003ff7e7eb00 in strncmp () from .../libc.so.6
#1  starts_key(...) at pipeline-runtime/src/prt_gemmini_artifacts.c:236
#2  parse_mapping_file(...) at pipeline-runtime/src/prt_gemmini_artifacts.c:1003
#3  prt_validate_gemmini_artifacts(...) at pipeline-runtime/src/prt_gemmini_artifacts.c:1169
#4  prt_runtime_run(...) at pipeline-runtime/src/prt_runtime.c:4930
#5  prt_main_entry(...) at pipeline-runtime/src/main.c:341
```

This first stop is not the pipeline runtime card point. It was still in mapping
artifact validation.

After continuing again, the helper sent another `Ctrl-C`, but GDB never got a
new stop reply:

```text
continue
Continuing.
^Ctimeout waiting for Ctrl-C/SIGINT stop
```

The helper exited with `expect_rc=13`. Because the session ended without a clean
detach, this run was considered polluted and the run farm was terminated.

## Captured Evidence

Artifact directory:

```text
pipeline-runtime/debug_records/artifacts/20260507T144432Z_dummy8x8_sbus64_stack_sample_ctrlc_timeout/
```

Important files:

- `gdb/pairdummy-cfg32-stack-sample.expect.log`
- `gdb/expect-driver.stdout`
- `uartlog`
- `heartbeat.csv`
- `guest-sparse-log.txt`
- `status.txt`
- `guest-breadcrumb.bin`
- `guest-runner-proc-stage.txt`
- `guest-wrapper-proc-stage.txt`
- `gdbserver-info.txt`
- `gdbserver-log.txt`
- `triage.txt`

The guest status file still reported:

```text
state=running
periodic_sync_enable=1
guest_log_enable=1
breadcrumb_enable=1
dummy_gemmini_mode=1
skip_model_bin_load=1
skip_input_load=1
skip_golden_check=1
gdbserver_enable=1
```

The sparse log was much later than the first GDB stop. It had completed
artifact validation and was inside synthetic model allocation/prefault:

```text
[prt-progress] init validate-artifacts end elapsed_ms=1816
[prt-progress] init synthesize-model-bin begin reason=skip-model-bin-load
[prt-progress] synthetic-model alloc before-mmap size=17055744
[prt-progress] synthetic-model alloc after-mmap ptr=0x3ff6db4000 size=17055744
[prt-progress] synthetic-model alloc before-prefault path=(none) ptr=0x3ff6db4000 size=17055744 page_bytes=4096 mode=write-preserve
[prt-progress] synthetic-model alloc prefault-progress path=(none) touched_pages=3072/4164 touched_bytes=12582912/17055744 page_bytes=4096
```

The breadcrumb frontier was earlier than the sparse text log:

```text
version=1 enabled=1 slot_count=64 update_count=182
last_kind=runtime last_phase=runtime_init_done
line=4730
```

`triage_prt_capture.py` reported a stale spm-xlate frontier from runtime init,
not a worker-stage card point:

```text
frontier=spm-xlate-release mgr=11 cfg=31 phase=restore-end
verdict=inside spm-xlate release path (restore-end)
sparse_tail=synthetic-model alloc prefault-progress ...
```

For this run, the sparse log is the better indicator of forward progress after
the successful first GDB stop. The breadcrumb filters were set to segment 0 /
stage 0, and the last runtime breadcrumb did not cover synthetic model prefault.

## Interpretation

- This run does not reproduce the old token546 DMA card point.
- It also does not yet reach the worker pipeline card point.
- The first timed interrupt happened too early, inside YAML/mapping parsing.
- The second timed interrupt happened while the process was still in runtime
  initialization, most likely in the synthetic model prefault window.
- Therefore this result should not be interpreted as a hardware DMA or Gemmini
  hang.
- The key practical finding is that time-based no-breakpoint sampling is too
  coarse for this profile: under FireSim wall-clock time, a 75 second sample can
  still land in initialization, and the next interrupt can fail before any worker
  code runs.

## Next Step

Use a breakpoint-guided run instead of pure time sampling:

1. Start a fresh run farm and wait for `[gdbserver] phase=listening`.
2. Connect with GDB as the first TCP client.
3. Pre-set software breakpoints at runtime phase boundaries, especially:
   - `prt_runtime.c:5014` or nearby `runtime init-step=ready`
   - `prt_runtime.c:5126` segment begin after topology/setup
   - `prt_runtime.c:5199` worker `pthread_create`
   - `stage_worker_main`
4. Once stopped after initialization, dynamically add narrower breakpoints for:
   - `prt_gemm_conv_run`
   - `sync_stage_export_aliases`
   - `dma_blocking_wait`
5. Continue through only a bounded number of stops, collecting `bt`,
   `thread apply all bt`, local variables, and key runtime fields.

This avoids relying on `Ctrl-C` during the long synthetic model prefault window
and should place the debugger directly at the first real pipeline boundary.

## Cleanup

The polluted run was terminated with:

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

`terminaterunfarm` targeted `i-07efa20cd54397c08` and AWS reported
`Client.UserInitiatedShutdown: User initiated shutdown`.
