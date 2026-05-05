# 20260505T190000Z cfg32 gdb triage helper

## Context

I added a small host-side helper to reduce the chance of wasting the first
`gdbserver --once` connection on manual setup mistakes.

## Script

- `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh`

## Behavior

- Opens an SSH `-L` tunnel from the manager host to the FireSim run host and
  guest endpoint.
- Uses the current host-side `rerocc_pipeline_runtime-linux` as the GDB target.
- Checks that the target ELF has debug info.
- Sets the current first-breakpoint list:
  `prt_runtime_run`, `prt_action_bind_topology`, `stage_prepare_exec_views`,
  `prt_dma_submit`, `prt_dma_wait`, `dma_blocking_wait`,
  `prt_gemmini_spm_xlate_program`, `prt_gemmini_spm_xlate_flush`,
  `prt_rr_release_scope`, `prt_gemm_conv_run`, and `prt_gemm_fence`.
- Defaults to `continue` after breakpoints unless `PRT_GDB_CONTINUE=0`.

## Validation

Ran:

```bash
bash -n generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_first_triage.sh --help
```

Both succeeded. No FireSim run host or AGFI was touched.
