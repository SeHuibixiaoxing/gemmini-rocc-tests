# 2026-05-07T16:12Z dummy8x8/sbus64 token548 marker run aborted before gdbserver

## Goal

Use the flexible source-marker workflow to stop at `dma-wait-return` for
stage0/tensor2/token548, then switch breakpoint sets inside the same
`gdbserver --once` GDB session.

This was meant to validate whether token548 naturally returns and then continue
toward a later token frontier.

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Config: dummy8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Run host launched: `i-035a33f9b328d554b`
- Run host private IP: `192.168.1.129`
- Runtime config:
  `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB:
  `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`

## Intended Marker Env

```text
PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
PIPELINE_RUNTIME_GDB_MARKER_SITE=dma-wait-return
PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
PIPELINE_RUNTIME_GDB_MARKER_MANAGER=0
PIPELINE_RUNTIME_GDB_MARKER_TENSOR=2
PIPELINE_RUNTIME_GDB_MARKER_TOKEN=548
```

`debug-preflight` passed with probe tier 2. `infrasetup` also passed. Local and
remote image freshness both confirmed:

```text
runtime-binary sha256=6f59324bde25e49b2ad80afbf6e9d21fee0270699e4adb38cd29a74c612a5622
firemarshal-env sha256=35dd7a7163a0de7338c29a27d0835687cccde22b8ba4d4fff8d9031c064ad0c4
remote_image sha256=ea4f9b432129e838b1600326864b6d1e8ed52091070ede44105705d57ed73c1a
```

## What Happened

`launchrunfarm` first hit temporary F2 capacity errors, then successfully
launched:

```text
i-035a33f9b328d554b 192.168.1.129
```

`infrasetup` flashed `agfi-077451484fe3b63c3` and passed FireSim driver
readiness preflight.

`runworkload` started as:

```text
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-160708
```

The run did not reach `[gdbserver] phase=listening`. AWS reported the instance
entered `shutting-down` almost immediately:

```text
State: shutting-down
Reason: User initiated (2026-05-07 16:07:55 GMT)
```

The runworkload log was still polling when the instance disappeared and then
started reporting SSH banner failures. The host watchdog log only reported:

```text
[prt-host-watchdog] no pending/running instances remain for pairbertb8d12s64gdbcfg32nicnt
```

No GDB command was run in this attempt, and no conclusion can be drawn about
token548 or pipeline-runtime progress.

## Root Cause Found Locally

After the abort, the manager host still had multiple stale local FireSim
`runworkload` processes and tmux sessions from previous cfg32 NIC runs, all
using the same runtime config and cluster tag:

```text
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-135320
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-143555
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-152059
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-154236
pairdummy-sbus64-dummy8x8-gdbserver-cfg32-nic-notrace-runworkload-20260507-160708
```

There were also many orphaned `python3 ... firesim runworkload` monitor
processes for the same runtime/HWDB. These stale managers could race with a new
run because they all poll the same FireSim cluster tag and runtime config.

## Cleanup Performed

No directories were deleted.

Local stale runworkload processes matching the cfg32 NIC runtime config were
terminated, and the stale runworkload tmux sessions were killed. After cleanup:

```text
pgrep ... firesim runworkload ... cfg32_nic_notrace: no live process
tmux ls | grep pairdummy-sbus64...runworkload: no session
aws ec2 describe-instances pending/running f2.*: no instances
```

## Interpretation

This is an infrastructure hygiene failure, not a hardware or runtime result.
The next token548 marker run should be started only after checking that no stale
local runworkload managers remain for this runtime config.

The useful new constraint is:

- Before launching a new gdbserver debug run, check both AWS F2 state and local
  FireSim manager state.
- Stale `firesim runworkload` Python processes and tmux sessions for the same
  cluster tag must be stopped before a fresh `launchrunfarm`.
