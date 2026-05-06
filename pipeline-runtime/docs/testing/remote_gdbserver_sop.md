# Remote gdbserver SOP

Updated: `2026-05-06`

This SOP is for host-side `gdb` connecting to guest Linux `gdbserver` through
FireSim NIC networking. It complements `local_gdb_debug_sop.md`, which is the
UART/local-gdb fallback path when NIC is unavailable.

## Known-Good Baseline

The historical single-core NIC noTrace 30 MHz bitstream is the baseline for the
software control path:

- AGFI: `agfi-0079cbbca617eca4e`
- AFI: `afi-0ee7774f829acd4de`
- Build result: `sims/firesim/deploy/results-build/2026-04-30--18-47-10-firesim_rocket_singlecore_nic_notrace_30mhz/`
- Config: `FireSimRocketNICNoTraceConfig + BaseF2Config`
- Type: single-core Rocket + NIC + no TraceIO + 30 MHz
- Not an 8BP variant

The recovered software/harness path has also been validated against the
single-core 1BP path in later checkpoints. Treat that as the control: if this
baseline passes and a new AGFI fails the same matrix with the same workload and
driver bundle discipline, first suspect AGFI/driver collateral or hardware
implementation behavior, not generic gdbserver setup.

## Capability Matrix

The baseline remote gdbserver workflow must cover at least:

- `target remote`
- multiple software breakpoints
- `continue`
- `next`
- `info threads`
- `thread apply all bt`
- register reads
- disassembly
- variable reads and writes
- memory reads and writes
- thread switching
- Ctrl-C regain of control
- `detach`

Do not make hardware breakpoints or hardware watchpoints a required milestone for
pipeline-runtime debug. The current mainline is software breakpoints through
Linux ptrace and `gdbserver`.

## Preconditions

Before launching a test:

- HWDB points at the intended AGFI.
- FireSim driver bundle was rebuilt for that exact AGFI/build result.
- `firesim infrasetup` was rerun after any AGFI, HWDB, rootfs, workload, or
  binary change.
- workload-local rootfs includes `gdbserver`.
- target binary is unstripped and has useful symbols, preferably `-Og -g` or
  `-O0 -g`.
- guest script prints a clear phase marker before starting `gdbserver`, for
  example `[gdbserver] phase=listening`.
- guest script has a deterministic completion path after the debugged process
  exits, usually by writing a marker and calling `poweroff`.

Use the FireSim manager from `sims/firesim`:

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
```

Long-running commands should use the repo wrapper:

```bash
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh launchrunfarm \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>

/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh infrasetup \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>

/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh runworkload \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>
```

## Guest gdbserver Runner

A minimal guest-side runner should avoid accepting stray connections and should
make state visible in UART/results:

```sh
#!/bin/sh
set -eu

LOG_DIR=/root/gdbserver-debug
INFO="${LOG_DIR}/gdbserver.info"
mkdir -p "${LOG_DIR}"

TARGET=/root/gdbserver-smoke/gdbserver-smoke
PORT=2345

echo "phase=starting" > "${INFO}"
ip addr show > "${LOG_DIR}/ip.txt" 2>&1 || true
echo "[gdbserver] phase=listening port=${PORT}" >/dev/console
echo "phase=listening" > "${INFO}"

exec gdbserver --once 0.0.0.0:${PORT} "${TARGET}" \
  >"${LOG_DIR}/gdbserver.log" 2>&1
```

For an automated pass/fail run, wrap this so that after the debugged program
exits the script writes a completion marker, syncs, and powers off. If you use
`detach`, remember that `gdbserver --once` can exit while the guest workload
does not necessarily reach its final `poweroff` path.

## Live Inspection

Find the run host from manager logs or EC2 tags. Prefer private IP:

```bash
aws ec2 describe-instances \
  --filters 'Name=instance-state-name,Values=running' \
  --query 'Reservations[].Instances[].{Id:InstanceId,Type:InstanceType,PrivateIp:PrivateIpAddress,Tags:Tags}' \
  --output table
```

If manager has not copied artifacts back, inspect live slot files:

```bash
ssh -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip> \
  'tail -200 /home/ubuntu/sim_slot_0/uartlog; ls -lh /home/ubuntu/sim_slot_0'
```

Wait for the guest marker:

```text
[gdbserver] phase=listening
```

Also confirm basic NIC state when possible:

```bash
ssh -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip> \
  'ip addr show tap0; ping -c 2 172.16.0.2'
```

## Tunnel

The guest IP `172.16.0.2` is reachable from the F2 run host, not generally from
the manager. Use an SSH local forward:

```bash
ssh -N -L 32345:172.16.0.2:2345 \
  -i /home/ubuntu/firesim.pem \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  ubuntu@<run-host-private-ip>
```

Keep this tunnel process alive while running host-side `gdb`.

Do not probe `gdbserver --once` with `nc`, `telnet`, browser fetches, or generic
port scanners before `gdb` connects. A stray TCP connection consumes the one
allowed session and invalidates the test.

## Host gdb Session

Use the RISC-V cross gdb matching the target binary:

```bash
riscv64-unknown-linux-gnu-gdb /path/to/target-binary
```

Recommended initial commands:

```gdb
set pagination off
set confirm off
set print thread-events off
set breakpoint pending on
target remote localhost:32345
info threads
break main
continue
bt
info registers
x/16i $pc
```

Software breakpoint and stepping matrix:

```gdb
break main
break <second_function>
info breakpoints
continue
next
step
info threads
thread apply all bt
thread 1
bt
```

Memory and variable probes:

```gdb
p variable_name
set var variable_name = 1
x/16gx 0x<addr>
set {int}0x<addr> = 1234
x/16i $pc
info registers
```

Ctrl-C regain test:

1. Start or continue into a long-running section.
2. Press Ctrl-C in host `gdb`.
3. Confirm `gdb` regains the prompt.
4. Run `bt`, `info threads`, and `x/8i $pc`.

Detach test:

```gdb
detach
quit
```

Only treat detach as pass if the guest side has a known completion path or the
run is intentionally live-inspected and then manually terminated.

## Result Collection

After the workload completes, inspect copied results:

```bash
find /home/ubuntu/chipyard/sims/firesim/deploy/results-workload \
  -path '*gdbserver*' -type f | sort
```

Archive at least:

- manager build/run logs
- runtime YAML, HWDB YAML, build recipe YAML
- AGFI/AFI
- target binary checksum
- guest `gdbserver.log`
- guest `gdbserver.info`
- UART log excerpt with boot, NIC, and `[gdbserver]` markers
- host-side gdb transcript
- ping or packet-capture proof when NIC correctness is being tested

## Cleanup

After copying artifacts and live inspection:

```bash
cd /home/ubuntu/chipyard/sims/firesim/deploy
firesim terminaterunfarm --forceterminate \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>
```

Then verify the F2 instance is no longer running. If FireSim reports success but
the instance remains running, explicitly reclaim it with AWS termination after
confirming it is the stale run-farm instance, not an active build host.

## Checkpoint Commit Rule

After every key gdbserver milestone, commit the exact tested state. The commit
message should include:

- AGFI/AFI
- target config and platform config
- runtime YAML, HWDB YAML, build recipe YAML
- workload name
- driver/rootfs/binary change summary
- exact gdb capability matrix pass/fail
- artifact/result paths
- known limitations, especially whether `detach` prevented normal workload
  shutdown
