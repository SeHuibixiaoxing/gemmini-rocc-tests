# 20260505T145111Z cfg32 NIC gdbserver image closure pass

## Summary

The `pairdummy-sbus128` cfg32 NIC gdbserver workload image was rebuilt, installed into FireSim, and rechecked for local freshness.
This removes the stale guest-image risk found during the earlier cfg32 gdbserver workflow preflight.

## Target

- Workload JSON: `generators/gemmini/software/gemmini-rocc-tests/rerocc-linux-tests-coupleddma/workload/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- Installed workload: `sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- Image: `software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img`
- Runtime use case: `12p4c128sbus32cfg + optimized DMA + current NIC` FireSim run with guest `gdbserver` enabled.

## Commands

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh image-closure
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh local-freshness
```

The image-closure wrapper ran FireMarshal clean, build, and install through detached tmux sessions:

- Clean session: `pairdummy-sbus128-gdbserver-cfg32-nic-clean-20260505-144654`
- Build session: `pairdummy-sbus128-gdbserver-cfg32-nic-build-20260505-144704`
- Install session: `pairdummy-sbus128-gdbserver-cfg32-nic-install-20260505-145032`

## Evidence

FireMarshal results:

- `tmp/firemarshal-tmux/pairdummy-sbus128-gdbserver-cfg32-nic-clean-20260505-144654.exitcode`: `0`
- `tmp/firemarshal-tmux/pairdummy-sbus128-gdbserver-cfg32-nic-build-20260505-144704.exitcode`: `0`
- `tmp/firemarshal-tmux/pairdummy-sbus128-gdbserver-cfg32-nic-install-20260505-145032.exitcode`: `0`
- Build log: `software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-build-2026-05-05--14-47-13-SUJWQUQGS955VBJ1.log`
- Install log: `software/firemarshal/logs/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver-install-2026-05-05--14-50-35-VP22A4B15WEWSSX5.log`

`local-freshness` result:

```text
[image-freshness] OK workload-run-entrypoint sha256=ab9fe062aab64432727797750e4281e2ad5a4d614b0b432cb1f176e2270ca659
[image-freshness] OK fileonly-wrapper sha256=ab9fe062aab64432727797750e4281e2ad5a4d614b0b432cb1f176e2270ca659
[image-freshness] OK runner-script sha256=d9c8761dd5d9246e67a5d56dc36807e7e2d6cba4b004b16cd5af624f4960ff82
[image-freshness] OK runtime-binary sha256=b6983f933ebfe1b6fb38be255a403ae8a07e272832f06ca0af15d913a9a2e5d6
[image-freshness] OK ptrace-probe sha256=c9a82c2619e49725439022395eef453f7124c04225bfe3a86b5b79eab7ec5015
[image-freshness] OK firemarshal-env sha256=a07f85139dc16ee660c0cb1f0a14fa8fe54abcb6984429f45daecfe551a1eace
[image-freshness] OK gdbserver present in image
[image-freshness] PASS workload=rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver image=/home/ubuntu/chipyard/software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img
```

Effective guest environment rendered by the workflow:

```text
PIPELINE_RUNTIME_PROFILE_ID=pairdummy-sbus128-fixed-v25
PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1
PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=0
PIPELINE_RUNTIME_GDBSERVER_ENABLE=1
PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR=0.0.0.0
PIPELINE_RUNTIME_GDBSERVER_PORT=2345
```

## Interpretation

The guest image is now aligned with the current local runtime binary, runner scripts, ptrace probe, and gdbserver environment.
The previous preflight blocker was an image packaging problem, not a FireSim hardware or NIC problem.

## Limitations and Next Steps

- This checkpoint validates image contents only. It does not validate the new cfg32 NIC AGFI, because the `pairdummy-cfg32-nic-mainline-20260505T132956Z` bitstream build is still in the local GoldenGate phase.
- After the new AGFI is available, update the cfg32 NIC HWDB, rerun `infrasetup`, launch the workload, and attach with a real RISC-V GDB as the first TCP client to `gdbserver`.
- Do not use `nc` or telnet against `gdbserver --once`; doing so can consume the one allowed connection before GDB attaches.
