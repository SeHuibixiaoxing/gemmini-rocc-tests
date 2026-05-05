# old AGFI GDB invalid-hex checkpoint

Date: 2026-05-05

The current old-AGFI control no longer fails at Linux boot, IceNet bring-up,
`gdbserver` prelaunch, or TCP handshake. It fails after GDB receives a
payload-bearing RSP packet whose IPv4/TCP checksums have been repaired but
whose bytes are already corrupted.

Known-good target:

- AGFI: `agfi-0079cbbca617eca4e`
- AFI: `afi-0ee7774f829acd4de`
- Original FireSim commit in AGFI metadata: `9920b2919337411b8ade6d0c2e1f836666422504`
- Original config: `FireSimRocketNICNoTraceConfig + BaseF2Config`

Current failing control:

- FireSim switch commit: `a300ebdfe`
- Runtime config:
  `config_runtime_f2_rocket_singlecore_nic_gdbserver_smoke_notrace_clean1bp_gdbpayload_30mhz.yaml`
- HWDB:
  `config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz_agfi0079_emptydriver_01e763.yaml`
- Expect result: `expect_rc=5`
- GDB symptom: `Invalid hex digit 59`, then `The program is not being run`
- Artifact:
  `pipeline-runtime/debug_records/artifacts/20260505T015410Z_oldagfi_gdbpayload_invalidhex/`

Key conclusion:

The payload checksum repair is not the root fix. It lets the host accept a
target-to-host TCP packet that was previously dropped, but the packet already
contains corrupted RSP text at `ShmemPort recv` time. The visible packet starts
with `$PackEtSize=47ff` instead of `$PacketSize=47ff` and contains many
single-byte corruptions across the 562-byte response.

Next low-cost software experiment:

Restore `ShmemPort::send()` empty-marker clearing while keeping the current
ARP/TCP checksum repair. Commit `a3cc758c6` reintroduced marker preservation,
but the old AGFI metadata points at FireSim `9920b291...`, whose switch source
cleared `0xDEADBEEFDEADBEEF` before publishing a full shared-memory buffer.
This test does not require a new bitstream; it only requires `infrasetup` to
rebuild/redeploy the switch.

Update after FireSim `b17cc8979`:

The marker-clearing control was tested on a fresh F2 with the same old AGFI and
the same `01e763` driver bundle. It still failed with `Invalid hex digit 59`;
pcap/switchlog again showed `$PackEtSize=47ff` in the payload-bearing RSP
packet. Marker preservation is therefore not the root cause.

Next low-cost control:

Use the recovered original old-AGFI driver bundle instead of the `01e763`
bundle:

```text
sims/firesim/deploy/config_hwdb_f2_rocket_singlecore_nic_notrace_30mhz_agfi0079_recovered_driver.yaml
```

The recovered bundle's `FireSim-f2` sha256 is
`dc94d9f93b34dfc32dd994d8926d6cd81cf325003a8b2ba551eb9b3ff8d05a92`, matching
the driver binary in the original 2026-04-30 old AGFI build result.
