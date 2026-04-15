#!/bin/sh
set -eu

GUEST_WRAPPER="/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini_fileonly_sync_capture.sh"
GUEST_RUNNER="/root/rerocc-linux-tests/run_rerocc_pipeline_runtime_bertmini.sh"

if [ -x "${GUEST_WRAPPER}" ]; then
  exec "${GUEST_WRAPPER}" "$@"
fi

exec "${GUEST_RUNNER}" "$@"
