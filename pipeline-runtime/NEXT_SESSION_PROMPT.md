# Next Session Prompt

```text
You are continuing work in:
/home/wzy/proj/wp2/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime

Read these first:
- README.md
- HANDOFF.md
- TESTPLAN.md
- DECISIONS.md

Current objective:
- bertmini end-to-end closure on globalnoc Linux
- main target config: GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA
- final FireSim runtime config: sims/firesim/deploy/config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml

Current rules:
- pipeline-runtime is the Gemmini pipeline software stack; MudnacSim is reference only
- use /home/wzy/proj/wp2/chipyard/tmp/HybridMapper as the artifact root
- keep shared entire_model YAML schema with MudnacSim; only layer mapping is Gemmini-specific
- size mismatch rule is prefix crop + zero tail pad everywhere
- do not reintroduce non-globalnoc as a main path

Validation policy:
- if runtime logic changes, rerun host bertmini closure for ours2/gemini2/tangram2
- if Linux packaging changes, rerun host-init script checks and target wrapper compile checks
- if FireSim startup path changes, rerun globalnoc metasim smoke
- FPGA replay stays deferred until hardware and toolchain are available
```
