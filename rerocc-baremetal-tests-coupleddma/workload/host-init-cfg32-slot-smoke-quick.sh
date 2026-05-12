#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export CFG32_SLOT_BOUNDARY_SWEEP="${CFG32_SLOT_BOUNDARY_SWEEP:-0}"
export CFG32_SLOT_OUTPUT_BASENAME="${CFG32_SLOT_OUTPUT_BASENAME:-rerocc_lc_cfg32_slot_smoke_quick.riscv}"
export CFG32_SLOT_OUTPUT_TAG="${CFG32_SLOT_OUTPUT_TAG:-cfg32-slot-smoke-quick}"
export CFG32_SLOT_TRACERV_MARKERS="${CFG32_SLOT_TRACERV_MARKERS:-1}"

"${SCRIPT_DIR}/host-init-cfg32-slot-smoke.sh" "$@"
