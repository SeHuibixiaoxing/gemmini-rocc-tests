#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: run_pairdummy_cfg32_gdbserver_segment1_subbatch7_frontier.sh <run-host-private-ip> <guest-ip-or-endpoint> [local-port]

Attach cross-GDB to a pipeline-runtime gdbserver --once run prepared with:

  PIPELINE_RUNTIME_GDB_MARKER_ENABLE=1
  PIPELINE_RUNTIME_GDB_MARKER_SITE=worker-gemm-run
  PIPELINE_RUNTIME_GDB_MARKER_SEGMENT=1
  PIPELINE_RUNTIME_GDB_MARKER_GLOBAL_STAGE=1
  PIPELINE_RUNTIME_GDB_MARKER_LOCAL_STAGE=0
  PIPELINE_RUNTIME_GDB_MARKER_SUBBATCH=7

The helper waits for the marker, arms software breakpoints around the segment 1
stage 0 subbatch 7 compute/export/DMA frontier, then continues. If the target
stops making progress, it sends Ctrl-C and captures full stack/register/debug
state. It does not probe the guest TCP port with nc/telnet/curl; the first TCP
client to gdbserver --once is GDB.

Environment:
  RISCV_GDB                    Cross GDB path.
  EXPECT_BIN / EXPECT          Expect interpreter.
  PRT_GDB_TARGET_BIN           Host-side RISC-V ELF with symbols.
  PRT_GDB_OUT_ROOT             Output root. Default: tmp/firesim-aws-f2/gdbserver-tests.
  PRT_GDB_TAP_DEV              Run-host tap device. Default: tap0.
  PRT_GDB_STATIC_NEIGH_MAC     Optional static neighbor MAC for the guest.
  PRT_GDB_MARKER_TIMEOUT       Seconds to wait for initial marker. Default: 1500.
  PRT_GDB_FRONTIER_TIMEOUT     Seconds to wait after arming breakpoints. Default: 900.
  PRT_GDB_DETACH               Detach before quitting. Default: 1.
EOF
}

if [[ $# -eq 1 && ( "$1" == "-h" || "$1" == "--help" ) ]]; then
  usage
  exit 0
fi

if [[ $# -lt 2 || $# -gt 3 ]]; then
  usage >&2
  exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cy_dir="$(cd "${script_dir}/../../../../../.." && pwd)"

run_host_ip="$1"
guest_endpoint="$2"
local_port="${3:-32345}"

guest_ip="${guest_endpoint%:*}"
guest_port="2345"
if [[ "${guest_endpoint}" == *:* ]]; then
  guest_port="${guest_endpoint##*:}"
fi

gdb="${RISCV_GDB:-${cy_dir}/.conda-env/riscv-tools/bin/riscv64-unknown-linux-gnu-gdb}"
expect_bin="${EXPECT_BIN:-${EXPECT:-$(command -v expect || true)}}"
target_bin="${PRT_GDB_TARGET_BIN:-${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/build/rerocc-linux-tests/rerocc_pipeline_runtime-linux}"
ssh_key="${FIRESIM_SSH_KEY:-/home/ubuntu/firesim.pem}"
ssh_user="${FIRESIM_SSH_USER:-ubuntu}"
out_root="${PRT_GDB_OUT_ROOT:-${cy_dir}/tmp/firesim-aws-f2/gdbserver-tests}"
tap_dev="${PRT_GDB_TAP_DEV:-tap0}"
static_neigh_mac="${PRT_GDB_STATIC_NEIGH_MAC:-}"
marker_timeout="${PRT_GDB_MARKER_TIMEOUT:-1500}"
frontier_timeout="${PRT_GDB_FRONTIER_TIMEOUT:-900}"
detach_after="${PRT_GDB_DETACH:-1}"

runtime_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_runtime.c"
adapter_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_gemmini_adapter.c"
dma_src="${cy_dir}/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/src/prt_dma.c"

stamp="$(date -u +%Y%m%dT%H%M%SZ)"
out_dir="${out_root}/segment1-subbatch7-frontier-${stamp}-${run_host_ip//./_}-${guest_ip//./_}"
tunnel_log="${out_dir}/ssh-tunnel.log"
frontier_cmds="${out_dir}/segment1-subbatch7-frontier.gdb"
expect_script="${out_dir}/segment1-subbatch7-frontier.expect"
transcript="${out_dir}/segment1-subbatch7-frontier.expect.log"

mkdir -p "${out_dir}"

for numeric in marker_timeout frontier_timeout; do
  value="${!numeric}"
  if [[ ! "${value}" =~ ^[0-9]+$ || "${value}" -lt 1 ]]; then
    echo "invalid ${numeric}=${value}" >&2
    exit 2
  fi
done
case "${detach_after}" in
  0|1) ;;
  *)
    echo "invalid PRT_GDB_DETACH=${detach_after}; expected 0 or 1" >&2
    exit 2
    ;;
esac

if [[ ! -x "${gdb}" ]]; then
  echo "missing executable cross-gdb: ${gdb}" >&2
  exit 1
fi
if [[ -z "${expect_bin}" || ! -x "${expect_bin}" ]]; then
  echo "missing executable expect interpreter; set EXPECT_BIN" >&2
  exit 1
fi
if [[ ! -f "${target_bin}" ]]; then
  echo "missing target ELF: ${target_bin}" >&2
  exit 1
fi
if [[ ! -f "${ssh_key}" ]]; then
  echo "missing FireSim SSH key: ${ssh_key}" >&2
  exit 1
fi
if [[ ! -f "${runtime_src}" || ! -f "${adapter_src}" || ! -f "${dma_src}" ]]; then
  echo "missing source files for line breakpoints" >&2
  exit 1
fi

ssh_base=(
  ssh
  -i "${ssh_key}"
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o ConnectTimeout=10
  "${ssh_user}@${run_host_ip}"
)

if pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >/dev/null 2>&1; then
  echo "refusing duplicate tunnel on local port ${local_port} to ${guest_ip}:${guest_port}" >&2
  pgrep -af "ssh .*${local_port}:${guest_ip}:${guest_port}" >&2 || true
  exit 1
fi

if [[ -n "${static_neigh_mac}" ]]; then
  "${ssh_base[@]}" \
    "sudo ip neigh replace '${guest_ip}' lladdr '${static_neigh_mac}' dev '${tap_dev}' nud permanent && ip neigh show dev '${tap_dev}'" \
    > "${out_dir}/static-neigh.log" 2>&1
fi

cat > "${frontier_cmds}" <<EOF
set breakpoint pending on
set print pretty on
set print elements 64
set print repeats 16

break conv_call_for_manager_sync_strided
commands
silent
printf "\\n--- BP conv_call_for_manager_sync_strided ---\\n"
info args
bt 6
continue
end

break prt_run_pointwise_matmul_fallback_strided_impl
commands
silent
printf "\\n--- BP prt_run_pointwise_matmul_fallback_strided_impl ---\\n"
info args
info locals
bt 8
continue
end

break tiled_matmul_nn_stride_auto
commands
silent
printf "\\n--- BP tiled_matmul_nn_stride_auto ---\\n"
info args
bt 8
continue
end

break ${adapter_src}:2451
commands
silent
printf "\\n--- BP pointwise subcall return line 2451 ---\\n"
info args
info locals
bt 8
continue
end

break ${adapter_src}:2534
commands
silent
printf "\\n--- BP pointwise rr_fence begin line 2534 ---\\n"
info args
info locals
bt 8
continue
end

break ${adapter_src}:2545
commands
silent
printf "\\n--- BP pointwise gemmini_fence line 2545 ---\\n"
info args
info locals
bt 8
continue
end

break ${adapter_src}:2557
commands
silent
printf "\\n--- BP pointwise drain line 2557 ---\\n"
info args
info locals
bt 8
continue
end

break ${adapter_src}:2581
commands
silent
printf "\\n--- BP pointwise release line 2581 ---\\n"
info args
info locals
bt 8
continue
end

break ${adapter_src}:2593
commands
silent
printf "\\n--- BP conv_call_for_manager_sync_strided return line 2593 ---\\n"
info args
info locals
bt 8
continue
end

break prt_rr_acquire_scope if stage_id == 0 && opcode_id == 2
commands
silent
printf "\\n--- BP prt_rr_acquire_scope opcode2 ---\\n"
info args
bt 8
continue
end

break prt_rr_fence_scope
commands
silent
printf "\\n--- BP prt_rr_fence_scope ---\\n"
info args
print scope ? *scope : *scope
bt 8
continue
end

break ${runtime_src}:4515
commands
silent
printf "\\n--- BP worker after prt_gemm_conv_run line 4515 ---\\n"
info locals
bt 8
continue
end

break ${runtime_src}:4560
commands
silent
printf "\\n--- BP worker export-sync enter line 4560 ---\\n"
info locals
bt 8
continue
end

break sync_stage_export_aliases
commands
silent
printf "\\n--- BP sync_stage_export_aliases ---\\n"
info args
bt 8
continue
end

break ${runtime_src}:4571
commands
silent
printf "\\n--- BP worker export-sync return line 4571 ---\\n"
info locals
bt 8
continue
end

break ${runtime_src}:4582
commands
silent
printf "\\n--- BP worker compute-done line 4582 ---\\n"
info locals
bt 8
continue
end

break prt_dma_submit
commands
silent
printf "\\n--- BP prt_dma_submit ---\\n"
info args
print tok ? *tok : *tok
bt 8
continue
end

break dma_blocking_wait
commands
silent
printf "\\n--- BP dma_blocking_wait ---\\n"
info args
print tok ? *tok : *tok
bt 8
continue
end

break dma_gdb_marker_wait_return
commands
silent
printf "\\n--- BP dma_gdb_marker_wait_return ---\\n"
info args
print tok ? *tok : *tok
bt 8
continue
end

info breakpoints
EOF

ssh -N \
  -L "${local_port}:${guest_ip}:${guest_port}" \
  -i "${ssh_key}" \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o ExitOnForwardFailure=yes \
  -o ServerAliveInterval=30 \
  -o ServerAliveCountMax=3 \
  "${ssh_user}@${run_host_ip}" > "${tunnel_log}" 2>&1 &
tunnel_pid=$!

cleanup() {
  if kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
    kill "${tunnel_pid}" >/dev/null 2>&1 || true
    wait "${tunnel_pid}" >/dev/null 2>&1 || true
  fi
}
trap cleanup EXIT

for _ in $(seq 1 50); do
  if ss -ltn "sport = :${local_port}" | grep -q ":${local_port}"; then
    break
  fi
  if ! kill -0 "${tunnel_pid}" >/dev/null 2>&1; then
    echo "SSH tunnel exited before listening; see ${tunnel_log}" >&2
    exit 1
  fi
  sleep 0.1
done

if ! ss -ltn "sport = :${local_port}" | grep -q ":${local_port}"; then
  echo "SSH tunnel did not listen on local port ${local_port}; see ${tunnel_log}" >&2
  exit 1
fi

cat > "${expect_script}" <<'EOF'
#!/usr/bin/env expect
set timeout [lindex $argv 5]

set gdb [lindex $argv 0]
set elf [lindex $argv 1]
set port [lindex $argv 2]
set transcript [lindex $argv 3]
set frontier_cmds [lindex $argv 4]
set marker_timeout [lindex $argv 5]
set frontier_timeout [lindex $argv 6]
set detach_after [lindex $argv 7]

log_file -noappend $transcript
spawn $gdb -q $elf

proc need_prompt {} {
    expect {
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for gdb prompt"; exit 3 }
        eof { puts stderr "gdb exited unexpectedly"; exit 4 }
    }
}

proc gdb_cmd {cmd {timeout_s 300}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "$cmd\r"
    need_prompt
    set ::timeout $old_timeout
}

proc target_remote {port {timeout_s 300}} {
    set old_timeout $::timeout
    set ::timeout $timeout_s
    send -- "target remote :$port\r"
    expect {
        -re "Remote communication error|Target disconnected|Connection reset by peer" {
            puts stderr "target remote failed"
            exit 7
        }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for target remote"; exit 8 }
        eof { puts stderr "gdb exited during target remote"; exit 9 }
    }
    set ::timeout $old_timeout
}

proc continue_to_marker {marker_timeout} {
    set old_timeout $::timeout
    set ::timeout $marker_timeout
    send -- "continue\r"
    expect {
        -re "Breakpoint 1, .*prt_gdb_marker_stop" {
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "prt_gdb_marker_stop" {
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "exited normally|exited with code" {
            puts stderr "inferior exited before marker"
            need_prompt
            exit 10
        }
        timeout {
            puts stderr "timeout waiting for marker"
            exit 11
        }
        eof { puts stderr "gdb exited before marker"; exit 12 }
    }
}

proc interrupt_and_dump {} {
    puts "GDB_FRONTIER_TIMEOUT_SEND_CTRL_C"
    send \003
    expect {
        -re "received signal SIGINT|Program received signal SIGINT|Thread .* received signal SIGINT" {
            need_prompt
        }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for Ctrl-C stop"; exit 13 }
        eof { puts stderr "gdb exited during Ctrl-C"; exit 14 }
    }
    puts "GDB_FRONTIER_TIMEOUT_STOPPED"
    gdb_cmd "printf \"\\n--- timeout diagnostic: threads ---\\n\""
    gdb_cmd "info threads" 300
    gdb_cmd "printf \"\\n--- timeout diagnostic: all bt full ---\\n\""
    gdb_cmd "thread apply all bt full" 600
    gdb_cmd "printf \"\\n--- timeout diagnostic: registers ---\\n\""
    gdb_cmd "info registers pc sp ra a0 a1 a2 a3 a4 a5 a6 a7" 300
    gdb_cmd "printf \"\\n--- timeout diagnostic: pc window ---\\n\""
    gdb_cmd "x/32i \$pc-64" 300
    gdb_cmd "printf \"\\n--- timeout diagnostic: marker/debug state ---\\n\""
    gdb_cmd "print g_prt_gdb_marker_state" 300
    gdb_cmd "print g_prt_debug_state" 300
    gdb_cmd "print g_prt_debug_tls_state" 300
}

proc continue_frontier {frontier_timeout} {
    set old_timeout $::timeout
    set ::timeout $frontier_timeout
    puts "GDB_FRONTIER_CONTINUE"
    send -- "continue\r"
    expect {
        -re "exited normally|exited with code" {
            puts "GDB_FRONTIER_INFERIOR_EXITED"
            need_prompt
            set ::timeout $old_timeout
            return 0
        }
        -re "\\(gdb\\) $" {
            puts "GDB_FRONTIER_STOPPED_WITH_PROMPT"
            set ::timeout $old_timeout
            return 2
        }
        timeout {
            set ::timeout $old_timeout
            interrupt_and_dump
            return 1
        }
        eof { puts stderr "gdb exited during frontier continue"; exit 15 }
    }
}

need_prompt
gdb_cmd "set pagination off"
gdb_cmd "set confirm off"
gdb_cmd "set print pretty on"
gdb_cmd "set print thread-events off"
gdb_cmd "set debuginfod enabled off"
gdb_cmd "set auto-load safe-path /"
gdb_cmd "set remotetimeout 180"
target_remote $port 300
puts "GDB_FRONTIER_MARK_CONNECTED"

gdb_cmd "break prt_gdb_marker_stop" 120
continue_to_marker $marker_timeout
puts "GDB_FRONTIER_MARKER_HIT"
gdb_cmd "printf \"\\n--- marker state ---\\n\""
gdb_cmd "print g_prt_gdb_marker_state"
gdb_cmd "bt 16"
gdb_cmd "thread apply all bt" 300
gdb_cmd "delete 1"
gdb_cmd "source $frontier_cmds" 300

set outcome [continue_frontier $frontier_timeout]

if {$outcome == 2} {
    gdb_cmd "printf \"\\n--- unexpected prompt diagnostic ---\\n\""
    gdb_cmd "info threads" 300
    gdb_cmd "thread apply all bt full" 600
    gdb_cmd "print g_prt_gdb_marker_state" 300
    gdb_cmd "print g_prt_debug_state" 300
}

if {$detach_after == 1} {
    send -- "detach\r"
    expect {
        -re "Ending remote debugging|Inferior .* detached|Detaching from program" { need_prompt }
        -re "\\(gdb\\) $" {}
        timeout { puts stderr "timeout waiting for detach"; exit 16 }
        eof { puts stderr "gdb exited during detach"; exit 17 }
    }
    puts "GDB_FRONTIER_DETACH_OK"
}

send -- "quit\r"
expect eof
exit 0
EOF

echo "[segment1-subbatch7-frontier] tunnel localhost:${local_port} -> ${guest_ip}:${guest_port} via ${run_host_ip}"
echo "[segment1-subbatch7-frontier] target_bin=${target_bin}"
echo "[segment1-subbatch7-frontier] gdb_cmds=${frontier_cmds}"
echo "[segment1-subbatch7-frontier] transcript=${transcript}"
echo "[segment1-subbatch7-frontier] marker_timeout=${marker_timeout} frontier_timeout=${frontier_timeout}"

set +e
"${expect_bin}" -f "${expect_script}" \
  "${gdb}" "${target_bin}" "${local_port}" "${transcript}" "${frontier_cmds}" \
  "${marker_timeout}" "${frontier_timeout}" "${detach_after}" \
  > "${out_dir}/expect-driver.stdout" 2> "${out_dir}/expect-driver.stderr"
expect_rc=$?
set -e

echo "[segment1-subbatch7-frontier] expect_rc=${expect_rc}"
echo "[segment1-subbatch7-frontier] out_dir=${out_dir}"

if [[ "${expect_rc}" -ne 0 ]]; then
  tail -n 260 "${transcript}" >&2 || true
  cat "${out_dir}/expect-driver.stderr" >&2 || true
  exit "${expect_rc}"
fi

if ! grep -q "GDB_FRONTIER_MARKER_HIT" "${transcript}" && \
   ! grep -q "GDB_FRONTIER_MARKER_HIT" "${out_dir}/expect-driver.stdout"; then
  echo "marker did not hit; see ${transcript}" >&2
  exit 1
fi

if grep -q "GDB_FRONTIER_TIMEOUT_STOPPED" "${transcript}" || \
   grep -q "GDB_FRONTIER_TIMEOUT_STOPPED" "${out_dir}/expect-driver.stdout"; then
  echo "[segment1-subbatch7-frontier] PASS captured timeout stack"
  exit 0
fi

if grep -q "GDB_FRONTIER_INFERIOR_EXITED" "${transcript}" || \
   grep -q "GDB_FRONTIER_INFERIOR_EXITED" "${out_dir}/expect-driver.stdout"; then
  echo "[segment1-subbatch7-frontier] PASS inferior exited"
  exit 0
fi

echo "[segment1-subbatch7-frontier] PASS marker and frontier transcript captured"
