#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck disable=SC1091
source "${SCRIPT_DIR}/../../pipeline-runtime/scripts/pairdummy_sbus64_dummy8x8_g6_fixed_env.sh"

exec "${SCRIPT_DIR}/host-init.sh"
