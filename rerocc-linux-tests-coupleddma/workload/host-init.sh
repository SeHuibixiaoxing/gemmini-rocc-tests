#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_TESTS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_TESTS_DIR}/.." && pwd)"
OVERLAY_ROOT_DIR="${SCRIPT_DIR}/overlay/root/rerocc-linux-tests-coupleddma"

CC_LINUX="${CC_LINUX:-riscv64-unknown-linux-gnu-gcc}"

echo "Building rerocc-linux-tests binaries for coupled DMA verification"
pushd "${GEMMINI_ROCC_TESTS_DIR}" >/dev/null
autoconf
mkdir -p build
pushd build >/dev/null
../configure
make TARGET=riscv64-unknown-linux-gnu- -j rerocc-linux-tests
popd >/dev/null

mkdir -p "${OVERLAY_ROOT_DIR}"
cp -f build/rerocc-linux-tests/rerocc_gemmini_conv_matrix-linux "${OVERLAY_ROOT_DIR}/"
cp -f build/rerocc-linux-tests/rerocc_dma_matrix-linux "${OVERLAY_ROOT_DIR}/"

"${CC_LINUX}" \
  -mcmodel=medany \
  -std=gnu99 \
  -O2 \
  -pthread \
  -march=rv64gc -Wa,-march=rv64gc \
  -ffast-math \
  -fno-common \
  -fno-tree-loop-distribute-patterns \
  -I"${GEMMINI_ROCC_TESTS_DIR}" \
  -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests" \
  -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests/env" \
  -I"${GEMMINI_ROCC_TESTS_DIR}/riscv-tests-benchmarks-common" \
  -I"${GEMMINI_ROCC_TESTS_DIR}/rerocc-linux-tests" \
  "${REROCC_TESTS_DIR}/rerocc_dma_matrix_linux_coupleddma.c" \
  -o "${OVERLAY_ROOT_DIR}/rerocc_dma_matrix_coupleddma-linux"

"${CC_LINUX}" \
  -mcmodel=medany \
  -std=gnu99 \
  -O2 \
  -march=rv64gc -Wa,-march=rv64gc \
  -ffast-math \
  -fno-common \
  -fno-tree-loop-distribute-patterns \
  "${REROCC_TESTS_DIR}/rerocc_lc_matrix_linux_coupleddma_verify.c" \
  -o "${OVERLAY_ROOT_DIR}/rerocc_lc_matrix_linux_coupleddma_verify-linux"

chmod +x "${OVERLAY_ROOT_DIR}/rerocc_lc_matrix_linux_coupleddma_verify-linux"
popd >/dev/null
