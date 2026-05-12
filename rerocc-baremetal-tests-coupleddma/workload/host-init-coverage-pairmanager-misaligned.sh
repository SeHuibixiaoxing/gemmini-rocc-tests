#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export REROCC_TARGET="${REROCC_TARGET:-small}"
export NUM_CORES="${NUM_CORES:-2}"
export NUM_GEMMINI="${NUM_GEMMINI:-2}"
export NUM_DMA="${NUM_DMA:-2}"
export GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
export DMA_BASE_ID="${DMA_BASE_ID:-0}"
export LOCAL_GEMMINI_ID="${LOCAL_GEMMINI_ID:-0}"
export BYTES="${BYTES:-384}"
export PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-1}"
export DMA_MISALIGNED_ENABLE="${DMA_MISALIGNED_ENABLE:-1}"
export COVERAGE_OUTPUT_BASENAME="${COVERAGE_OUTPUT_BASENAME:-rerocc_lc_coverage_baremetal_coupleddma_pairmanager_misaligned.riscv}"
export COVERAGE_OUTPUT_TAG="${COVERAGE_OUTPUT_TAG:-coverage-pairmanager-misaligned}"

"${SCRIPT_DIR}/host-init-coverage.sh" "$@"
