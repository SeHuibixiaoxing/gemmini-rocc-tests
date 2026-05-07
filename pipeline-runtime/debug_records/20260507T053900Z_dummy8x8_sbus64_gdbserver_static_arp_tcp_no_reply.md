# 20260507T053900Z - dummy8x8 sbus64 cfg32 NIC static-ARP GDB no TCP reply

## Context

This record follows `20260507T053200Z_dummy8x8_sbus64_gdbserver_arp_no_reply.md`.

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Target: `firesim_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_cfg32_nic_notrace`
- Run host: `192.168.1.79`
- Instance: `i-0f44087ecacb06179`
- Guest endpoint printed by workload: `172.16.0.2:2345`
- gdbserver state before this attempt:
  - `net_dev=eth0`
  - `guest_ipv4=172.16.0.2`
  - `gdbserver_pid=221`
  - `pid=235`
  - `Listening on port 2345`

## Purpose

The first GDB attempt failed with `No route to host` because the run host could
not resolve `172.16.0.2` on `tap0`. This follow-up forced a permanent neighbor
entry so the test could distinguish:

- ARP-only failure, where TCP would work once the neighbor entry is supplied;
- a deeper host-to-target or target-to-host NIC data-path failure.

## Commands

On the run host:

```sh
sudo ip neigh replace 172.16.0.2 lladdr 00:12:6d:00:00:02 nud permanent dev tap0
sudo timeout 180 tcpdump -n -i tap0 -s 0 \
  -w /home/ubuntu/sim_slot_0/gdb-staticarp-20260507T0539Z.pcap \
  host 172.16.0.2
```

Host GDB attempt:

```sh
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_pairdummy_cfg32_gdbserver_expect_triage.sh \
  192.168.1.79 172.16.0.2:2345 32346
```

Transcript:

`tmp/firesim-aws-f2/gdbserver-tests/pairdummy-cfg32-expect-20260507T053108Z-192_168_1_79-172_16_0_2/pairdummy-cfg32-triage.expect.log`

## Result

GDB again failed at `target remote`:

```text
Remote debugging using :32346
Remote communication error.  Target disconnected: Connection reset by peer.
```

The SSH tunnel no longer failed immediately with `No route to host`; it timed
out connecting to the guest endpoint:

```text
channel 2: open failed: connect failed: Connection timed out
```

## Packet evidence

The tcpdump capture contained only outbound SYN retransmissions:

```text
172.16.0.1.44918 > 172.16.0.2.2345: Flags [S]
...
```

There were 11 captured packets, all host-to-target SYNs. No SYN-ACK, RST, ARP
reply, ICMP, or any other target-to-host packet appeared on `tap0`.

tap0 counters after the attempt:

- RX: `0 packets`
- TX: `17 packets`

The switch log showed the same host-to-target SYN frames being read from tap0
and sent into the shmem port:

```text
SWITCH DEBUG SSHPort tap_recv port=1 event=8 tap_len=74 bytes=00 12 6d 00 00 02 ...
SWITCH DEBUG ShmemPort send port=0 event=8 ... valid_flits=10 last_flits=1 ...
```

The guest-side `gdbserver.log` still contained only startup lines:

```text
Process /root/rerocc-linux-tests/pipeline-runtime/rerocc_pipeline_runtime-linux created; pid = 235
Listening on port 2345
```

It did not print `Remote debugging from host ...`, so the SYN never established
a TCP session with gdbserver.

## Interpretation

Static ARP changes the failure from immediate host routing failure to TCP SYN
timeout, but it does not make any packet reach gdbserver. Therefore the current
blocker is deeper than ARP payload generation.

The live evidence is consistent with:

- host tap/switch can emit ARP and TCP frames toward shmem;
- the target either does not receive those frames through the current NIC path,
  or receives them but cannot transmit replies back to host;
- gdbserver userspace is not the failing component.

Given the old single-core Rocket + NIC AGFI passed with a recovered 2026-04-30
driver bundle, this should be treated as a current bitstream/driver/switch
integration regression, not as a generic "FireSim NIC cannot support gdbserver"
issue.
