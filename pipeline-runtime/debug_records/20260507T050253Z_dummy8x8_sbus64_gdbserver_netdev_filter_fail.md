# 20260507T050253Z dummy8x8 sbus64 cfg32 NIC gdbserver failure

## Target

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Build result: `sims/firesim/deploy/results-build/2026-05-06--12-37-54-firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace/`
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Run host: `192.168.1.44`, instance `i-0881ca00449c8476a`
- Workload result dir: `sims/firesim/deploy/results-workload/2026-05-07--04-48-04-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace/`

## What passed before the failure

- AWS image creation completed and the AGFI entered `available`.
- `launchrunfarm` and `infrasetup` completed using the prebuilt `driver_tar`.
- Linux boot reached Buildroot init.
- UART showed the NIC device is present in the target:
  - `icenet 10016000.ice-nic: IceNet TX coherent bounce enabled; checksum offload and SG disabled`
  - `Registered IceNet NIC 00:12:6d:00:00:02`
- The workload entered `S99run`.
- Read-only `debugfs` inspection of the live guest disk image showed:
  - `bertmini-batch8.gdbserver.log`: `Listening on port 2345`
  - `bertmini-batch8.gdbserver.info`: `phase=inferior`, `gdbserver_pid=213`, inferior `pid=223`
  - `bertmini-batch8.runner-proc.stage`: inferior is stopped under `gdbserver` with `wchan=ptrace_stop.part.0`

## Failed test

Command:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  192.168.1.44 172.16.0.2:2345 32345
```

Result:

- Exit code: `7`
- Transcript: `tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-expect-20260507T045822Z-192_168_1_44-172_16_0_2/pairdummy-cfg32-triage.expect.log`
- GDB failed at `target remote :32345`:
  - `Remote communication error. Target disconnected: Connection reset by peer.`
- SSH tunnel log:
  - `channel 2: open failed: connect failed: No route to host`

This GDB invocation was the first TCP client attempt for this `gdbserver --once` instance. The guest-side `gdbserver` was not consumed by the failed tunnel attempt; the live guest disk still showed `Listening on port 2345` and no post-connect gdbserver log lines.

## Network evidence

- `ping 172.16.0.2` from the run host failed with `Destination Host Unreachable`.
- `ip neigh show dev tap0` reported `172.16.0.2 FAILED`.
- `tap0` stats after ping/GDB attempts:
  - RX: `0` packets
  - TX: `9` packets
- `switchlog` showed host ARP frames arriving from `tap0` and being sent toward the simulated NIC through `ShmemPort`.
- There was no observed guest response back to `tap0`.

## Root cause found in this run

The guest rootfs did not assign an IPv4 address to the IceNet interface.

Read-only `debugfs` inspection of `/etc/init.d/S40network` in the live guest image showed that the workload-local network script only selects an interface if `/sys/class/net/<ifname>/device` exists. The gdbserver runner has the same filter in `select_gdbserver_net_dev`.

In this run:

- the kernel registered IceNet;
- `S40network` printed `Starting network: OK`;
- but `bertmini-batch8.gdbserver.info` recorded `net_dev=` and `guest_ipv4=`;
- therefore `172.16.0.2/16` was never assigned and host-to-guest TCP could not route.

The immediate fix is to relax the workload-local netdev filter to accept Ethernet interfaces with `type=1` and a nonzero MAC address even when the `device` symlink is absent. This should be applied to both:

- `rerocc-linux-tests-coupleddma/workload/overlay/etc/init.d/S40network`
- `rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh`

Do not change shared `br-base` for this fix; keep it workload-local.
