# 2026-05-08 13:23Z - sbus64 dummy8x8 GDB-only image freshness

## Context

- Target hardware for next run: AGFI `agfi-077451484fe3b63c3`, AFI `afi-07989ce9ce725a690`
- Hardware shape: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`

## Why This Checkpoint Exists

The previous live GDB run stopped after stage0/subbatch3 C2 retire and before the worker-done progress log. Because earlier pipeline-runtime runs repeatedly showed file logging can move apparent stalls, this checkpoint prepares a run whose primary observation path is remote GDB, not guest file logs.

## Image Closure

Commands run:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh marshal-build
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh marshal-install
source generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_fixed_env.sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/render_pairdummy_guest_env.sh tmp/pipeline-runtime-effective-guest-env/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.firemarshal.env
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/apply_guest_env_to_image.sh software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img tmp/pipeline-runtime-effective-guest-env/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.firemarshal.env
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh local-freshness
```

The first `local-freshness` attempt before image patching failed only on `/firemarshal.env`, because that subcommand validates the image but does not patch it. After applying the same env patch used by the infrasetup workflow path, freshness passed.

## Freshness Result

`local-freshness` PASS:

- runtime binary sha256: `4fe6439b04996c1a5b0d064d048c1525b0bfda560df969255222c67b31402eeb`
- `/firemarshal.env` sha256: `10329a9c03fac1549d237d05a2d9256c6fa0ce691f43c19716f1311773ee5668`
- gdbserver present in image

Important guest env values verified in the image:

```sh
export PIPELINE_RUNTIME_PROFILE_ID='pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly'
export PIPELINE_RUNTIME_UART_LOG_ENABLE='0'
export PIPELINE_RUNTIME_GUEST_LOG_ENABLE='0'
export PIPELINE_RUNTIME_GUEST_DEEP_LOG_ENABLE='0'
export PIPELINE_RUNTIME_AUDIT_LOG_ENABLE='0'
export PIPELINE_RUNTIME_CHECKPOINT_LOG_ENABLE='0'
export PIPELINE_RUNTIME_STDIO_CAPTURE_MODE='uart'
export PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE='1'
export PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE='0'
export PIPELINE_RUNTIME_BREADCRUMB_ENABLE='0'
export PIPELINE_RUNTIME_GDBSERVER_ENABLE='1'
export PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR='0.0.0.0'
export PIPELINE_RUNTIME_GDBSERVER_PORT='2345'
export PIPELINE_RUNTIME_GDB_MARKER_ENABLE='0'
```

## Next Step

Launch/infrasetup/run this image on the current sbus64 dummy8x8 NIC AGFI. The next runtime frontier should be established with live remote GDB breakpoints and GDB memory/register/stack inspection, not guest file logs or breadcrumb records.
