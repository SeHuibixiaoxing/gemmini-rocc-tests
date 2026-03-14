# Next Session Prompt

```text
You are continuing work from the AWS side for:
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
- current handoff mode: local host/metasim/bitstream prep is done, AWS manager should continue the FPGA replay

Current rules:
- pipeline-runtime is the Gemmini pipeline software stack; MudnacSim is reference only
- use /home/wzy/proj/wp2/chipyard/tmp/HybridMapper as the artifact root
- keep shared entire_model YAML schema with MudnacSim; only layer mapping is Gemmini-specific
- size mismatch rule is prefix crop + zero tail pad everywhere
- do not reintroduce non-globalnoc as a main path

Local results already confirmed:
- host bertmini closure PASS for ours2/gemini2/tangram2
- baremetal globalnoc metasim suite PASS for matrix/coverage/nonblocking
- local buildbitstream PASS
  - log: /home/wzy/proj/wp2/chipyard/sims/firesim/deploy/logs/2026-03-12--16-55-53-buildbitstream-2NLSKFJM4VHB1EFW.log
  - hwdb entry: /home/wzy/proj/wp2/chipyard/sims/firesim/deploy/built-hwdb-entries/alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10
  - tarball: /home/wzy/proj/wp2/chipyard/sims/firesim/deploy/results-build/2026-03-12--16-55-53-alveo_u280_firesim_rerocc_lc_small_globalnoc_coupleddma_frequency_10/cl_xilinx_alveo_u280-firesim-FireSim-WithDefaultFireSimBridges_WithFireSimConfigTweaks_chipyard.GemminiLearningConfigSpadReRoCCGlobalNoC2C1x2G2x1x2D2x1x2CoupledDMA-FRFCFS16GBQuadRank_BaseXilinxAlveoU280Config/firesim.tar.gz

Critical warning:
- the build log records firesim-commit:e0237bab695ef4305fc307b0cdd20a71adfa8055-dirty
- do not assume the latest clean commit is enough; AWS must sync the same dirty worktree or regenerate all derived artifacts
- the built hwdb entry currently uses a local file:///home/wzy/... path, which is not directly usable on AWS

First actions on AWS:
1. bootstrap FireSim manager environment in the same shell:
   source ~/.ssh/AGENT_VARS
   FIRESIM_DIR=/home/wzy/proj/wp2/chipyard/sims/firesim
   cd "${FIRESIM_DIR}"
   source ./sourceme-manager.sh --skip-ssh-setup
2. verify the AWS checkout matches the local dirty tree used for the successful bitstream build
3. choose one bitstream path:
   - copy the local firesim.tar.gz + built-hwdb entry to AWS and rewrite bitstream_tar to an AWS-visible file:/// path
   - or rerun firesim buildbitstream on AWS with the same build config
4. confirm config_hwdb.yaml points at the AWS-visible hwdb entry
5. run:
   firesim infrasetup -c config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml -a config_hwdb.yaml -r config_build_recipes.yaml
   firesim runworkload -c config_runtime_rerocc_fpga_small_linux_globalnoc_coupleddma.yaml -a config_hwdb.yaml -r config_build_recipes.yaml

Validation policy:
- if runtime logic changes, rerun host bertmini closure for ours2/gemini2/tangram2
- if Linux packaging changes, rerun host-init script checks and target wrapper compile checks
- if FireSim startup path changes, rerun globalnoc metasim smoke
- if AWS FPGA replay changes hwdb/build/runtime wiring, record exact log paths and whether the bitstream was reused or rebuilt
```
