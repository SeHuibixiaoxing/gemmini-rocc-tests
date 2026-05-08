# 2026-05-08T00:19Z dummy8x8/sbus64 page-level GDB frontier: token16 seen, token24 lost by remote close

## Goal

Use the page-level GDB marker sites from commit `ac1c71a` to narrow the first
fixed-load DMA frontier for segment 0, local stage 0, tensor 0 on the current
dummy Gemmini 8x8 / sbus64 / cfg32 / NIC / no TraceIO F2 AGFI.

This run intentionally avoided source-line temporary breakpoints and optimized
locals. The helper only stopped on semantic marker sites written by the runtime:

- `DMA_WAIT_ENTER = 12`
- `DMA_WAIT_RETURN = 13`
- `DMA_SUBMITWAIT_AFTER_WAIT = 25`
- `DMA_SUBMITWAIT_AFTER_CLEANUP = 26`
- `DMA_FIXED_LOAD_SUBMITWAIT_BEGIN = 27`
- `DMA_FIXED_LOAD_SUBMITWAIT_END = 28`
- `DMA_FIXED_LOAD_PAGE_ACCOUNTED = 29`

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Config: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host instance: `i-047991d56243ab2f2`
- Run host private IP: `192.168.1.32`
- Guest gdbserver endpoint: `172.16.0.2:2345`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workflow script:
  `pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_gdbserver_cfg32_nic_notrace_workflow.sh`
- Runworkload session:
  `pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-191832`
- Cluster tag:
  `pairbertb8d12s64gdbcfg32nicnt`

Before GDB attached, the guest reached:

```text
[gdbserver] phase=listening ... endpoint=172.16.0.2:2345 pid=221
[gdbserver] phase=inferior ... pid=233
```

## Command

The first TCP client to the guest gdbserver port was GDB through the helper:

```bash
PRT_GDB_STATIC_NEIGH_MAC=00:12:6d:00:00:02 \
PRT_GDB_MARKER_TIMEOUT=2400 \
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_token2_frontier.sh \
  192.168.1.32 172.16.0.2:2345 32345
```

The tested local artifacts were:

```text
062f9dcc6899f355f26d0444bf0d8bf3bc9abe2d146718ac6e076b0c82793a3a  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
a49087a09c60f67849132be6e5dce73b116a75f69c446020ba6c3bcbfc0b0cab  generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_segment0_token2_frontier.sh
```

The FireMarshal build/install completed before the run. A direct RISC-V Linux
make in an unsourced shell still failed because `CC_LINUX` was not set; that is
not evidence against the staged image because FireMarshal produced the tested
ELF above.

## Result

Partial pass for page-level DMA frontier localization.

The marker path proved that the first fixed-load page completes and that the
runtime can keep progressing through at least token 16 / page 15:

- segment-begin marker hit: `site_id=2`, `segment_idx=0`.
- token 1 wait-return hit: `site_id=13`, `page_idx=0`, `token_id=1`,
  `rc=0`.
- submitwait after wait hit for token 1: `site_id=25`, `page_idx=0`,
  `token_id=1`, `rc=0`.
- submitwait cleanup hit for token 1: `site_id=26`, `page_idx=0`,
  `token_id=1`, `rc=0`.
- fixed-load submitwait returned for page 0: `site_id=28`, `page_idx=0`,
  `rc=0`.
- page 0 was accounted: `site_id=29`, `page_idx=0`, `aux0=1024`,
  `aux1=64512`.
- next fixed-load submitwait began for page 1: `site_id=27`,
  `page_idx=1`.
- token 2 wait-enter and wait-return hit for page 1 with `rc=0`.
- token 4 wait-return hit for page 3 with `rc=0`.
- token 8 wait-return hit for page 7 with `rc=0`.
- token 16 wait-return hit for page 15 with `rc=0`.

The next programmed stop was token 24 wait-return for page 23. Before that stop
arrived, GDB failed with:

```text
Remote connection closed
```

The SSH tunnel log also ended with:

```text
Connection to 192.168.1.32 closed by remote host.
```

The runworkload manager later saw repeated SSH failures to `192.168.1.32` and
the outer watchdog terminated the run at `2026-05-07T22:18:33Z`. A post-run AWS
query showed no live F2 instances for the cluster tag.

## Interpretation

This test rules out the earlier suspicion that the page-0 fixed-load
submit/wait cleanup path itself is the first hard stop. Page 0 reaches the
accounting marker, and later wait-return markers show progress through page 15.

The current frontier is narrower but still ambiguous:

- A runtime/DMA hang may occur after token 16 / page 15 and before token 24 /
  page 23 wait-return.
- The process or gdbserver may have exited before the token 24 stop.
- The host-to-run-host SSH path or the NIC/control path may have failed while
  the target was still running.

Because the failure is a closed remote connection rather than a clean marker
timeout, this record must not be treated as proof that page 23 is the bad page.
The only hard dynamic fact is that page 15 returned and the next requested
coarse stop at page 23 was not observed.

## Artifacts

Artifacts were archived under:

```text
pipeline-runtime/debug_records/artifacts/20260508T001932Z_dummy8x8_sbus64_token16_no_token24_remote_closed/
```

Important files:

- `gdb/pairdummy-cfg32-marker-stop.gdb`
- `gdb/pairdummy-cfg32-marker-stop.gdb.log`
- `gdb/ssh-tunnel.log`
- `gdb/static-neigh.log`
- `manager/runworkload.pane.log`
- `manager/runworkload.command.sh`
- `manager/runworkload.exitcode`
- `manager/infrasetup.pane.log`
- `manager/marshal-build.pane.log`
- `manager/marshal-install.pane.log`
- `aws/instance-i-047991d56243ab2f2.json`
- `aws/live-f2-after-terminate.json`
- `local/sha256.txt`

## Next Probe

The next dynamic probe should reduce the step size after page 15:

1. Start a fresh run with the same AGFI and staged image closure.
2. Stop first at token 16 wait-return to skip the already-proven prefix.
3. Walk page 16 through page 23 with markers `27`, `28`, `29`, and `13` rather
   than jumping directly to token 24.
4. If the remote closes again, archive SSH tunnel, gdbserver, runworkload, and
   run-host evidence immediately and distinguish process exit from host/NIC
   control-path loss.

Static follow-up before the next run:

- Audit the fixed-load page loop around `dma_copy_host_to_spm_pages_linux()`.
- Verify the marker filter cannot skip an important page-16..23 state.
- Check whether token accounting, page accounting, or direct/bounce source
  address selection changes around a 16-page boundary.
