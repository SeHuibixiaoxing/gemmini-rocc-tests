# Pipeline Runtime Hard Constraints

## 1. 执行入口

- `pairdummy/sbus128` 主线默认只允许使用：
  - `scripts/pairdummy_sbus128_fixed_env.sh`
  - `scripts/pairdummy_sbus128_workflow.sh`
  - `docs/workflows/pairdummy_sbus128.md`
- 除非明确在做新的 debug profile bring-up，不要在 shell 里临时叠一组 env 直接跑。

## 2. 编译与环境

- 任何编译、FireMarshal、FireSim 前都必须先：
  `cd /home/ubuntu/chipyard/sims/firesim`
- 然后执行：
  `set +u; source sourceme-manager.sh --skip-ssh-setup; set -u`
- 如需回到 repo 根目录再跑命令，再补：
  `source /home/ubuntu/chipyard/env.sh`

## 3. FireMarshal / FireSim

- FireMarshal 只允许通过：
  `/home/ubuntu/chipyard/scripts/firemarshal-tmux-run.sh`
- FireSim manager 只允许通过：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- FireSim 正规流固定为：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`

## 4. Freshness

- 每次 `infrasetup` 或 `runworkload` 前，必须完成 freshness 闭环。
- local freshness 至少覆盖：
  - guest image
  - runner script
  - runtime binary
  - `firemarshal.env`
- remote freshness 必须在 run host 的 guest image 上验证，而不是看 host 文件系统或旧 `*-bin` 包装物。
- 任何 freshness 不通过，先归类为 stale image / wrong source layer，不要继续解释成新 blocker。

## 5. SSH 与 live 检查

- SSH / live 检查一律使用私网 IP，不允许使用公网 IP。
- guest 文件必须通过 run host 上的 guest image + `debugfs` 读取。
- 不要把 run host 的 `/dev/root` 当成 guest image。
- 当前 remote gdbserver workload 默认只保证 UART、run-host 文件、`heartbeat.csv` 和
  gdbserver TCP 调试入口；不要把 guest `/proc/<pid>/...` 当成未预置的旁路能力。
- 如果需要卡死后读取 guest `/proc/<pid>/task/*/{stack,wchan,syscall,status}`，必须提前提供
  guest 登录通道、guest-side sampler，或先由 GDB 成功停住目标。仅凭一个 UART 口不能在
  host 侧事后任意读取 guest `/proc`。
- Linux boot 阶段只要没有明确 boot error / panic / crash，
  且仍停留在早期启动过程，
  就不要把静默窗口记成新的异常或 blocker。
- 这类阶段若 `heartbeat.csv` 仍在推进，
  先继续等，
  不要过早 terminate / 改 probe / 改叙事。
- 只有出现明确 boot 错误、
  reboot loop、
  或已经进入用户态 workload 后再次停住，
  才把它升级为新的卡点。

## 6. 内存与页表语义

- 只要 page-table backing 需要物理连续，就不能退回普通匿名页。
- 当前 `spm_xlate` 路径必须保留 hugetlb / contiguous page 语义。
- 当前主线必须保留 `PIPELINE_RUNTIME_MLOCKALL_MODE=2` 的 prefault + targeted lock 路线。
- 不要把 `MCL_CURRENT`、`MCL_FUTURE` 或普通匿名页 fallback 当成可随意替换的等价语义。

## 7. DMA / Gemmini 同步语义

- **已知重要约束：DMA doneflag 已验证有问题，不能作为完成语义。**
- 不要重新引入 DMA doneflag 轮询作为完成逻辑；这不是可选优化，也不是低风险 fallback。
- 任何依赖 doneflag polling 判定 DMA 完成、绕过 `hw_dma_fence()` / blocking wait、
  或把 `dma-wait-doneflag-poll phase=done` 当作“DMA 已正确完成”的测试证据，
  都必须标记为无效证据。
- doneflag / completion flag 最多只能作为辅助观测点，用来和 fence 返回、breadcrumb、
  DMA manager 状态做交叉对照；它不能驱动控制流前进。
- 当前主线以 fence / blocking retire 为准；Linux 路径的 DMA completion 必须经过
  `hw_dma_fence()` / blocking wait 方法。
- 如果后续为了定位硬件问题临时观察 doneflag，必须在 change/debug record 中明确写成
  “观测用途”，并补一轮不依赖 doneflag 的 control run。
- Linux host buffer 与 SPM DMA 继续遵守 page-chunk / bounce / `virt_to_phys` /
  completion flag PA 的 guardrail。

## 8. 日志与观测

- 主观测面是 guest 文件系统日志与 breadcrumb，不是 `uartlog`。
- `uartlog` 只用于 boot 活性、panic、manager verdict 辅助。
- `gdbserver --once` 的第一条 TCP 连接必须来自 GDB；不要用 `nc`、telnet、curl 或端口探测
  触碰 guest gdbserver 端口。
- live GDB 的 Ctrl-C/interrupt 不是必然可用的卡死现场采样手段。若目标进入 custom
  instruction、fence、MMIO 或其它硬件等待路径，GDB 可能不能返回 prompt，甚至会
  `Disconnected from target`。这类结果要记录为有效证据，不要伪造成调用栈缺失的测试失败。
- noTrace FPGA bitstream 不能在运行后事后读取 Rocket 内部 PC、最后退休 PC 或未退休 stalled
  instruction。若需要 PC/retire 级证据，必须在构建前加入 TracerV、AutoCounter 或定制
  ready-valid / busy / token / manager 状态观测点。
- 仅凭 `uartlog` 在 Linux boot 早期变慢或静默，
  不能直接判为异常；
  必须结合 `heartbeat.csv` 和是否已经进入用户态一起判断。
- 遇到卡点/报错时，先做静态 artifact 审计和代码阅读，再读 capture；不要一上来就扩大日志面。
- 优先使用：
  - `scripts/audit_pipeline_runtime_artifact.py`
  - `scripts/triage_prt_capture.py`
- rerun 前先跑：
  - `scripts/pairdummy_sbus128_workflow.sh debug-preflight`
- 不要在 runtime 内做前台阻塞 `sync`。
- 不要在热路径上继续堆高频文本 `write(O_APPEND)` 日志。
- 若需要细日志，只允许按 segment/stage/subbatch/page 等条件极窄开启。
- 当前唯一允许临时 overlay 的 fixed-profile 参数族是：
  - `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
- 即使使用 trigger-gated 短日志，也只允许单变量 rerun；
  同时打开多类高风险 probe 属于违规。
- 若 frontier 变化仅伴随 observability 变化，
  不能直接记成“卡点被穿过”；
  必须补 control rerun。

## 9. 记录

- 每一轮调试必须新建：
  `debug_records/<UTC timestamp>.md`
- 每一轮实际修改必须新建：
  `change_records/<UTC timestamp>.md`
- 不复用旧文件追加多轮内容。
- 主入口文档不允许再写回大时间线；历史必须进 `debug_records/` 或 `docs/archive/`。
