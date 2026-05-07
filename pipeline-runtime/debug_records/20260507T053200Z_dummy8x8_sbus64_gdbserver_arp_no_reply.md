# 20260507T053200Z - dummy8x8 sbus64 cfg32 NIC gdbserver ARP no-reply

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Build result: `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`
- Target: `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `192.168.1.79`
- Instance: `i-0f44087ecacb06179`
- Workload result dir: `sims/firesim/deploy/results-workload/2026-05-07--05-17-25-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`
- Run log: `sims/firesim/deploy/logs/2026-05-07--05-17-25-runworkload-OEZA7HHV87HOIHZT.log`

## What changed relative to the prior failure

The prior run failed because the guest network scripts did not select IceNet: `net_dev=` and `guest_ipv4=` were empty. The relaxed netdev selection fix was included in the rebuilt image for this run.

This run confirms that fix worked:

- Linux registered IceNet: `Registered IceNet NIC 00:12:6d:00:00:02`
- `/etc/init.d/S40network` completed: `Starting network: OK`
- gdbserver info file reported:
  - `net_dev=eth0`
  - `guest_ipv4=172.16.0.2`
  - `pid=235`
  - `gdbserver_pid=221`
- gdbserver log reported:
  - `Process /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux created; pid = 235`
  - `Listening on port 2345`

## Test command

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  192.168.1.79 172.16.0.2:2345 32345
```

Transcript:

`tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-expect-20260507T052653Z-192_168_1_79-172_16_0_2/pairdummy-cfg32-triage.expect.log`

## Result

The triage failed at `target remote` before any breakpoint or register operation:

```text
Remote debugging using :32345
Remote communication error.  Target disconnected: Connection reset by peer.
```

The SSH tunnel log showed the underlying run-host failure:

```text
channel 2: open failed: connect failed: No route to host
```

## Network evidence

On the run host:

```text
tap0: inet 172.16.0.1/16
172.16.0.2 dev tap0 FAILED
```

`ping -c 3 -W 2 172.16.0.2` from the run host failed:

```text
From 172.16.0.1 icmp_seq=1 Destination Host Unreachable
From 172.16.0.1 icmp_seq=2 Destination Host Unreachable
From 172.16.0.1 icmp_seq=3 Destination Host Unreachable
```

tap0 counters before/after ping:

- before: RX `0 packets`, TX `3 packets`
- after: RX `0 packets`, TX `6 packets`

`niclog0` was empty.

The switch saw host ARP frames and sent them toward the target over the shmem port:

```text
SWITCH DEBUG SSHPort tap_recv port=1 event=1 tap_len=42 bytes=ff ff ff ff ff ff ...
SWITCH DEBUG ShmemPort send port=0 event=1 round=0 valid_flits=6 last_flits=1 ...
```

There was no matching switch log evidence of packets returning from the target to tap0.

## Current interpretation

The software-side setup advanced past the previous netdev/IP failure. The current blocker is the FireSim NIC data path between host tap/switch and the simulated IceNet endpoint:

- guest userspace and gdbserver are alive;
- guest has `172.16.0.2`;
- run-host route to `172.16.0.0/16` exists through tap0;
- host ARP requests reach the FireSim switch and are emitted to the shmem port;
- no ARP reply or other target-to-host packet reaches tap0.

This points away from the GDB expect script and toward the NIC/switch/FPGA packet path, or a target-side IceNet RX/TX handling issue in this bitstream.

## Open questions

- Did the target receive the host ARP flits and fail to reply, or did the host-to-target shmem/NIC path stall before IceNet?
- Does a non-gdbserver pipeline-runtime workload progress on this AGFI, or is the NIC-only path the first blocker?
- How does this switch/shmem trace differ from the old single-core Rocket + NIC AGFI that passed remote gdbserver tests?
