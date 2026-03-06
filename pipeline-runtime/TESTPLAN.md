# Pipeline Runtime V1 Test Plan

## Test Matrix by Buffer Category

| Test ID | Categories Covered | Scenario | Expected Result |
|---|---|---|---|
| TB1 | C1,C2 | Entry/Export DRAM single path | No deadlock, correct fetch/flush transitions |
| TB2 | C1,C2 + ring | DRAM_DEPEN with DRAM ring queue | correct ring head/tail and offset gating |
| TB3 | C3 | Isolate no-ring one producer one consumer | send command pairing and counter updates match |
| TB4 | C3 | Isolate no-ring one producer two consumers | producer clear only after both successors consume |
| TB5 | C4 | Shared no-ring double-buffer | tag toggles and full/empty handshake stable |
| TB6 | C5,C6 | Isolate with SPM ring under pressure | no overwrite, no underflow, offsets monotonic |
| TB7 | C7,C8 | All-ring direct bind mode | no explicit DMA, ring tag transitions correct |
| TB8 | C1..C8 mixed | Combined segment with all buffer modes | global loop stable, no state corruption |
| TB9 | C1..C8 mixed | 100 batches stress | no deadlock, no page leaks, watchdog clean |

## Hardware-First Gate Sequence (Locked)

1. Gate-1 baremetal metasim:
   - `config_runtime_rerocc_metasim_small_baremetal_globalnoc_coupleddma.yaml`
2. Gate-2 baremetal FPGA:
   - `config_runtime_rerocc_fpga_small_baremetal_globalnoc_coupleddma.yaml`
3. Gate-3 Linux FPGA:
   - `config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml`

Automation script:

```bash
/home/wzy/proj/wp2/chipyard/sims/firesim/deploy/run_rerocc_coupleddma_validation_gates.sh
```

Gate success criterion:
- `ALL_TESTS_PASS` must appear in each gate's `uartlog`.
- gate script now does this by default (`GATE_RUNS=5`); override by setting `GATE_RUNS=<n>`.

## CoupledDMA Hardware Microbench Expectations

1. Baremetal matrix workload (`conv + resadd + DMA`) should pass with cross-page DMA payload:
   - marshal profile: `marshal-config/rerocc_lc_baremetal_coupleddma_quick.json`
   - default bytes: `65536`.
2. Linux matrix workload should pass with:
   - Gemmini matrix (`conv + resadd`) and
   - coupled-DMA matrix using software page-walk and per-page submit.
   - marshal profile: `marshal-config/rerocc_lc_linux_coupleddma_quick.json`
   - default bytes: `65536`.
3. Quick preflight of runtime stack is available via:
   - `pipeline_runtime --hw-validate-only`
   - expected stdout: `HW_VALIDATE_ONLY_PASS`.

## Command Completion Validation

- Every issued fetch/flush/send command must decrement exactly one expected counter path.
- C3 send decrements both source and destination counters.
- C4 and C7/C8 do not issue explicit transfer commands.

## Ring Buffer Validation

- Check invariant: `tail - head <= size`.
- Check `use_count` decrements to zero before head advances.
- Validate out-degree semantics in fan-out.

## Page Table Validation

- Temp mappings created before command issue and invalidated on completion.
- No stale vpage mapping remains after pipeline completion.

## End-to-End

- Compare final output with golden:
  - INT8/INT16 exact
  - FP `atol=1e-3`, `rtol=1e-3`

## Real-Data Smoke (Current Baseline)

1. Generate dummy artifacts (HybridMapper):

```bash
python3 /home/wzy/proj/HybridMapper/scripts/create-pipeline-dummy-runtime-data.py \
  --model bertmini \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml
```

2. Run runtime with 8-core mapping:

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000
```

Expected: process exits with `RC=0`.

3. Negative e2e compare test (tampered golden):

```bash
cp /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin /tmp/golden_bad.bin
orig=$(od -An -tu1 -N1 /tmp/golden_bad.bin | tr -d ' ')
new=$(( (orig + 1) % 256 ))
printf "\\$(printf '%03o' "$new")" | dd of=/tmp/golden_bad.bin bs=1 seek=0 count=1 conv=notrunc status=none

/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /tmp/golden_bad.bin \
  --batch 1 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000
```

Expected: non-zero exit code and printed mismatch summary.

4. Watchdog deadlock-guard test (timeout must fail):

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --batch 4294967295 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 1
```

Expected: non-zero exit code with `timeout`.

5. Operator coverage sanity:

- Use a model containing both `conv` and `resadd` (e.g. bertmini) and confirm runtime exits cleanly under the same command set above.

6. 100-batch stress (no deadlock/no page leak):

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --batch 100 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000
```

Expected: `RC=0`, no allocator-leak report.

7. DMA poll-progress backend smoke:

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000 \
  --spm-xlate-enable 0 \
  --dma-backend poll_progress_thread
```

Expected: `RC=0`.

Note: with `--spm-xlate-enable 1`, runtime currently forces blocking-debug mode until async per-page retire support lands.

## ReRoCC + CoupledDMA Track Smoke (2026-03-05)

Use manager-aware CLI fields and explicit sync mode:

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  --model-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/layers.yaml \
  --model-bin /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_weight/model.bin \
  --model-offset 0 \
  --pipeline-yaml /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/entire_model/8_256_16_19_64_ours2.yaml \
  --input /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/input.bin \
  --golden /home/wzy/proj/wp2/chipyard/tmp/HybridMapper/output/pipeline/bertmini/dummy_input/golden/golden.bin \
  --batch 1 \
  --num-cores 8 \
  --num-gemmini-mgrs 8 \
  --num-dma-mgrs 8 \
  --gemmini-base-id 0 \
  --dma-base-id 8 \
  --pages-per-acc 4096 \
  --watchdog-ms 5000
```

Expected: `RC=0`.

Blocking debug fallback:

```bash
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime \
  ... same args ... \
  --sync-mode blocking_debug
```

Expected: `RC=0`.

## ReRoCC split/fence regression additions (2026-03-05)

1. Manager-aware mode sanity:
- with `--spm-xlate-enable 1`, run default command and verify blocking-debug fallback still returns `RC=0`.
- with `--spm-xlate-enable 0`, run async command and verify `RC=0`.

2. Split error path:
- construct/choose a layer where `accUtil > output_domain_elems` for spatial fallback.
- expected: runtime returns non-zero with split error (no silent single-core downgrade).

3. Dependency-boundary fence observability:
- run async mode with `--trace /tmp/prt_trace_async_rerocc.txt`.
- expected:
  - `gemm_issue_count > 0`
  - `gemm_fence_count > 0`
  - `overlap_est_ns > 0` on multi-batch overlap workload.

4. Dual paging translation validation (new frozen architecture):
- shared-spad dedicated DMA path:
  - verify software-side page-walk emits per-page commands over 1KB shared-spad pages.
- Gemmini load/store DMA path:
  - verify front-end dual-path translation:
    - DRAM accesses follow OS/TLB mapping;
    - shared-spad accesses follow runtime-managed shared-spad page table.
- residency invariant:
  - each tensor must be entirely in DRAM or entirely in shared-spad.
