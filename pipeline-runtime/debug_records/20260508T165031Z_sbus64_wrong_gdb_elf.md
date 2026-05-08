# 2026-05-08T16:50Z sbus64 wrong GDB ELF

## Context

- AGFI: `agfi-077451484fe3b63c3`
- AFI: `afi-07989ce9ce725a690`
- Hardware: dummy Gemmini 8x8, `4c12p12`, `sbus64`, cfg32, NIC, no TraceIO
- Runtime config: `sims/firesim/deploy/config_runtime_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_linux_bertmini_pipeline_runtime_batch8_fileonly_sync_gdbserver_cfg32_nic_notrace.yaml`
- HWDB: `sims/firesim/deploy/config_hwdb_f2_gemmini_rerocc_pairmanager_dummy8x8_4c12p12_sbus64_bertmini_cfg32_nic_notrace.yaml`
- Workload: `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver`
- Profile: `pairdummy-sbus64-dummy8x8-fixed-v5-gdbonly`
- Run host: `192.168.2.159`
- FireSim result directory: `sims/firesim/deploy/results-workload/2026-05-08--16-33-24-rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-f2-gemmini-rerocc-pairmanager-dummy8x8-4c12p12-sbus64-linux-bertmini-batch8-fileonly-sync-gdbserver-cfg32-nic-notrace`
- FireSim run log: `sims/firesim/deploy/logs/2026-05-08--16-33-24-runworkload-X42YDUFW82F8VD5U.log`

## What Went Wrong

GDB was launched with:

```sh
.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

That ELF is not the binary packaged into the FireMarshal image.

The two local binaries differ:

```text
1cdd2913cd976df87c2061e96fae811a198349c16469da2357cebe92f93aa0f6  rerocc-linux-tests/rerocc_pipeline_runtime-linux
da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1  build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

The image freshness check for this workload reported the runtime binary SHA256 as `da4856f6fc4d30cdf49674c7ad41de5c8f818fadfc77ad850524d24c933319f1`. Therefore the correct GDB ELF is:

```text
generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

## Evidence

With the wrong ELF loaded, GDB claimed:

```text
Symbol "main" is a function at address 0x11f70.
```

But the live target disassembly at `0x11f70` was a PLT-like stub:

```text
0x0000000000011f70 <main+0>: auipc t3,0x46
0x0000000000011f74 <main+4>: ld    t3,624(t3)
0x0000000000011f78 <main+8>: jalr  t1,t3
```

The expected local source build has `main` as a direct jump to `prt_main_entry`; that was not what the target executed. The GDB stack also showed impossible frames and parameters, for example `main(argc=3, argv=...)` where the register values matched a `pread(fd=3, count=8, offset=752)` call, and frames such as `prt_spm_fault_count -> main`.

This invalidates the source-line breakpoints, stacks, and line-number conclusions from this run and likely explains the previous confusing `main`/`pread64` observations.

## Outcome

- This run is invalid as runtime frontier evidence.
- It does prove that `gdbserver` still reaches `listening` and that GDB can attach.
- The run farm was terminated afterward. The cluster tag `pairbertb8d12s64gdbcfg32nicnt` had no pending/running/stopping/stopped instances after termination.
- Stale local `firesim runworkload` processes for the terminated run were killed.

## Required Constraint For Next Runs

Always launch GDB with the ELF whose SHA matches the runtime binary inside the image:

```sh
.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb -q \
  generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux
```

Before trusting breakpoints, verify:

```gdb
info files
info address main
x/8i main
```

The `main` disassembly must match the correct ELF and must not look like an unrelated PLT stub for the packaged binary.
