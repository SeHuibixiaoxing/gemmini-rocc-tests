#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out_dir="${script_dir}/overlay/root/gdbserver-smoke"
out_bin="${out_dir}/gdbserver-smoke"
cc="${RISCV_LINUX_GCC:-/home/ubuntu/chipyard/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gcc}"

mkdir -p "${out_dir}"

if ! "${cc}" -g -O0 -pthread -static -o "${out_bin}" "${script_dir}/gdbserver-smoke.c"; then
  "${cc}" -g -O0 -pthread -o "${out_bin}" "${script_dir}/gdbserver-smoke.c"
fi

echo "${out_bin}"
