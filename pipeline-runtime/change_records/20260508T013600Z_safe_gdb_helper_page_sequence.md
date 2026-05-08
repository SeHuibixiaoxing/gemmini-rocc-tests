# 2026-05-08T01:36Z safe GDB helper page-accounted sequence

## Change

Extended `pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh`
with an optional fixed-load page-accounted sequence.

New environment variables:

- `PRT_GDB_PAGE_ACCOUNTED_SEQUENCE`: comma-separated page indices to wait for
  after the already-proven page0 accounting marker, for example
  `1,2,15,16,23,24`.
- `PRT_GDB_PAGE_STAGE_ID`: local stage id for page filters, default `0`.
- `PRT_GDB_PAGE_MANAGER_ID`: manager id for page filters, default `0`.
- `PRT_GDB_PAGE_TENSOR_ID`: tensor id for page filters, default `0`.

The helper still uses a single GDB session and still avoids shell-level
`timeout` kills. If a page marker does not arrive, the existing timeout path
sends Ctrl-C from inside expect, captures thread/register/backtrace state, then
detaches.

## Reason

The 2026-05-08 safe-helper run proved tensor0 token1/page0 reaches fixed-load
page accounting, but a post-detach reconnect to `gdbserver --once` did not
recover useful thread or register state. Future probes therefore need to gather
all evidence inside the first GDB session.

The page-accounted sequence lets one fresh run advance directly from page0 to
page1/page2/page15/page23 boundaries without repeatedly rebuilding or relying
on a second attach.

## Validation

Ran:

```bash
bash -n pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh
pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh --help
git diff --check -- pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_worker_dma_chain_safe.sh
```

All checks passed.
