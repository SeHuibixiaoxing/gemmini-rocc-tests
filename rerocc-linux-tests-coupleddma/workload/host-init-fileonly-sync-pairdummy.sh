#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIPELINE_RUNTIME_SCRIPT_DIR="$(cd "${SCRIPT_DIR}/../../pipeline-runtime/scripts" && pwd)"
fixed_env_script="${PAIRDUMMY_FIXED_ENV_SCRIPT:-${PIPELINE_RUNTIME_SCRIPT_DIR}/pairdummy_sbus128_fixed_env.sh}"

if [[ -n "${PAIRDUMMY_FIXED_ENV_SCRIPT:-}" || -z "${TARGET_KEY:-}" ]]; then
  # shellcheck disable=SC1091
  source "${fixed_env_script}"
fi

exec "${SCRIPT_DIR}/host-init.sh"
