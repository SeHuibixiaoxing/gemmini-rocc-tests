# 20260505T181253Z - Stage resource contract microtest while cfg32 builds run

## Goal

Continue software/static triage while the two `12p4c128sbus32cfg + current NIC`
F2 bitstream builds are still running. This checkpoint focuses on the user's
resource-conflict questions:

- whether a segment's stages can over-subscribe the 12 Gemmini/DMA manager
  target if stage workers are considered together;
- whether explicit physical manager lists contain duplicate manager ids;
- whether the current segment/stage count exceeds the ReRoCC cfg budget after
  reserving cfg 31 for SPM xlate;
- whether the current host build and basic artifact/runtime validation still
  pass after the latest docs and guardrail work.

This is a software/static contract test only. It does not prove live DMA,
Gemmini, SPM xlate, NIC, or F2 timing behavior.

## Commands

From `/home/ubuntu/chipyard`:

```sh
make -C generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean all

PRT=generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime
ROOT=conference/HybridMapper/output/pipeline_runtime/bertmini
TARGET=rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128
for method in ours2 gemini2 tangram2; do
  .conda-env/bin/python "$PRT/scripts/audit_pipeline_runtime_artifact.py" \
    --pipeline-yaml "$ROOT/pipeline_mapping.${TARGET}.${method}.yaml" \
    --hardware-yaml "$ROOT/hardware_target.${TARGET}.yaml" \
    --model-yaml "$ROOT/model.layers.yaml" \
    --expect-target-key "$TARGET" \
    --page-size-bytes 1024
  "$PRT/pipeline_runtime" \
    --hw-validate-only \
    --backend cpu \
    --num-cores 4 \
    --num-gemmini-mgrs 12 \
    --num-dma-mgrs 12 \
    --pair-manager-mode 1 \
    --pages-per-acc 1024 \
    --spm-page-bytes 1024 \
    --spm-xlate-enable 1
done
```

Additional inline Python YAML audit:

```sh
.conda-env/bin/python - <<'PY'
from pathlib import Path
import yaml
root = Path("conference/HybridMapper/output/pipeline_runtime/bertmini")
target = "rerocc_globalnoc_pairmanager_dummy16x16_c4_g12_d12_spad1024kb_dram19_noc64_mac256_sbus128"
for method in ["ours2", "gemini2", "tangram2"]:
    doc = yaml.safe_load((root / f"pipeline_mapping.{target}.{method}.yaml").read_text())
    for seg in doc["segments"]:
        stages = []
        for group in seg.get("stages", []) or []:
            stages.extend(group if isinstance(group, list) else [group])
        utils = [int(st.get("accUtil", 0) or 0) for st in stages]
        pacc = []
        for st in stages:
            raw = st.get("pAccIdxList", []) or []
            if len(raw) == 1 and isinstance(raw[0], list):
                raw = raw[0]
            pacc.extend(int(v) for v in raw)
        assert sum(utils) <= 12
        assert len(pacc) == len(set(pacc))
        assert len(stages) <= 15
        assert int(seg.get("segmentSpmPageSpan", 0) or 0) <= 12 * 1024
PY
```

## Results

Host build:

- `make clean all`: pass.
- The previous temporary host-build warning in `src/prt_dma.c` is not present in
  this state.

Artifact/runtime contract validation:

- `ours2`: artifact audit pass, `HW_VALIDATE_ONLY_PASS`.
- `gemini2`: artifact audit pass, `HW_VALIDATE_ONLY_PASS`.
- `tangram2`: artifact audit pass, `HW_VALIDATE_ONLY_PASS`.

Stage resource summary:

- `ours2`: 13 segments, `max_util_sum=12`, `max_segment_pages=2060`.
- `gemini2`: 15 segments, `max_util_sum=12`, `max_segment_pages=1734`.
- `tangram2`: 12 segments, `max_util_sum=12`, `max_segment_pages=2061`.
- No segment exceeds the 12-manager target.
- No explicit physical manager duplicate was found.
- No segment exceeds the current RR stage cfg budget of 15 ordinary stages with
  cfg 31 reserved for SPM xlate.
- No segment SPM page span exceeds `12 * 1024` pages.

## Interpretation

For the current `bertmini` artifacts, the mapper/runtime contract does not show
an obvious segment-level manager over-subscription bug. If the F2 run later
hangs in a stage worker, the first suspicion should not be "the artifact needs
more than 12 managers"; it should be narrowed with GDB to one of:

- DMA fence or completion;
- SPM xlate program/flush/release;
- Gemmini fence;
- pipe/ring wait;
- stage-local DMA/Gemmini same-manager overlap semantics;
- hardware/NIC transport.

The unresolved design gap remains: this static test proves that the segment
manager sum fits, but it does not yet prove that every dynamic DMA/Gemmini issue
uses non-conflicting manager/page sets under future overlap modes. Current
`spm_xlate_enable=1` still keeps the runtime on the blocking debug path, which is
the right first target for the new gdbserver-capable bitstream.

## Concurrent bitstream state

At the time of this checkpoint:

- `pairdummy-cfg32-nic-mainline-20260505T132956Z` is alive on build host
  `i-0479b74dd4de8e428` / `192.168.0.60`. Vivado has completed a large timing
  optimization pass for synthesis but has not yet reached placement or AGFI/AFI
  generation.
- `pairdummy-cfg32-nic-notrace-20260505T171453Z` is alive in local
  `GoldenGateMain`; `FireSim-generated.sv` has not appeared yet.
