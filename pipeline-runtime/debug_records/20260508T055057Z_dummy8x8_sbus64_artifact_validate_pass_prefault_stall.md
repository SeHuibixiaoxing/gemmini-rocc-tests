# 20260508T055057Z dummy8x8/sbus64 artifact validation pass and synthetic prefault stall

## Context

- AGFI / AFI: `agfi-077451484fe3b63c3` / `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `i-09ca3c4cdf2bbe6a0`, private IP `192.168.1.192`
- Workload results:
  `sims/firesim/deploy/results-workload/2026-05-08--05-42-43-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Captured artifacts:
  `pipeline-runtime/debug_records/artifacts/20260508T055057Z_dummy8x8_sbus64_artifact_validate_pass_prefault_stall/`
- GDB artifact directory inside the capture:
  `pipeline-runtime/debug_records/artifacts/20260508T055057Z_dummy8x8_sbus64_artifact_validate_pass_prefault_stall/gdb/pairdummy-cfg32-artifact-validate-stage-20260508T055057Z-192_168_1_192-172_16_0_2/`

## Guest Setup

The run used the normal gdbserver profile with marker filtering disabled:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0
```

Image freshness checks passed before GDB attached:

```text
local/remote image sha256 = c4dc60658d14ddc1690a1a5d728fb36cc4a52f9879edb269ea4339908b02f0ca
remote /firemarshal.env sha256 = e118b3d0d1f676c50d9053f970e2ec7905c35386019a413a2141ec6291d73c8d
runtime ELF sha256 = 62aa6489325dcec067e94ad04401f2b26f544afc73c1bb57bb1aabdc74414f42
```

gdbserver reported the expected phases in UART:

```text
[gdbserver] phase=prelaunch method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=0
[gdbserver] phase=listening method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=222
[gdbserver] phase=inferior method=ours2 guest_ipv4=172.16.0.2 endpoint=172.16.0.2:2345 pid=235
```

## GDB Command

The host helper was:

```sh
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_VALIDATE_SEG=14 \
PRT_GDB_VALIDATE_STAGE=0 \
PRT_GDB_HIT_TIMEOUT=900 \
PRT_GDB_FINISH_TIMEOUT=120 \
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_artifact_validate_stage_frontier.sh \
  192.168.1.192 172.16.0.2:2345 32345
```

GDB was the first TCP client to `gdbserver --once`; no `nc`, `curl`, or
`telnet` probe was used.

## Artifact Validation Result

GDB hit the requested source breakpoint:

```text
validate_stage_against_mapping_entries(... seg_idx=14, stage_idx=0, stage_id=35, layer_id=35)
```

The selected stage and mapping database were coherent:

```text
db->count = 16848
stage->acc_util = 2
stage->tensor_id_count = 4
stage->dram_bypass_count = 4
stage->spm_bypass_count = 4
stage->local_spm_tensor_count = 4
stage->local_spm_page_span = 193
```

The bounded `finish` returned successfully:

```text
Value returned is $14 = 0
```

After return, `prt_validate_gemmini_artifacts()` had advanced to
`seg_idx=14`, `stage_idx=1`. GDB detached cleanly.

## Corrected Frontier

The prior `20260508T051922Z` record observed the sparse log at:

```text
artifacts validate stage-begin seg=14 local_stage=0 ...
```

and treated it as a possible artifact-validation frontier. This run disproves
that narrow hypothesis. After GDB detached, the guest completed all artifact
validation:

```text
[prt-progress] artifacts validate segment-end seg=14 stages=5 elapsed_ms=747
[prt-progress] artifacts validate end ... segments=15
[prt-progress] init validate-artifacts end elapsed_ms=3249
```

The new live frontier is synthetic model allocation while
`PIPELINE_RUNTIME_SKIP_MODEL_BIN_LOAD=1`:

```text
[prt-progress] init synthesize-model-bin begin reason=skip-model-bin-load
[prt-progress] synthetic-model alloc before-prefault path=(none) ptr=0x3ff6db4000 size=17055744 page_bytes=4096 mode=write-preserve
[prt-progress] synthetic-model alloc prefault-progress path=(none) touched_pages=3328/4164 touched_bytes=13631488/17055744 page_bytes=4096
```

No `synthetic-model alloc after-prefault`, `before-mlock`, or `after-mlock`
line appeared before capture and cleanup.

The relevant code is:

```text
pipeline-runtime/src/prt_runtime.c:957  prefault_and_lock_blob()
pipeline-runtime/src/prt_runtime.c:986  for (size_t off = 0; off < blob_size; off += step)
pipeline-runtime/src/prt_runtime.c:988  const uint8_t value = touch[off]
pipeline-runtime/src/prt_runtime.c:989  touch[off] = value
pipeline-runtime/src/prt_runtime.c:1330 allocate_synthetic_model_blob() calls prefault_and_lock_blob()
```

The stall occurs before `mlock(buf, blob_size)`, so this specific run is not
evidence for a DMA fence/doneflag/export problem.

## Historical Cross-Check

The earlier `20260508T011812Z_dummy8x8_sbus64_safe_first_page_pass` artifacts
show the same 17,055,744 byte synthetic blob can complete prefault and `mlock`
on this hardware/profile family:

```text
synthetic-model alloc prefault-progress ... touched_pages=4096/4164 ...
synthetic-model alloc after-prefault ...
synthetic-model alloc before-mlock ...
synthetic-model alloc after-mlock rc=0 errno=0 ...
```

That means the current evidence is narrower than "synthetic prefault always
hangs". The next run needs direct GDB control around the prefault loop to see
whether the guest is stuck on a page fault, in the progress log path, or was
disturbed by the detach/run-control sequence.

## Cleanup

Captured artifacts were copied before cleanup. The run farm was terminated
through:

```sh
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=0 \
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0 \
pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh terminate
```

FireSim selected and terminated `i-09ca3c4cdf2bbe6a0`; AWS reported it in
`shutting-down` state immediately after the terminate workflow.

## Next Step

Start a fresh gdbserver run with:

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=synthetic-model-prefault-begin
PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=0
```

Then attach GDB as first TCP client, stop at `prt_gdb_marker_stop`, set a
bounded breakpoint in `prefault_and_lock_blob()` near the progress branch after
`touched_pages >= 3328`, and use an expect-driven helper that can send Ctrl-C
inside GDB and dump `bt full`, `info locals`, registers, and the current PC
window if the loop stops making progress.

Do not use DMA doneflag polling as completion evidence. doneflag is known-bad
and is only valid as auxiliary telemetry in records that also include blocking
wait/fence evidence.
