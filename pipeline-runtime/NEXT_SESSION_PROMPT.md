# Next Session Prompt

Use this as the first message in a new Codex session:

```text
You are continuing an in-progress implementation across two local repos:

1) Chipyard runtime repo:
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime

2) HybridMapper repo:
/home/wzy/proj/HybridMapper

First read these docs completely:
- /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/HANDOFF.md
- /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/IMPLEMENTATION_STATUS.md
- /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/ARCHITECTURE.md

Then verify the current baseline with these commands:

python3 /home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py \
  --model bertmini \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml

make -C /home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j4

/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 500

Current status assumptions to preserve:
- layer address is model-relative offset (with addr_base compatibility fallback).
- runtime threads are blocking (no busy-wait in stage logic).
- parser already supports stage blocks where accUtil appears before globalStageId.
- parser now handles layers where address/address2 appear before index.
- runtime already supports strict --input/--golden e2e compare and mismatch return code.
- runtime already supports batch-complete termination (`--batch`) with watchdog timeout as deadlock guard.
- runtime operator dispatch already includes `conv` and `resadd` descriptor paths.
- generated golden is pseudo CPU reference for flow validation (not Gemmini bit-exact).

Your first task:
Implement multi-segment runtime execution support:
1) remove current `pipeline.segments[0]` limitation in topology build/run path,
2) make completion sink detection prefer model outputs without needing terminal-export fallback warnings,
3) preserve existing strict --input/--golden behavior and batch-complete stop semantics,
4) update README + HANDOFF.md + TESTPLAN.md with runnable verification commands.

Keep changes minimal and deterministic, update README + HANDOFF.md + TESTPLAN.md after code changes, and provide runnable commands for verification.
```
