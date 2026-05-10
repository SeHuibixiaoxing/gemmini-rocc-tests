#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The 1C1P hardware-debug UART probe exercises the small CoupledDMA export
# alias binary, not the BERT pipeline runtime. Keep FireMarshal image creation
# independent of whichever HybridMapper artifacts happen to exist locally.
export ENABLE_PIPELINE_RUNTIME="${ENABLE_PIPELINE_RUNTIME:-0}"

exec "${SCRIPT_DIR}/host-init.sh" "$@"
