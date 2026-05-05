# cfg32 NIC gdbserver image refresh pass

Timestamp: `2026-05-05T15:27:55Z`

## Context

After the software/runtime checkpoints `336ad52` and `2f74f93`, the previously
closed gdbserver guest image was stale relative to the local
`rerocc_pipeline_runtime-linux` binary. I refreshed the cfg32 NIC gdbserver
FireMarshal image before the new AGFI is available so the later FPGA gdbserver
run will test the current runtime software.

The active bitstream build remains:

- tmux session: `pairdummy-cfg32-nic-mainline-20260505T132956Z`
- target: `WithNIC_WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC4C2x2P12x4x3CoupledDMAPairManagerDummy16x16Sbus128`
- platform: `FRFCFS16GBQuadRank_BaseF2Config`
- strategy/frequency: `TIMING`, `20MHz`
- state at this checkpoint: still running; no new AGFI/AFI yet.

## Commands

```bash
generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh image-closure

generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/pairdummy_sbus128_gdbserver_cfg32_nic_workflow.sh local-freshness
```

## FireMarshal sessions

- clean: `pairdummy-sbus128-gdbserver-cfg32-nic-clean-20260505-152434`
- build: `pairdummy-sbus128-gdbserver-cfg32-nic-build-20260505-152455`
- install: `pairdummy-sbus128-gdbserver-cfg32-nic-install-20260505-152738`

## Freshness result

Result: `PASS`.

Freshness hashes:

- workload-run-entrypoint:
  `ab9fe062aab64432727797750e4281e2ad5a4d614b0b432cb1f176e2270ca659`
- fileonly-wrapper:
  `ab9fe062aab64432727797750e4281e2ad5a4d614b0b432cb1f176e2270ca659`
- runner-script:
  `d9c8761dd5d9246e67a5d56dc36807e7e2d6cba4b004b16cd5af624f4960ff82`
- runtime-binary:
  `302e1c77600d199a3664f122bbbbd172b589f7eb69cc5121d13c243f90d35ddc`
- ptrace-probe:
  `c9a82c2619e49725439022395eef453f7124c04225bfe3a86b5b79eab7ec5015`
- firemarshal-env:
  `a07f85139dc16ee660c0cb1f0a14fa8fe54abcb6984429f45daecfe551a1eace`

The image contains `/usr/bin/gdbserver`.

Key guest env retained:

- `PIPELINE_RUNTIME_GDBSERVER_ENABLE=1`
- `PIPELINE_RUNTIME_GDBSERVER_BIND_ADDR=0.0.0.0`
- `PIPELINE_RUNTIME_GDBSERVER_PORT=2345`
- `PIPELINE_RUNTIME_DMA_FORCE_DIRECT_ENABLE=1`
- `PIPELINE_RUNTIME_DMA_BOUNCE_BYPASS_ENABLE=0`
- `PIPELINE_RUNTIME_BREADCRUMB_ENABLE=1`

## Artifacts

- workload JSON:
  `sims/firesim/deploy/workloads/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.json`
- image:
  `software/firemarshal/images/firechip/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver/rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-gdbserver.img`

## Limitations

This validates only image closure and local image freshness. It does not validate
the active cfg32 NIC bitstream, HWDB, runfarm, guest boot, NIC TCP path, or
remote gdbserver attach. After the bitstream succeeds, HWDB must be updated to
the new AGFI/driver tar and `infrasetup` must be rerun before `runworkload`.
