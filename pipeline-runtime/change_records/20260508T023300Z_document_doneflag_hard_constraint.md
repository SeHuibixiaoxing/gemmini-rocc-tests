# 2026-05-08 02:33 UTC: document doneflag as a known-bad DMA completion signal

## Context

The project had already verified that DMA doneflag completion is problematic.
Recent debugging accidentally re-enabled `PIPELINE_RUNTIME_DMA_BLOCKING_WAIT_POLL_TIMEOUT_ENABLE=1`
in the fixed profile and produced evidence that looked like DMA completion while
bypassing the intended blocking wait/fence method.

## Change

- Promoted the doneflag rule in `docs/constraints/hard_constraints.md` from a
  short bullet to an explicit hard constraint:
  doneflag is known-bad as a completion semantic and must not drive DMA control
  flow.
- Strengthened `docs/reference/linux_dma_guardrails.md` to state that any result
  that skips `hw_dma_fence()` / blocking wait via doneflag polling is invalid
  DMA completion evidence.

## Required interpretation going forward

- Mainline Linux DMA completion proof must be `hw_dma_fence()` / blocking wait.
- doneflag may only be used as auxiliary telemetry.
- If a run shows doneflag polling was enabled, first fix profile/image freshness;
  do not use that run to conclude where pipeline-runtime is blocked.
