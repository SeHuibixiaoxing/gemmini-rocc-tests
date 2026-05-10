# 20260510T0922 no-DMA perf uart/cache-disabled stall

This artifact captures the no-DMA `ours2 gemini2` performance attempt that used
the stdio-rollback, cache-disabled, summary-only trace profile.

- AGFI: `agfi-077451484fe3b63c3`
- runworkload session:
  `pairdummy-sbus64-dummy8x8-no-dma-perf-cfg32-nic-notrace-runworkload-20260510-090013`
- instance: `i-018ec159180f85f6c`, private IP `192.168.1.72`
- profile id: `pairdummy-sbus64-dummy8x8-no-dma-perf-v2-cache-disabled`
- methods: `ours2 gemini2`
- important env:
  - `PIPELINE_RUNTIME_STDIO_CAPTURE_MODE=uart`
  - `PIPELINE_RUNTIME_DISABLE_MAPPING_CACHE=1`
  - `PIPELINE_RUNTIME_TRACE_SUMMARY_ONLY=1`
  - `PIPELINE_RUNTIME_NO_DMA_COMPUTE_ENABLE=1`
  - `PIPELINE_RUNTIME_GDBSERVER_ENABLE=0`

Observed frontier:

- UART reached `S99run`, started method `ours2`, and reached
  `[prt-early] calling runtime_run`.
- `heartbeat.csv` advanced once to `18337109557, 965` and then stopped.
- Host watchdog reached `hb_idle=313s` at that same heartbeat, beyond the
  `hb_idle=187s` completion point observed in the 2026-05-09 full no-DMA PASS.
- Debugfs showed `state=running`, empty `runner.stage`, and an empty
  `/root/pipeline-runtime-debug/traces` directory.
- No `ours2.trace` or `gemini2.trace` was emitted. This run is not performance
  evidence.

The run farm was terminated after evidence capture. The target instance entered
`shutting-down`, and the stale runworkload tmux/manager processes were cleaned
up.
