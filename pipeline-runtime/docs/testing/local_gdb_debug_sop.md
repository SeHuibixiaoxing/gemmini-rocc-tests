# Pipeline Runtime Local GDB Debug SOP

更新时间：`2026-04-26 17:15 UTC`

## 1. 适用范围

这条路线可以用于后续 `pipeline-runtime` 测试负载，前提是 workload-local
rootfs 里带 target-side `gdb`，并且被调试的 target 程序带符号或至少保留
可用函数符号。

它不依赖 guest NIC，不要求 `tap0`、guest IP、`gdbserver` 或 host 到 guest TCP
链路可用。调试流量走 FireSim UART，因此适合在 NIC 还不稳定时确认：

- 程序是否能进入 `main`
- 当前 PC / 调用栈 / 寄存器
- 某个断点前后的用户态状态
- 不依赖网络的最小复现是否成立

它不能替代 host-side `gdbserver` 的所有能力。大型程序长时间交互、反复
continue 后人工抢回控制、attach 已运行进程等场景，仍然更适合 `gdbserver`
或一个可用的第二控制通道。

## 2. 方案选择

优先级建议：

1. **Batch local gdb**：推荐。guest 启动后自动运行 `gdb -batch`，把 `bt`、
   `info registers`、指令窗口等写入文件和 UART，runworkload 结束后自动
   copy-back。
2. **Interactive UART gdb**：只在需要手动试命令时使用。通过 `screen -X stuff`
   往 target UART shell 注入命令。
3. **Attach 模式**：只有在 workload 自己能先启动后台程序，且还能保留一个
   可输入 shell 时使用。没有第二 shell 时不要默认选它。

## 3. Rootfs 要求

workload-local Buildroot kfrag 需要至少包含：

```text
BR2_PACKAGE_GDB=y
BR2_PACKAGE_GDB_DEBUGGER=y
```

如果同一个 rootfs 也要继续支持网络 `gdbserver`，再加：

```text
BR2_PACKAGE_GDB_SERVER=y
```

当前最小 smoke 已有示例：

- `pipeline-runtime/workloads/rocket-singlecore-nic-gdbserver-smoke/linux_target_gdb.kfrag`
- `rerocc-linux-tests-coupleddma/workload/linux_gdbserver.kfrag`

不要改共享 `br-base`；把 gdb 放到 workload-local rootfs 变体里。

## 4. 被调试程序要求

推荐编译选项：

```bash
-g -O0
```

如果程序太大，可以用 `-Og -g`。不要 strip target binary。若源码路径在 target
里不存在，gdb 仍能用符号和地址停住，但会显示 `No such file or directory`；
这不影响 `bt`、`info registers` 和反汇编。

对 `pipeline-runtime` 正式负载，建议先确认：

- guest 里的实际 binary 路径
- runner 传入的完整 argv
- 关键环境变量是否已写入 `/firemarshal.env` 或 runner 脚本
- 输出目录如 `/root/pipeline-runtime-debug/` 是否存在

## 5. Batch local gdb SOP

### 5.1 写 guest runner

runner 的核心结构如下：

```sh
#!/bin/sh
set -eu

LOG_DIR=/root/pipeline-runtime-debug
INFO_PATH="${LOG_DIR}/local-gdb.info"
LOG_PATH="${LOG_DIR}/local-gdb.log"
GDB_CMDS=/tmp/local-gdb.gdb
TARGET_BIN=/root/path/to/rerocc_pipeline_runtime-linux

mkdir -p "${LOG_DIR}"

cat > "${GDB_CMDS}" <<'EOF'
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set auto-load safe-path /
break main
run
bt
info registers
x/16i $pc
quit
EOF

echo "phase=running" > "${INFO_PATH}"
/usr/bin/gdb -q -batch -x "${GDB_CMDS}" --args "${TARGET_BIN}" \
  arg1 arg2 arg3 > "${LOG_PATH}" 2>&1
rc=$?
echo "phase=exit" > "${INFO_PATH}"
echo "gdb_rc=${rc}" >> "${INFO_PATH}"
tail -120 "${LOG_PATH}" >/dev/console 2>/dev/null || true
sync
poweroff -f || poweroff || true
exit "${rc}"
```

如果程序内部有 `sleep()`、等待输入、等待硬件事件等长等待，优先通过环境变量
让本地 gdb 版本走短路径。例如 smoke 程序使用：

```gdb
set environment GDBSERVER_SMOKE_SLEEP_ITERS 0
```

对正式 `pipeline-runtime`，可以仿照这个方式增加 debug-only 环境变量，把
batch local gdb 停在真正关心的早期断点，不要一上来无界 `continue`。

### 5.2 workload JSON

workload JSON 中要：

- 引入 gdb kfrag
- 把 gdb 日志列入 outputs
- run 指向 local-gdb runner

示例：

```json
{
  "name": "pipeline-runtime-local-gdb",
  "base": "br-base.json",
  "workdir": ".",
  "distro": {
    "name": "br",
    "opts": {
      "configs": ["linux_target_gdb.kfrag"]
    }
  },
  "outputs": [
    "/root/pipeline-runtime-debug/local-gdb.info",
    "/root/pipeline-runtime-debug/local-gdb.log"
  ],
  "run": "run_pipeline_runtime_local_gdb.sh"
}
```

### 5.3 FireSim 执行

必须走标准 manager 流程，并且 manager 命令从 `sims/firesim` source 环境：

```bash
cd /home/ubuntu/chipyard/sims/firesim
source sourceme-manager.sh --skip-ssh-setup
```

如果改了 workload、rootfs、binary 或 AGFI，先重新 `infrasetup`。

长任务统一用 tmux wrapper：

```bash
/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh launchrunfarm \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>

/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh infrasetup \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>

/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh runworkload \
  -c <runtime.yaml> -a <hwdb.yaml> -r <build-recipes.yaml>
```

run 完成后看：

```bash
find sims/firesim/deploy/results-workload -path '*local-gdb*' -type f | sort
tail -200 <result-dir>/<job>/local-gdb.log
cat <result-dir>/<job>/local-gdb.info
```

如果 runworkload 没有 copy-back，先不要关 runfarm。按 live inspection SOP
SSH 到 run host private IP，手动取：

```bash
ssh -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip> \
  'ls -lh /home/ubuntu/sim_slot_0; tail -200 /home/ubuntu/sim_slot_0/uartlog'
```

再 `scp` 现场回来，最后 terminate runfarm。

## 6. Interactive UART gdb SOP

### 6.1 guest shell runner

interactive runner 应优先这样启动 shell：

```sh
if command -v setsid >/dev/null 2>&1 && command -v cttyhack >/dev/null 2>&1; then
  setsid cttyhack /bin/sh -i </dev/console >/dev/console 2>&1
else
  /bin/sh -i </dev/console >/dev/console 2>&1
fi
```

`setsid cttyhack` 的目的是让 shell 尽量拿到控制终端，改善 Ctrl-C 和前台进程
控制。

### 6.2 注入命令

run host 上通常有 `fsim0` screen：

```bash
ssh -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip> 'screen -ls'
```

注入只读探针：

```bash
ssh -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip> \
  'screen -S fsim0 -X stuff "echo UART_PING; pwd; /usr/bin/gdb --version\r"'
```

进入 gdb：

```bash
ssh -i /home/ubuntu/firesim.pem ubuntu@<run-host-private-ip> \
  'screen -S fsim0 -X stuff "/usr/bin/gdb -q /root/path/to/program\r"'
```

常用命令：

```gdb
set pagination off
set confirm off
break main
run <args>
bt
info registers
x/16i $pc
```

如果后续要 `continue`，先确认当前 shell 有 job control，或者确认程序会很快
自然退出。否则优先使用 batch local gdb。

## 7. “没有 job control” 是什么

在本轮 FPGA 现场里，UART shell 打印：

```text
sh: cannot set terminal process group: Inappropriate ioctl for device
sh: no job control in this shell
```

这表示这个 shell 只是把 stdin/stdout 接到了 `/dev/console`，但它不是这个终端的
控制会话，内核没有给它正常的 foreground process group。

影响是：

- `Ctrl-C` 不一定会变成发给前台程序的 `SIGINT`
- 它可能只是作为字符 `^C` 进入 UART 输入流
- gdb `continue` 之后，inferior 在跑，gdb 等待事件；如果没有正常 SIGINT，
  人工很难把控制权抢回 gdb prompt
- `fg/bg/jobs` 这类 shell 作业控制不可用

这不是 NIC 问题，也不是 gdb 一定坏了；它是当前 UART shell 启动方式的问题。

## 8. 改进方法

推荐顺序：

1. **优先 batch local gdb**
   把要抓的断点、`bt`、寄存器、反汇编写成 gdb command file。避免依赖人工
   Ctrl-C。

2. **interactive shell 用 `setsid cttyhack`**
   Buildroot/BusyBox 常见做法是：

   ```sh
   setsid cttyhack /bin/sh -i </dev/console >/dev/console 2>&1
   ```

   如果 target 有明确 tty，如 `/dev/ttySIF0`，也可以实验：

   ```sh
   setsid cttyhack /bin/sh -i </dev/ttySIF0 >/dev/ttySIF0 2>&1
   ```

3. **不要无界 `continue`**
   交互调试时优先用：

   ```gdb
   tbreak some_function
   continue
   bt
   ```

   或者在程序里用 debug-only 环境变量跳过长等待。

4. **在 runner 里加 timeout 和自动现场导出**
   batch 脚本可以在超时后 `kill -INT` / `kill -TERM` gdb，并把 log tail 打到
   `/dev/console`。即使 Ctrl-C 不可靠，也能有 copy-back 证据。

5. **如果 NIC 恢复，再切回 gdbserver**
   网络可用后，host-side cross-gdb + guest gdbserver 的交互体验更好，也更适合
   长时间调试和 attach。

## 9. 后续改进建议

这一节是给后续接手者的工程建议。目标是把当前“可用的本地 gdb 路线”提升成
`pipeline-runtime` 的稳定调试入口。

### 9.1 优先级 P0：做一个正式 pipeline-runtime local-gdb workload

当前已经验证的是最小 `gdbserver-smoke` 程序。真实 `pipeline-runtime`
负载已有 workload-local 变体：

- workload JSON：
  `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime-batch8-fileonly-sync-pairdummy-local-gdb.json`
- runner：
  `run_rerocc_pipeline_runtime_bertmini_local_gdb.sh`
- 复用或新增 Buildroot kfrag，确保 guest 内有 `/usr/bin/gdb`
- outputs 至少包含：
  - `/root/pipeline-runtime-debug/local-gdb.info`
  - `/root/pipeline-runtime-debug/local-gdb.log`
  - 原 pipeline-runtime 的 breadcrumb / audit / result 文件

验收标准：

- 不依赖 NIC、`tap0` 或 guest IP。
- `runworkload` 结束后能在 `results-workload/` 看到 `local-gdb.log`。
- log 至少包含 `break main` 命中、`bt`、`info registers`、`x/16i $pc`。

### 9.2 优先级 P0：把 gdb command file 参数化

runner 支持这些环境变量：

- `PIPELINE_RUNTIME_LOCAL_GDB_EXTRA_BREAKPOINTS`
- `PIPELINE_RUNTIME_LOCAL_GDB_CONTINUE_AFTER_MAIN`
- `PIPELINE_RUNTIME_LOCAL_GDB_TIMEOUT_SECS`
- `PIPELINE_RUNTIME_LOCAL_GDB_LOG_PATH`
- `PIPELINE_RUNTIME_LOCAL_GDB_INFO_PATH`

推荐默认命令：

```gdb
set pagination off
set confirm off
set print thread-events off
set debuginfod enabled off
set auto-load safe-path /
break main
run
bt
info registers
x/16i $pc
quit
```

如果需要继续跑到某个函数，使用一次性断点：

```gdb
tbreak pipeline_runtime_interesting_function
continue
bt
info registers
```

不要默认无界 `continue`。

### 9.3 优先级 P1：解决源码路径映射

当前 gdb 能停在带符号的函数上，但如果 target rootfs 没有 host 源码路径，会显示：

```text
No such file or directory
```

这不影响栈和寄存器，但影响 `list` 和源码级单步。建议两种方案二选一：

1. 在 gdb command file 里自动加入 `set substitute-path`：

   ```gdb
   set substitute-path /home/ubuntu/chipyard /root/chipyard-src
   ```

   然后把必要源码片段放进 rootfs 的 `/root/chipyard-src`。

2. 不拷源码，只在 log 中默认打印反汇编：

   ```gdb
   x/32i $pc
   disassemble /m main
   ```

   这更轻量，适合 FPGA 长调试。

验收标准：local-gdb log 能稳定给出源码行或反汇编窗口，二者至少有一个。

### 9.4 优先级 P1：改进 interactive UART shell

当前已经把 interactive runner 改为优先：

```sh
setsid cttyhack /bin/sh -i </dev/console >/dev/console 2>&1
```

后续应验证它在最新 rootfs 中是否真的消除了：

```text
sh: no job control in this shell
```

如果仍然没有 job control，继续尝试：

- 明确使用真实串口设备，例如 `/dev/ttySIF0`
- 在 `/etc/inittab` 中给 console 开 `getty`
- runner 不直接启动 `/bin/sh -i`，而是让 init 管理一个登录 shell

验收标准：

- UART shell 不再打印 `no job control`。
- gdb `continue` 后，发送 Ctrl-C 能回到 gdb prompt。

注意：不要因为 interactive shell 仍不完美而阻塞 batch local gdb。batch 路线更稳。

### 9.5 优先级 P1：增加失败现场自动归档

runner 应在成功、失败、超时三种情况下都写清楚状态：

```text
phase=exit|timeout|missing-gdb|missing-target-bin
gdb_rc=<rc>
elapsed_secs=<secs>
target_bin=<path>
argv=<argv>
```

超时时建议：

- 先 `kill -INT <gdb_pid>`
- 等 5 秒
- 再 `kill -TERM`
- 最后 `kill -KILL`
- 把 `tail -200 local-gdb.log` 打到 `/dev/console`

验收标准：

- 即使 `runworkload` 没有 copy-back，也能从 `uartlog` 看到最后 200 行 gdb 现场。
- 手动 SSH 到 run host 后能从 guest copied output 或 UART 恢复失败原因。

### 9.6 优先级 P2：支持 attach 已运行进程

如果要调试“程序已经跑起来后卡住”的情况，需要一个更复杂的本地方案：

1. runner 后台启动 target 程序，并把 PID 写入文件。
2. runner 再启动 gdb：

   ```gdb
   attach <pid>
   thread apply all bt
   info registers
   detach
   quit
   ```

3. 必须确认 Buildroot / kernel 允许 ptrace attach。

风险：

- 如果没有第二 shell，attach 模式更容易把现场锁死。
- 如果程序很快退出，attach 可能抓不到。
- 优先先做 batch run-from-start，再做 attach。

### 9.7 优先级 P2：增加 core dump fallback

对不适合 gdb 长时间在线跑的 case，可以增加 core dump 兜底：

```sh
ulimit -c unlimited
echo /root/pipeline-runtime-debug/core.%e.%p > /proc/sys/kernel/core_pattern
```

然后让程序自然崩溃或由 watchdog 触发 abort。runworkload copy-back core 后，host
侧用 cross-gdb 离线看：

```bash
riscv64-unknown-linux-gnu-gdb <target-bin> core.<name>.<pid>
```

验收标准：

- 异常退出时能拿到 core。
- host 侧能离线 `bt`。

### 9.8 优先级 P2：和 gdbserver 路线共存

建议保留两套 workload：

- `*-gdbserver`：网络恢复后用于 host-side cross-gdb 长交互。
- `*-local-gdb`：NIC 不通或只需要早期现场时使用。

不要把 local-gdb 改成依赖 `example_1config`、`tap0` 或 guest IP。它的价值正是
不依赖 NIC。

## 10. 结论

后续跑 `pipeline-runtime` 测试负载时，可以用本地 gdb 路线调试。最稳的方式不是
手工 UART 长交互，而是为该 workload 准备一个 local-gdb rootfs 变体和 batch
runner，让 guest 自己启动 `/usr/bin/gdb -batch`，把完整现场写到
`/root/pipeline-runtime-debug/` 并由 FireSim copy-back。
