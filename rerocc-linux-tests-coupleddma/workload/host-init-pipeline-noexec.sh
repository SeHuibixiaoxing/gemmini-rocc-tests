#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

export ENABLE_PIPELINE_RUNTIME=1
export PIPELINE_RUNTIME_ONLY_MARKER=0

exec "${SCRIPT_DIR}/host-init.sh"
