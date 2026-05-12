#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export REROCC_TARGET="${REROCC_TARGET:-small}"
export NUM_CORES="${NUM_CORES:-2}"
export NUM_GEMMINI="${NUM_GEMMINI:-2}"
export NUM_DMA="${NUM_DMA:-2}"
export GEMMINI_BASE_ID="${GEMMINI_BASE_ID:-0}"
export DMA_BASE_ID="${DMA_BASE_ID:-0}"
export BYTES="${BYTES:-256}"
export LONG_CONV_ITERS="${LONG_CONV_ITERS:-1}"
export SHORT_CONV_ITERS="${SHORT_CONV_ITERS:-1}"
export LONG_RESADD_ITERS="${LONG_RESADD_ITERS:-1}"
export LONG_DMA_ITERS="${LONG_DMA_ITERS:-1}"
export SHORT_DMA_ITERS="${SHORT_DMA_ITERS:-1}"
export PAIR_MANAGER_MODE="${PAIR_MANAGER_MODE:-1}"
export DMA_MISALIGNED_PROFILE="${DMA_MISALIGNED_PROFILE:-1}"
export NONBLOCKING_OUTPUT_BASENAME="${NONBLOCKING_OUTPUT_BASENAME:-rerocc_lc_nonblocking_baremetal_coupleddma_pairmanager_misaligned.riscv}"
export NONBLOCKING_OUTPUT_TAG="${NONBLOCKING_OUTPUT_TAG:-nonblocking-pairmanager-misaligned}"

"${SCRIPT_DIR}/host-init-nonblocking.sh" "$@"
