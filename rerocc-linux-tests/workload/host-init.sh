#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REROCC_TESTS_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
GEMMINI_ROCC_TESTS_DIR="$(cd "${REROCC_TESTS_DIR}/.." && pwd)"
OVERLAY_ROOT_DIR="${SCRIPT_DIR}/overlay/root/rerocc-linux-tests"

echo "Building rerocc-linux-tests binaries"
pushd "${GEMMINI_ROCC_TESTS_DIR}" >/dev/null
autoconf
mkdir -p build
pushd build >/dev/null
../configure
make TARGET=riscv64-unknown-linux-gnu- -j rerocc-linux-tests
popd >/dev/null

mkdir -p "${OVERLAY_ROOT_DIR}"
cp -f build/rerocc-linux-tests/*-linux "${OVERLAY_ROOT_DIR}/"
cp -f rerocc-linux-tests/run_rerocc_lc_linux.sh "${OVERLAY_ROOT_DIR}/"
chmod +x "${OVERLAY_ROOT_DIR}/run_rerocc_lc_linux.sh"
popd >/dev/null
