# 20260507T101900Z - dummy8x8 sbus64 gdbserver startup sample

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy8x8, `4c12p12`, `sbus64`, `cfg32`, NIC, no TraceIO
- Runtime workflow:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-0499d7c411970fcd1`, private IP `192.168.1.99`

## Goal

Validate that the current fail-fast software image still allows remote GDB to
be the first `gdbserver --once` TCP client, then sample runtime state before
the known DMA fence frontier.

The run used the fail-fast prep from `20260507T094913Z`:

- `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1`
- `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1`
- `EXPORT_DMA_TIMEOUT_MS=5000`
- `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`

## Commands

GDB first-client stack sampling:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_SAMPLE_COUNT=4 \
PRT_GDB_SAMPLE_SECONDS=15 \
PRT_GDB_EXPECT_TIMEOUT=300 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_stack_sample.sh \
  192.168.1.99 172.16.0.2:2345 32345
```

Evidence:

- GDB output:
  `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-stack-sample-20260507T100839Z-192_168_1_99-172_16_0_2/`
- Guest capture:
  `tmp/firesim-aws-f2/prt-captures/20260507T1010_dummy8x8_sbus64_after_gdb_detach/`
- Runworkload log:
  `sims/firesim/deploy/logs/2026-05-07--09-57-58-runworkload-NXAN8U3BJUCXE67U.log`
- Results dir:
  `sims/firesim/deploy/results-workload/2026-05-07--09-57-58-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

## Result

Remote GDB connection itself worked:

- `target remote :32345` connected to guest `172.16.0.2:2345`.
- `info threads`, `thread apply all bt`, register reads and disassembly worked.
- Four `continue` + Ctrl-C samples stopped successfully.
- `detach` completed.

The helper returned nonzero only because `GDB_STACK_SAMPLE_*` markers were
printed to expect stdout rather than the transcript, while the self-check only
searched the transcript. This has been fixed in
`run_pairdummy_cfg32_gdbserver_stack_sample.sh`.

## Observed Stack Context

Initial attach stopped in the dynamic loader before user `main`.

Samples 1 and 2 stopped in layer-mapping file load:

```text
pread64
load_file(... gemmini_layer_mapping...sbus64.yaml) at prt_gemmini_artifacts.c:189
parse_mapping_file(...) at prt_gemmini_artifacts.c:867
prt_validate_gemmini_artifacts(...) at prt_gemmini_artifacts.c:1169
prt_runtime_run(...) at prt_runtime.c:4922
prt_main_entry(...) at main.c:341
```

Sample 3 stopped while counting mapping entries:

```text
strchrnul
strchr
count_mapping_entries_in_buf(...) at prt_gemmini_artifacts.c:759
parse_mapping_file(...) at prt_gemmini_artifacts.c:877
```

Sample 4 stopped while parsing a u32 list:

```text
parse_u32_token_advance(...) at prt_gemmini_artifacts.c:133
parse_u32_list_from_value(...) at prt_gemmini_artifacts.c:273
parse_mapping_file(...) at prt_gemmini_artifacts.c:966
```

Conclusion: this GDB session did not reach the DMA/RoCC card point. It only
proved that startup-stage user-space stack sampling still works on the current
AGFI and image.

## Guest/Breadcrumb Context

After detach, guest debug files stayed at:

- `bertmini-batch8.log` size: `219105` bytes
- `bertmini-batch8.breadcrumb.bin` size: `7272` bytes
- breadcrumb last slot:
  `kind=runtime phase=runtime_init_done line=4722`
- text log last progress:
  `artifacts mapping parse progress ... entries=8192 elapsed_ms=522`

The mapping file being parsed was:

```text
/root/rerocc-linux-tests/pipeline-runtime/bertmini/gemmini_layer_mapping.rerocc_globalnoc_pairmanager_dummy8x8_c4_g12_d12_spad1024kb_dram19_noc64_mac64_sbus64.yaml
```

It was `12029909` bytes with estimated `16848` entries. The cache file exists
in the image, but the profile intentionally had
`PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`, so YAML parsing was on the slow
path.

## Known DMA Frontier Context To Reproduce Next

The previous real frontier from
`20260507T091637Z_dummy8x8_sbus64_pipeline_runtime_stack_sample_dma_fence_frontier.md`
was reconstructed again from HybridMapper artifacts:

```text
kind=dma phase=dma_wait_before_fence
segment=0 global_stage=0 local_stage=0 subbatch=1
tensor=2 token=546 manager=0 page=31
src=0x40702c00 dst=0x103362000 done_pa=0x103217000 timeout_ns=5000000000
```

Mapping context:

- stage/layer: segment 0, stage 0, layer 0
- layer type: `conv`
- split: `oc`
- tensor 2 role: export tensor
- tensor 2 total bytes: `65536`
- tensor 2 page count: `64`
- page 31 byte offset: `31744`
- page 31 local SPM addr: `163840`
- page 31 local vpage: `160`
- page 31 transfer bytes: `1024`

This is the small repro window for the next GDB run. The next run should not
detach after generic startup sampling. It should set a conditional breakpoint
near this frontier:

```text
dma_blocking_wait if tok != 0 && tok->stage_idx == 0 && tok->tensor_id == 2 && tok->id >= 546
```

Then print token fields and continue for a short interval before Ctrl-C. A new
helper was added for exactly this path:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_FRONTIER_TIMEOUT=1200 \
PRT_GDB_POST_HIT_SECONDS=8 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_dma_frontier.sh \
  <run-host-private-ip> 172.16.0.2:2345 32345
```

If startup time dominates, the sbus64 fixed env now supports:

```sh
PAIRDUMMY_SBUS64_DISABLE_MAPPING_CACHE=0
```

This keeps the default low-disturbance behavior unchanged, but allows a
targeted rerun to use the prebuilt mapping cache and reach the DMA frontier
faster. Any such rerun must record the cache setting explicitly.

## Current Interpretation

The current card point is still not reclassified by this run. Evidence says:

1. remote gdbserver/NIC/GDB are still healthy enough for first-client attach
   and Ctrl-C while the runtime is in normal user-space startup code;
2. generic sampling consumed the only `gdbserver --once` session too early;
3. after detach, the run no longer provides a reliable way to recover
   user-space stacks;
4. the actionable next test is a DMA-frontier conditional breakpoint run,
   not another generic startup sampler.
