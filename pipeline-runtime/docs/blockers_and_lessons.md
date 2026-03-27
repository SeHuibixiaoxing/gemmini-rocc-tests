# Blockers And Lessons

## 0. Linux Boot 判定红线

必须遵守：

- Linux 启动阶段，只要没有明确报错，就不要把“长时间没有新的 UART 输出”判成新的卡点
- 这里的“明确报错”至少包括：
  - boot error
  - kernel panic
  - crash / reboot loop
  - 已进入用户态 workload 后再次停住
- 仅仅因为：
  - boot 很慢
  - `heartbeat.csv` 还在推进
  - UART 有较长静默窗口
  都不构成新的卡点结论

2026-03-26 的明确反例：

- `2026-03-26 14:39:22 UTC` 之后一度长期没有新的 boot 输出
- 但随后在 `2026-03-26 14:47:48 UTC` 继续推进到：
  - `running /etc/init.d/S99run`
  - `launching firemarshal workload run/command`
  - `[bertmini] method=ours2`

因此，后续凡是 Linux boot 阶段的静默窗口，都必须先按“继续等待”处理，而不是先记成新卡点。

## 1. 之前最深的 Linux/F2 卡点

在旧 runtime 上，最深卡点曾收敛到：

- `segment 0`
- `stage 0`
- pointwise OS path
- bias `config_ld` 周边

更精确地说，历史上曾观测到：

- `matmul-os-biascfg-pre-ld` 已打印
- `matmul-os-biascfg-post-ld` 未打印

但 `heartbeat.csv` 仍在推进。

## 2. 这轮重构与旧卡点的关系

这轮并没有直接证明旧卡点已经修好。

这轮实际完成的是：

- 去掉旧的 runtime-global topology/execution state 污染因素

所以新的结论应当是：

- 旧卡点需要在新 runtime 上重新验证
- 如果卡点仍在，更可能是 Gemmini/ReRoCC/DMA 路径本身问题，而不是旧全局状态复用

## 3. 日志经验

已经确认的经验：

- process log 不能单独判断卡点
- `printf` 缓冲会让“最后一条日志”不可信
- guest 输出应尽量 unbuffered
- 必要时可以补额外 filler log，把关键 marker 挤出缓冲
- 前面部分日志噪声可以减少，但关键路径必须更细、更密

## 4. FireSim/Runfarm 经验

- Linux 启动慢时要耐心等待
- heartbeat 前进不等于 kernel hang
- 一旦确认 run 已卡死，要及时停掉 runfarm
- 不要只信 manager exit code
- 要直接查 live `uartlog`、`heartbeat.csv`，必要时 SSH 到 run host

补充一条必须遵守的判断规则：

- Linux 启动阶段，只要没有明确报错，就不要把“长时间没有新 UART 输出”直接判成新的卡点
- 必须至少等到：
  - 明确出现 boot error / panic / crash
  - 或者已经超过历史同 workload 的正常慢启动窗口
  - 或者已经进入用户态 workload 后再次停住

2026-03-26 的一个反例：

- 本轮 rerun 在 `2026-03-26 14:39:22 UTC` 后一度长期没有新的 boot 输出
- 但随后在 `2026-03-26 14:47:48 UTC` 继续推进到了：
  - `running /etc/init.d/S99run`
  - `launching firemarshal workload run/command`
  - `[bertmini] method=ours2`
- 因此，这种 boot 静默窗口不能记录为真正的“新卡点”

## 5. baremetal 与 Linux 的使用原则

- 数值问题和接口问题，优先用 baremetal 快速缩小范围
- 真正的 guest-only 死锁，最后仍要回到 Linux/F2 证明
- 但 Linux/F2 启动贵，所以每次进 Linux 前都应该先把日志断点补充分

## 6. bias mvin 路径

之前已经做过：

- `mvin0` 到 `mvin3`
- `config_ld id=0` 到 `id=2`
- `sizeof_D/low_D` 对齐

这些修补曾经没有把旧卡点彻底推过去。

因此当前不应把问题简单归因成“只要换 bias mvin 就会好”。

## 7. 当前下一步

1. 在新 action-private runtime 上重新跑 baremetal 对照
2. 再跑 Linux/host closure
3. 最后重进 FireSim/F2

## 8. 这轮静态排查新增结论

- `matmul-os-biascfg-post-ld` 没打印这件事本身是可信的。
- 当前关键 marker 不是普通 `printf`，而是走 `write()`/`fflush()` 的 critical path；同时 runtime `main()` 也已经对 `stdout/stderr` 做了 `_IONBF`。
- 因此，如果 `pre-ld` 已经看到、`post-ld` 仍然看不到，不能再简单归因成“只是日志缓冲”。

- `gemmini_extended3_config_ld(..., id=2)` 在 Gemmini `LoadController` 内部不是慢路径。
- `LoadController` 对 `CONFIG_CMD` 的处理是直接写寄存器并 `cmd.ready := true`。
- 如果 CPU 真卡在这条指令上，更可能是它前面的 `Controller` / `ReservationStation` / ReRoCC `inst_q` / cfg credit 没把指令收进去，而不是 `LoadController` 自己在等 DMA。

- `gemmini_flush(0)` 不能当成“全局排干 Gemmini”理解。
- 从 `Controller.scala` 静态看，这条指令只走 `FLUSH_CMD` 特判，主要驱动 TLB / DMA flush 信号，不会像 `rr_fence(cfg)` 那样等待 manager-visible completion。
- 因此，软件里凡是把 `gemmini_flush(0)` 当成 drain 的地方，都需要单独复核语义。

- 当前 Linux runtime 的 pointwise path 和之前 baremetal 小诊断并不等价。
- Linux/runtime 路径是：
  - outer `config_ld(id=2)`，inner `mvin3`
- 之前 baremetal 小诊断是：
  - 先 `config_ld(id=2)`，随后又 `config_ld(default id=0)`，并用 `mvin0`
- 所以不能因为 baremetal 某个小例子过了，就直接推出 runtime 的 `mvin3/state2` 路径没有问题。

- `D_scale_factor` 的 C 类型和 `config_ld` 打包宏之间确实存在接口不一致：
  - `D_scale_factor` 是 `scale_acc_t`
  - `gemmini_extended3_config_ld()` 固定按 `scale_t` 打包
- 但对当前配置，硬件 `mvin_scale_acc_args = None`，acc-width bias mvin 实际走不缩放旁路。
- 所以这更像需要修的接口债，不像当前最深 stall 的主因。

- ReRoCC client 里存在一个独立静态 bug，已修：
  - 文件：`generators/rerocc/src/main/scala/client/Client.scala`
  - `InstructionSender` 在 `s_inst` 分支里错误地把第二个条件写成了 `xs1`，应为 `xs2`
- 这个 bug 会影响“只带 rs2、不带 rs1”的 ReRoCC 指令发包。
- 它不太像这次 `config_ld` stall 的主因，因为 Gemmini 常规指令基本都带 `rs1+rs2`，但它是一个真实协议缺陷，后续不能忽略。

## 9. 当前最可疑的静态方向

- 最可疑方向仍是“`config_ld(d)` 只是第一个暴露出来的被阻塞指令”，真正堵点在它前面的前端接收路径。
- 需要优先怀疑：
  - ReRoCC cfg credit / manager `inst_q`
  - Gemmini `Controller -> ReservationStation` 接收背压
  - runtime 在 pointwise 路径里插入的额外控制面指令是否把前端堆满

- 当前最值得带着去做下一轮动态验证的观察是：
  - pointwise 调用前，这条路径已经可能发出 `flush`、`spm_xlate_fault` 读取、`config_ex/st/ld_a/ld_b`
  - ReRoCC 默认 `inst_q` / cfg credit 只有 `4`
  - 如果任意一条前序指令没有及时 ack，后面的 `config_ld(d)` 就会在 CPU custom 指令处直接卡住

## 10. 2026-03-26 新 runtime 上的 live Linux/F2 结论

- 这轮 live run 已经越过历史最深边界：
  - `matmul-os-biascfg-post-ld` 已打印
  - 已进入 `matmul-inner -> sp_tiled_matmul_os()`
  - 已打印到 `matmul-os-biascfg-enter`

- 当前新的最深可见边界不再是 `config_ld(id=2)`。

- 远端 `uartlog` 在 `2026-03-26 14:18:28 UTC` 后文件大小固定为 `49055` 字节，而 `heartbeat.csv` 仍持续推进。

- `uartlog` 最后 128 字节的原始内容显示，最后一条日志停在半行中间，没有换行结束：
  - `... [gcrit] matmul-os-biascfg-state I=32 J=8 K=32 pad_I=0 pa`

- 因此当前 run 的“最深边界”应记录为：
  - `matmul-os-biascfg-state` 长 critical log 的写出过程中，或其紧邻位置
  - 不能再记成旧的 `pre-ld/post-ld` 边界

- 下一轮 Linux/F2 验证前，应先做两件事：
  - 把 deepest-path 的长 critical log 拆成多条更短的日志
  - 保留超短 raw marker，把第一条 `bias mvin3` 前后的断点继续压深

- 否则，即使真正的硬件卡点更靠后，也可能再次被 console/UART 写出阻塞掩盖。

## 11. 2026-03-26 新增静态结论：先排除 stale binary

- 这轮对当前本地 `rerocc_pipeline_runtime-linux` 做静态核对后，发现它一度仍然包含旧字符串：
  - `matmul-os-biascfg-ld-params`
  - `matmul-os-biascfg-state`
- 但源码里的 `gemmini.h` 已经改成 split markers：
  - `matmul-os-biascfg-ld-shape`
  - `matmul-os-biascfg-shape`
  - `matmul-os-biascfg-flags`
  - `matmul-os-biascfg-addrs`

直接证据：

- `gemmini.h` 时间戳晚于本地 `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
- 旧的 `rerocc-linux-tests/Makefile` 没有给 `pipeline-runtime` 对象文件声明 header 依赖
- 因此，像 `gemmini.h` 这种 header-only 改动可以被 `make` 静默漏编

修正：

- 已给 `rerocc-linux-tests/Makefile` 增加 `pipeline-runtime/include/*.h`、`include/*.h`、`rerocc-linux-tests/*.h` 依赖
- 现在 `make -n` 会在 header 更新后重新编 `prt_gemmini_adapter.o` 和最终 `rerocc_pipeline_runtime-linux`
- 本地重编后的 binary 已确认切换到新 split markers，不再包含旧的 merged `ld-params/state`

执行纪律：

- 下次看到 live guest 仍打印旧 deep marker 时，先不要继续猜 Gemmini/RoCC 卡点
- 先检查：
  - 本地 binary 是否比源码旧
  - staged overlay/image 里的 binary 是否真更新
  - FireMarshal 是否真的 rebuild/install 了新的 workload image

## 12. 2026-03-26 新增静态结论：host-init 校验必须更强

- 旧的 `host-init.sh` 校验过于宽松，只检查：
  - `[prt-marker]`
  - `[gemmini-phase]`
  - 少量早期字符串
- 这不足以发现“binary 仍是旧 biascfg marker 版本”

修正：

- 两条 Linux workload 路径的 `host-init.sh` 都已增加对当前 biascfg split markers 的校验
- 如果 binary 仍含旧的：
  - `matmul-os-biascfg-ld-params`
  - `matmul-os-biascfg-state`
  将在 host-init 阶段直接失败，而不是等到 F2 live run 才发现

- coupled-DMA 的 `host-init.sh` 还补齐了 `PIPELINE_RUNTIME_PROGRESS_PAD_BURST` 透传
- 否则即使用户要求 filler burst，coupled workload 也会静默用默认值 `0` 编译

## 13. 2026-03-26 新增静态结论：区分 manager/SSH 异常与 guest stall

- `2026-03-26 15:21:13 UTC` 这轮 `runworkload` 不能记成新的 runtime 卡点。
- 直接证据：
  - manager log：
    `sims/firesim/deploy/logs/2026-03-26--15-21-13-runworkload-57RFHK61GBWTW8NZ.log`
  - tmux pane log：
    `tmp/firesim-aws-f2/tmux/fs-bertmini-run-r3.pane.log`
  - 末尾报错是：
    - `paramiko.ssh_exception.SSHException: Error reading SSH protocol banner`
    - `fabric.exceptions.NetworkError: Error reading SSH protocol banner`
- 这说明：
  - 仿真 job 已经被 manager 拉起
  - 但 manager 后续轮询 run host 时，SSH/Paramiko 连接层先失败
  - 因此这轮没有形成新的 guest 深断点证据
- 执行纪律：
  - 后续看到这类 manager/fabric/SSH 报错，先归类为基础设施异常
  - 不要直接把它并入 `segment0/stage0/pointwise` 的 stall 时间线

## 14. 2026-03-26 新增修补：恢复 baremetal 快路径并补上 Linux-only xlate 发布

- baremetal 快路径之前的新阻塞点已经修掉：
  - `gemmini.h` 已补齐 `prt_gemmini_issue_bias_mvin0_debug()`
  - 因此前面 `host-init-explicit-interleaved.sh` / FireMarshal build 被
    `undefined reference to 'prt_gemmini_issue_bias_mvin0_debug'`
    卡住的问题已解除
- 本地验证：
  - `./host-init-explicit-interleaved.sh --target 2c2g2d`
    - PASS
  - `marshal build rerocc-lc-baremetal-coupleddma-explicit-interleaved-small.json`
    - PASS

- 这轮还补了一个 Linux runtime 的静态风险修补：
  - page table / PTE 在软件写完后，现在会显式执行一次发布栅栏
  - 位置：
    - `prt_spm_xlate_ctx_publish()`
    - `configure_action_spm_xlate()`
    - `prt_action_install_spm_context()`
    - 各个 `map/bind/unmap/unbind` PTE 写入点
- 这不是对当前 stall 的最终定论，但它修掉了一个真实的 Linux-only 风险：
  - CPU 刚写完 action-local PTE
  - 随后立刻让 Gemmini shared-spad walker 读取该 PT
  - 中间原先没有显式发布点

## 15. 当前静态收敛状态

- 目前仍然没有发现一个足以单独解释“首个 pointwise bias 路径卡死”的
  明显软件地址计算 bug。
- 当前更值得继续验证的两个方向是：
  - Linux runtime 与 baremetal runtime-style case 之间，在 pointwise issue 前
    是否还存在时序差异
    - 例如 pre-dispatch `gemmini_flush(0)` 的有无
  - Linux action-private xlate / PTBR / flush 时序是否仍有 guest-only 差异
- 因此后续顺序应继续保持：
  - 先用已恢复的 baremetal 快路径做更贴近 runtime 的对照
  - 再回 Linux/F2 取更深边界

## 16. 2026-03-26 新增修补：baremetal runtime-style 快速复现路径已对齐到当前 Linux pointwise 假设

- 为了避免继续拿“和 Linux runtime 不完全同构”的 baremetal case 做判断，
  这轮又补了两处关键对齐：
  - `host-init-explicit-interleaved.sh`
    - `REROCC_RUNTIME_STYLE_ONLY` 默认从 `0` 改到 `1`
    - 因此 baremetal / FireMarshal 默认会直接进入 runtime-style 复现路径
  - `rerocc_lc_resadd_explicit_interleaved.c`
    - `pointwise_matmul_issue_chunked_runtime_style()` 的激活从
      `NO_ACTIVATION` 改成 `RELU`
    - `run_pointwise_matmul_chunked_runtime_style_case()` 去掉了
      issue 前的预先 `gemmini_flush(0)`
    - 并补了一条明确 marker：
      - `CASE_TRACE ... runtime_style_skip_preflush=1 act=1`

- 这两点分别对应当前 Linux runtime 的两个已知前提：
  - 当前调试策略里，激活先统一按 `RELU` 处理
  - canonical pointwise fallback 路径在 issue 前不再主动 `gemmini_flush(0)`
- 为了继续消除 xlate 时序差异，baremetal helper 也已补齐：
  - 启用 xlate 前先 `fence rw, rw`
  - `cfg/range` 后显式 `rerocc_gemmini_spm_xlate_flush()`
  - 这样就与当前 Linux action-local xlate install 的
    `publish -> cfg -> range -> flush`
    顺序更一致

- 本地验证：
  - 直接 host-init 重编：
    - `./host-init-explicit-interleaved.sh --target 2c2g2d`
    - PASS
  - FireMarshal build：
    - log:
      `software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-explicit-interleaved-small-build-2026-03-26--16-37-34-79SYJN11ID3H0NUR.log`
    - 编译命令已确认带入：
      - `-DREROCC_RUNTIME_STYLE_ONLY=1`
    - PASS

- 这意味着后续如果 baremetal runtime-style 路径仍复现不出 Linux stall，
  结论会更可信：
  - 不是因为 baremetal 还停留在旧激活/旧 preflush/旧 case 入口
  - 而更可能是 Linux guest 专属的时序、页表发布、或 runtime 调用序差异

## 17. 目前仍保留的一个值得继续盯的 runtime-specific 差异

- Linux runtime 的 `cfg` 选择不是固定 `0`：
  - `rr_cfg_id_for_stage(stage_id, opcode_id)` 里
    - opcode `3` 走 lane `1`
    - 因此 `stage0/opcode3` 会落到 `cfg1`
- 当前 baremetal explicit-interleaved 复现程序仍固定使用：
  - `GEMMINI_CFG_ID = 0`
- 这通常不应该改变功能语义，但如果后续 baremetal 仍无法逼近 Linux stall，
  这个差异值得作为下一轮对齐项继续验证。

## 18. 2026-03-27 新增结论：focused baremetal 已经成为当前 stall 的真实复现入口

- 当前最有价值的 baremetal case 不再是 omnibus suite，而是专门对齐 Linux
  `segment0/stage0/pointwise` 首个 chunk 的 focused case：
  - wrapper:
    `bareMetalC/learn-gemmini/rerocc_lc_pointwise_stage0_runtime_interleaved_focus.c`
  - body:
    `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
  - workload:
    `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus.json`

- 这个 focused case 已对齐到当前 Linux 假设：
  - `stage=0`
  - `opcode=3`
  - `cfg=1`
  - pointwise fallback `WS -> OS`
  - `I=256 / J=128 / K=256` 的 runtime-style chunked path 中，
    只取首个 `oc_beg=0 / oc_tile=64`
  - interleaved shared-spad alias
  - `bias != NULL`
  - `act = RELU`
  - issue 前不做额外 `gemmini_flush(0)`

- 已确认的一条关键静态对齐结论：
  - 当前 runtime 的默认 conv 激活和输出缩放在
    `prt_runtime.c` 中分别固定为：
    - `act = RELU`
    - `output_scale = 1.0f`
  - 因此 focused baremetal 当前使用的
    `RELU + ACC_SCALE_IDENTITY`
    不再是一个额外的 runtime 差异源

- F2 上已经拿到一次 focused 复现：
  - 结果目录：
    `sims/firesim/deploy/results-workload/2026-03-27--01-30-14-rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus-f2-rerocc-baremetal-pointwise-stage0-focus`
  - 最后可见 guest 断点：
    - `CASE_PROGRESS ... phase=focused_chunk_issue_begin`
    - `matmul-nn-stride-auto-enter`
  - 随后 host 报：
    - `Simulator deadlock detected at target cycle 0. Terminating.`

- 这说明：
  - 当前 stall 已经不需要再依赖 Linux boot 才能逼近
  - baremetal focused case 已经足够接近当前 pointwise issue 路径

## 19. 2026-03-27 新增修补：deepest path 日志继续细分，且本地 artifact 已刷新

- 为了避免下一轮再次只看到一个“大日志入口”就停住，
  这轮把以下入口都拆成了更细的 raw marker + 短 phase log：
  - `matmul-nn-stride-auto`
    - `pre/post shape`
    - `pre/post addrs`
    - `pre/post flags`
  - `matmul-auto`
    - `pre/post shape`
    - `pre/post addrs`
    - `pre/post flags`
    - `pre/post padded`
    - `pre/post tiles`
    - `pre/post mode`
  - `matmul-config`
    - `shape`
    - `strides`
    - `flags`
  - `matmul-outer`
    - `shape`
    - `flags`

- 本地已确认这些新 marker 已进入 focused baremetal 二进制：
  - binary:
    `build/bareMetalC/rerocc_lc_pointwise_stage0_runtime_interleaved_focus-baremetal`
  - 校验方式：
    - `strings ... | rg 'matmul-(nn-stride-auto|auto|config|outer)'`
  - 结果：
    - 新增 `pre/post-*` raw marker 全部可见
    - 新增 split `phase` 字符串全部可见

- FireMarshal artifact 也已刷新：
  - build log:
    `software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus-build-2026-03-27--01-48-55-2OFHZOZ6WJLN73M2.log`
  - install log:
    `software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-pointwise-stage0-focus-install-2026-03-27--01-49-22-8Z72LHDU1UBWYQZM.log`

- 因此下一轮如果再次拿到 F2 容量，优先直接重跑这个 focused workload，
  不要再怀疑“源码改了但镜像里还是旧 marker”。

## 20. 2026-03-27 新增修补：补了真正最小的 `mvin3 + alias PTW` focused baremetal

- 之前已有的 `run_bias_mvin_linux_first_tile_case()` 虽然名字里写了
  `bias mvin`，但它实际 issue 的是：
  - `prt_gemmini_issue_bias_mvin0_debug()`
  - 不是当前 pointwise stall 路径里的 `mvin3 / LOAD3_CMD`

- 因此它不能单独证明：
  - `config_ld(id=2)` 没问题
  - `shared-spad alias + PTW + mvin3` 没问题
  - 或者 `bias channel(id=2)` 没问题

- 这轮新增了一个只针对当前卡点的最小 baremetal case：
  - wrapper:
    `bareMetalC/learn-gemmini/rerocc_lc_bias_mvin3_runtime_alias_focus.c`
  - body:
    `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
  - workload host-init:
    `rerocc-baremetal-tests-coupleddma/workload/host-init-bias-mvin3-focus.sh`
  - workload json:
    `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-bias-mvin3-focus.json`

- 这个新 case 的刻意约束：
  - 只保留 interleaved bias alias region
  - 仍使用 runtime-style 的：
    - `cfg=1`
    - `opcode=3`
    - `config_ex/st/ld0/ld1/ld2`
    - `runtime_skip_preflush=1`
  - 然后直接 issue：
    - `prt_gemmini_issue_bias_mvin3_debug()`
  - 不再经过完整 `tiled_matmul_nn_stride_auto -> tiled_matmul_auto -> tiled_matmul_outer -> sp_tiled_matmul_os`
    链路

- 已完成的本地验证：
  - 新 baremetal target 已编译通过：
    `build/bareMetalC/rerocc_lc_bias_mvin3_runtime_alias_focus-baremetal`
  - 对应 workload binary 也已生成：
    `rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_bias_mvin3_runtime_alias_focus.riscv`
  - FireMarshal install 已完成：
    `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-bias-mvin3-focus.json`
  - FireMarshal install log：
    `software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-bias-mvin3-focus-install-2026-03-27--02-24-30-49IW411JXQ7ZNN7E.log`
  - FireSim runtime config 已补：
    - `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_bias_mvin3_focus.yaml`
  - `strings` 已确认以下 marker 进入 binary：
    - `bias_mvin3_runtime_alias_focus`
    - `xlate-fault-pre`
    - `pre-issue-mvin3`
    - `post-issue-mvin3`
    - `matmul-os-bias-mvin3-debug-enter`

- 目前最值得优先验证的静态假设变成：
  - 如果这个最小 case 也卡死：
    根因更偏向 `LOAD3_CMD / bias channel / alias PTW frontend`
  - 如果这个最小 case 不卡死：
    根因更偏向 pointwise matmul 外层路径里，
    例如：
    - `tiled_matmul_*` 的前置 config 顺序
    - first-tile 外层状态
    - 或 `matmul` 链路比单发 `mvin3` 多出来的其他命令交互

- 这一轮继续静态扫过 `LOAD3/state2` 后，新结论是：
  - 真正对 `state_id=2` 有独立逻辑的硬件位置非常少
  - 目前确认到的主要只剩：
    - `LoadController.scala`
      - `LOAD3_CMD -> load_state_id = 2`
      - 使用 state2 的 `stride / scale / shrink / block_stride / pixel_repeat`
    - `ReservationStation.scala`
      - `LOAD3_CMD` 进入 load queue
      - `state_id=2` 的 `ld_block_strides / ld_pixel_repeats`
      - 地址重叠/依赖范围按 state2 配置推导
  - 其余关于 `LOAD3` 的地方，当前更多是在：
    - `LoopMatmul.scala`
    - `LoopConv.scala`
    里生成这条指令，而不是额外实现一套新的 load backend

- 这意味着下一次实验的解释力会更强：
  - 若最小 `mvin3` case 失败：
    可优先收缩到
    `LoadController + ReservationStation + shared-spad alias/PTW`
  - 若最小 `mvin3` case 成功：
    更像是完整 pointwise `matmul` 外层 issue 序列与这些状态交互出错

## 21. 2026-03-27 新增修补：补了更深一层的 `single inner OS tile` focused baremetal

- 新的静态结论：
  - 之前的 full focused pointwise case 表面上是：
    - `tiled_matmul_nn_stride_auto(PW_I=256, dim_J=64, PW_K=256, ...)`
  - 但按当前 `gemmini_params.h`：
    - `DIM=8`
    - `ACC_ROWS=2048`
    - `BANK_NUM=4`
    - `BANK_ROWS=8192`
  - `tiled_matmul_auto()` 会把这组参数收敛成：
    - `tile_I=32`
    - `tile_J=8`
    - `tile_K=32`
  - 并且这里有个很关键的事实：
    - `I0=1`
    - `J0=1`
    - `K0=1`
  - 所以 full focused pointwise case 在当前硬件参数下，本质上就是：
    - 单次 `tiled_matmul_outer()`
    - 配完 `config_ex/st/ld0/ld1/ld2`
    - 然后只打一发 `sp_tiled_matmul_os(...)`

- 这说明什么：
  - 之前的最小 `bias_mvin3` focused case 虽然能切掉大量噪声，
    但它仍然没有覆盖：
    - `B` 的 `mvin2`
    - `A` 的 `mvin0`
    - 第一发 `preload`
    - 第一发 `compute_preloaded`
  - 所以如果最小 `mvin3` case 过了，问题仍然可能在：
    - `sp_tiled_matmul_os` inner tile 本体
    - 而不是更外层的 `auto` / multi-tile 调度

- 因此新增了一个新的 focused baremetal：
  - wrapper:
    `bareMetalC/learn-gemmini/rerocc_lc_pointwise_os_inner_runtime_alias_focus.c`
  - body:
    `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
  - 入口名：
    `pointwise_os_inner_runtime_alias_focus`
  - 这个 case 的策略是：
    - 保留 runtime-style 的 shared-spad alias
    - 保留 runtime-style 的
      - `cfg=1`
      - `opcode=3`
      - `config_ex/st/ld0/ld1/ld2`
      - `runtime_skip_preflush=1`
    - 但不再走 `tiled_matmul_nn_stride_auto -> tiled_matmul_auto`
    - 也不再依赖 outer 自动算 tile
    - 直接手动调用单个：
      - `sp_tiled_matmul_os(...)`
    - 参数固定成当前 full focused 实际落下来的首个也是唯一一个 inner tile：
      - `I=32`
      - `J=8`
      - `K=32`
      - `pad_I=0`
      - `pad_J=0`
      - `pad_K=0`
      - `repeating_bias=1`
      - `act=RELU`

- 新 case 的作用：
  - 如果这个 `single inner OS tile` case 也卡死：
    根因会更像：
    - `sp_tiled_matmul_os` 内部的
      - bias `mvin3`
      - B `mvin2`
      - A `mvin0`
      - preload / compute
      链路之一
  - 如果这个 case 不卡死：
    更像是：
    - outer config / outer->inner 衔接
    - 或 `tiled_matmul_auto` / `tiled_matmul_outer` 的外围逻辑

- 已完成的本地验证：
  - 新 baremetal target 已编译通过：
    `build/bareMetalC/rerocc_lc_pointwise_os_inner_runtime_alias_focus-baremetal`
  - `strings` 已确认以下 marker 进入 binary：
    - `pointwise_os_inner_runtime_alias_focus`
    - `sp-tiled-matmul-os-single-inner`
    - `focus-inner-pre-call`
    - `focus-inner-post-call`
    - `matmul-os-biascfg-first-iter-enter`
    - `matmul-os-pre-b-mvin2`
    - `matmul-os-pre-preload0`
  - 对应 workload binary 已生成并安装：
    - workload binary:
      `rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_pointwise_os_inner_runtime_alias_focus.riscv`
    - deploy workload:
      `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-os-inner-focus.json`
    - install log:
      `software/firemarshal/logs/rerocc-lc-baremetal-coupleddma-pointwise-os-inner-focus-install-2026-03-27--02-42-42-J8WLAHYEF9CVCDUP.log`
  - FireSim runtime config 已补：
    - `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_os_inner_focus.yaml`

- 当前实验优先级建议：
  1. 先跑最小 `bias_mvin3_runtime_alias_focus`
  2. 若它通过，再跑新的 `pointwise_os_inner_runtime_alias_focus`
  3. 只有当这两个都通过时，再回到 full focused pointwise case

- 当前基础设施状态：
  - 以后只使用 `f2.6xlarge`
  - 不要再切到 `f2.12xlarge` 作为所谓 fallback
  - 当前账号 `Running On-Demand F instances` 配额是 `32` vCPU，而 `f2.12xlarge` 需要 `48` vCPU，所以会稳定触发 `VcpuLimitExceeded`
  - FireSim 上层会把这类错误也打印成 "insufficient capacity"，所以必须看原始 `launchrunfarm` 日志

## 22. 2026-03-27 新增静态结论：bias accumulator 语义与 direct-OS issue 语义需要分开看

- 本轮重新对了以下路径：
  - `sp_tiled_matmul_os()` in
    `generators/gemmini/software/gemmini-rocc-tests/include/gemmini.h`
  - `ExecuteController.scala`
  - `MeshWithDelays.scala`
  - `PE.scala`
  - `LoopMatmul.scala`
  - `ReservationStation.scala`

- 先确认一件重要事实：
  - `D_sp_addr_start = 1 << 31`
  - `C_sp_addr_start = 3 << 30 | ...`
  - 这两者不是“完全不同的一块物理 scratchpad 区域”
  - 它们共享同一个 accumulator row index，只是 local addr 的 metadata 位不同：
    - `D_sp_addr_start` 对应 acc 地址且 `accumulate=0`
    - `C_sp_addr_start` 对应 acc 地址且通常 `accumulate=1`

- 这意味着当前 runtime-style OS helper 里：
  - `mvin3` 把 bias 写入 accumulator row
  - 后续如果 `out_sp_addr` 保持 `accumulate=1`
  - bias 也可能是在 accumulator 写回阶段通过 RMW 累加进去
  - 而不是必须依赖 `preload` 的 `BD` 源操作数去显式读 bias

- 所以，单凭这一点，不能把
  - `gemmini_extended_preload(GARBAGE_ADDR, out_sp_addr, ...)`
  - 直接判成 bug

- 但另一条路径仍然非常可疑，而且更像“真卡死根因”：
  - 当前硬件 `ExecuteController` 对 direct `PRELOAD + COMPUTE` 两条 exec 指令的 lane 分配很敏感
  - direct software path 和 `LoopMatmul` 内建 loop-unroller path 不是同一种 issue 语义
  - 不能直接拿 `LoopMatmul.scala` 里的
    - `preload(B, C)`
    - `compute(A, GARBAGE)`
    去简单否定当前 helper 的
    - `preload(GARBAGE, C)`
    - `compute(A, B)`

- 当前更准确的结论是：
  - direct software OS path 的真正 lane 绑定，需要靠第一发 `preload/compute` 的原始 `rs1/rs2` 编码和 local addr metadata 一起看
  - 否则只靠 C 代码表面参数，无法确认：
    - B 最终走的是哪一路
    - D/zeros 最终走的是哪一路
    - `accumulate` / `read_full` 位在 guest 端编码是否符合当前 RTL 期待

- 为了下一轮一次性把这个问题看清，我已经在 `gemmini.h` 里补了仅针对 deepest-path 第一拍的指令级 debug：
  - 新增 helper：
    - `prt_gemmini_debug_localaddr()`
    - `prt_gemmini_issue_preload_debug()`
    - `prt_gemmini_issue_compute_preloaded_debug()`
    - `prt_gemmini_issue_compute_accumulated_debug()`
  - 这些 helper 会打印：
    - raw `rs1/rs2`
    - `is_acc`
    - `accumulate`
    - `read_full`
    - `acc_row`
    - `sp_row`
  - 当前只替换了 `sp_tiled_matmul_os()` 中：
    - 第一发 `preload`
    - 第一发 `compute_preloaded`
    - 第一发 `compute_accumulated`
  - 其余迭代仍保持原实现，避免日志量失控

- 本地编译验证：
  - 重新编过：
    `bareMetalC/rerocc_lc_pointwise_os_inner_runtime_alias_focus-baremetal`
  - 编译通过，说明新增 debug helper 没把 `gemmini.h` 编坏

- 下一轮实验解释方式：
  - 如果最小 `bias_mvin3_runtime_alias_focus` 卡死：
    优先看 `LOAD3 / alias PTW / load-state2`
  - 如果它通过，但 `pointwise_os_inner_runtime_alias_focus` 卡死：
    直接用新增 `preload/compute` raw-encoding 日志判断
    first inner tile 的 direct issue 语义是否和当前 RTL 不匹配

## 23. 2026-03-27 新增更深 focused baremetal：只覆盖首个 direct-issue pair

- 由于当前最深疑点已经继续收缩到：
  - `bias mvin3`
  - `B mvin2`
  - `A mvin0`
  - 第一发 `preload`
  - 第一发 `compute_preloaded`
- 所以又新增了一个比 `pointwise_os_inner_runtime_alias_focus` 更窄的 baremetal：
  - wrapper:
    `bareMetalC/learn-gemmini/rerocc_lc_pointwise_os_first_pair_runtime_alias_focus.c`
  - body:
    `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
  - 入口名：
    `pointwise_os_first_pair_runtime_alias_focus`

- 这个 case 保留：
  - runtime-style shared-spad alias
  - `cfg=1`
  - `opcode=3`
  - `config_ex/st/ld0/ld1/ld2`
  - `runtime_skip_preflush=1`
- 但不再执行整个 `sp_tiled_matmul_os()` inner tile
- 它只顺序 issue：
  - `mvin3(bias)`
  - `mvin2(B)`
  - `mvin0(A)`
  - 第一发 `preload(GARBAGE_ADDR, C_acc_addr)`
  - 第一发 `compute_preloaded(A_sp_addr, B_sp_addr)`
  - 然后 `gemmini_wait_managed_runtime_style()`

- 这个 case 的价值：
  - 如果它卡死：
    根因已经被压缩到上面 5 条指令之一
  - 如果它不死，而 `pointwise_os_inner_runtime_alias_focus` 卡死：
    下一层优先怀疑：
    - 第二发及之后的 compute
    - 或 inner tile 内部循环推进本身

- 已完成的本地验证：
  - baremetal target 已编译通过：
    `bareMetalC/rerocc_lc_pointwise_os_first_pair_runtime_alias_focus-baremetal`
  - workload binary 已生成：
    `rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_pointwise_os_first_pair_runtime_alias_focus.riscv`
  - `strings` 已确认以下 marker 进入 binary：
    - `pointwise_os_first_pair_runtime_alias_focus`
    - `matmul-os-preload-debug-enter`
    - `post-compute-preloaded0`
  - FireSim source/deploy config 已补：
    - `rerocc-baremetal-tests-coupleddma/workload/host-init-pointwise-os-first-pair-focus.sh`
    - `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-os-first-pair-focus.json`
    - `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-os-first-pair-focus.json`
    - `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_os_first_pair_focus.yaml`

## 24. 2026-03-27 新 Linux/F2 最深边界已经继续推进到第一发 `MVIN3`

- 本轮 live Linux/F2 已经明显越过旧的 `biascfg config_ld` 边界：
  - Linux boot 完成
  - FireMarshal workload 启动
  - `bertmini` runtime 启动
  - `segment=0`
  - `stage=0`
  - `pointwise`
  - `sp_tiled_matmul_os()`
  - 第一发 bias `mvin3`

- 当前最新最深可见 marker：
  - `[graw] bm0c0`
  - `[graw] bm0ce`
  - `[graw] bm0ca`

- 当前缺失 marker：
  - `[graw] bm0cb`

- 这组 marker 在 `gemmini.h` 中的意义已经确认：
  - `bm0ca` 是实际发出 `MVIN3` custom 指令之前
  - `bm0cb` 是从这条指令返回之后

- 因此这轮新的工作结论是：
  - 不应再把问题表述成“卡在 `config_ld(id=2)`”
  - 当前更准确的描述是：
    “Linux/F2 在 stage0 pointwise 首个 inner OS tile 的第一发 bias `MVIN3`
    issue 处没有返回”

- 同时，本轮 live host 的 `heartbeat.csv` 在 `bm0ca` 之后仍持续推进。
  这说明：
  - 不能再简单归因成 UART/stdio 缓冲
  - 至少从 guest 侧观察，真正最深边界已经压到 `MVIN3 issue`

## 25. 2026-03-27 新静态结论：`vpage 0` 仍值得验证，但暂时没有代码级铁证

- 这轮重新静态查看了：
  - `pipeline-runtime/src/prt_page_table.c`
  - `gemmini/FrontendTLB.scala`
  - `gemmini/SpmPageTableWalker.scala`
  - `gemmini/GemminiCoupledDMA.scala`

- 当前看到的是：
  - 软件页表允许 `ctx->free_vpages[0].start = 0`
  - `prt_spm_bind_vpages_ctx()` 也允许直接写 `pte[0]`
  - Gemmini shared-spad frontend 用的是：
    - `spmOffset = reqVaddr - range_base`
    - `spmVpn = spmOffset >> pageShift`
  - `SpmPageTableWalker` 直接取：
    - `reqAddr = ptbr + (vpn << 3)`

- 所以到目前为止：
  - 还没有看到明确的 “vpn=0 / pte[0] / ptbr+0” 特判或保留槽位
  - 但 Linux 当前 case 和之前 baremetal focused case 的一个关键未覆盖差异仍然存在：
    - 之前 baremetal bias alias 放在非零 vpage
    - 当前 Linux runtime bias alias 落在 action-local `vpage 0 / offset 0`

- 结论：
  - `vpage 0` 不能因为静态没看到特判就直接排除
  - 但也不能只凭直觉就直接改 runtime 主逻辑
  - 最好的下一步仍然是补一个“完全对齐 Linux 当前怀疑点”的 focused baremetal

## 26. 2026-03-27 新增 focused baremetal：`bias mvin3 + alias PTW + vpage0/offset0`

- 这轮新增了一个比原 `bias_mvin3_runtime_alias_focus` 更贴近 Linux 当前 case 的 focused baremetal：
  - wrapper:
    `bareMetalC/learn-gemmini/rerocc_lc_bias_mvin3_runtime_alias_vpage0_focus.c`
  - host-init:
    `rerocc-baremetal-tests-coupleddma/workload/host-init-bias-mvin3-focus-vpage0.sh`
  - workload json:
    `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-bias-mvin3-focus-vpage0.json`
  - FireSim runtime config:
    `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_bias_mvin3_focus_vpage0.yaml`

- 这个新 case 与之前版本的唯一区别就是：
  - bias alias region 现在强制落在：
    - `vpage_base = 0`
    - `page_offset = 0`

- 它的目的不是扩大覆盖面，而是专门回答一个非常具体的问题：
  - “如果把 baremetal 的 bias alias 精确压到 Linux 当前的 `vpage0/offset0` 形态，
    第一发 runtime-style `MVIN3` 还会不会卡住？”

- 本地已经完成的验证：
  - 新 baremetal target 已编译通过：
    `build/bareMetalC/rerocc_lc_bias_mvin3_runtime_alias_vpage0_focus-baremetal`
  - `strings` 已确认 marker 进入 binary：
    - `bias_mvin3_runtime_alias_vpage0_focus`
    - `focused_vpage0_before_init_pw_chunk_bias`
    - `focused_vpage0_after_init_pw_chunk_bias`
    - `pre-issue-mvin3`
    - `post-issue-mvin3`
    - `matmul-os-bias-mvin3-debug-enter`

- 因此下一轮如果继续走 baremetal，应优先跑这个 `vpage0` case。

## 27. 2026-03-27 FireMarshal/FireSim 接线经验：新 baremetal workload 不能只做 install

- 这轮第一次把
  `rerocc-lc-baremetal-coupleddma-bias-mvin3-focus-vpage0.json`
  安装进 FireSim 之后，`infrasetup` 失败了。

- 失败点不是 FireSim manager，也不是 FPGA host：
  - manager 在 rsync workload binary 时找不到
    `rerocc_lc_bias_mvin3_runtime_alias_vpage0_focus.riscv`
  - 原始报错是：
    `rsync: [sender] link_stat ... No such file or directory`

- 根因是：
  - 新 workload `install` 只把 workload json 接进了
    `deploy/workloads`
  - 但 source workload 目录下对应的 `.riscv` binary 还没有先物化出来
  - 对这个 baremetal 流程，单独 `marshal install ...json` 不足以保证
    binary 已存在

- 当前可复用的修正动作：
  - 先跑对应 workload 的 `host-init-*.sh`
  - 确认 source workload 目录下已经出现目标 `.riscv`
  - 再重跑 `launchrunfarm -> infrasetup -> runworkload`

- 这条经验对后续所有新加 baremetal focused case 都成立：
  - 不能只看 `deploy/workloads/*.json` 已经存在
  - 必须同时检查 source workload 目录下的 `.riscv`

## 28. 2026-03-27 新动态结论：`vpage0/offset0 + alias PTW + 首发 bias MVIN3` 已被 focused baremetal 排除

- 这轮在 F2 上实际跑通了新的 focused baremetal：
  - runtime config:
    `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_bias_mvin3_focus_vpage0.yaml`
  - 结果目录：
    `sims/firesim/deploy/results-workload/2026-03-27--06-09-20-rerocc-lc-baremetal-coupleddma-bias-mvin3-focus-vpage0-f2-rerocc-baremetal-bias-mvin3-focus-vpage0/`

- 关键动态证据已经完整跨过了之前 Linux/F2 的最深怀疑点：
  - `CASE_TRACE ... pre-issue-mvin3`
  - `[graw] bm0ce`
  - `[graw] bm0ca`
  - `[graw] bm0cb`
  - `CASE_TRACE ... post-issue-mvin3`
  - `CASE_TRACE ... post-wait`
  - `CASE_RESULT ... PASS`
  - `ALL_TESTS_PASS`
  - `*** PASSED *** after 1994741177 cycles`

- 因此当前可以明确排除的不是“某个模糊大类问题”，而是这个非常具体的组合：
  - action-local `vpage 0`
  - `page_offset = 0`
  - alias PTW 已安装
  - 第一发 runtime-style bias `mvin3`
  - `cfg=1`
  - `opcode=3`

- 这说明：
  - 之前 Linux live run 最后停在 `bm0ca`，并不能推出“硬件一定卡死在第一发 `mvin3` 本身”
  - 当前更可能是：
    - Linux guest 专属的后续 pointwise 路径差异
    - 或者 `mvin3` 之后更深一层的调用链/同步路径

- 因而下一轮动态策略应该切回 Linux/F2，而不是继续围绕 `vpage0 + first mvin3` 重复 baremetal：
  - 保留当前 gemmini 侧 `bm0c*` 和 `matmul-os-*` raw marker
  - 继续把 runtime 侧的短 marker 压深到：
    - `pointwise subcall enter/return`
    - `pointwise inner pre/post matmul call`
    - `pointwise postcall state`
    - `conv-sync fence/release`

- 这轮 run 结束后已经及时回收 runfarm：
  - instance: `i-01dea7ab4ff040612`
  - `terminaterunfarm --forceterminate` 已执行
  - EC2 状态已确认不再处于 `running`

## 29. 2026-03-27 新 Linux/F2 复现结论：加深 runtime marker 后，边界仍稳定停在 `bm0ca`

- 这轮先做了三件准备，再重跑 Linux/F2：
  - 在 `prt_gemmini_adapter.c` 的 pointwise runtime 路径加了更深 raw marker：
    - `conv-sync-pointwise-subcall-enter/return`
    - `pointwise-inner-pre/post-matmul-call`
    - `pointwise-inner-pre/post-postcall-state`
    - `conv-sync-pointwise-fence-begin`
    - `conv-sync/conv-nb pointwise release-begin/end`
  - `rerocc-linux-tests/workload/host-init.sh` 增加了对这些新 marker 的 binary 校验
  - `scripts/firemarshal-tmux-run.sh` 增加了
    `PIPELINE_RUNTIME_PROGRESS_HOT` 和
    `PIPELINE_RUNTIME_PROGRESS_PAD_BURST`
    的环境透传，避免 tmux 里的 FireMarshal build 静默丢失日志配置

- 这轮本地与 deploy 准备都已确认成功：
  - `host-init overlay stage PASS`
  - `strings` 已确认新 raw marker 进入
    `build/rerocc-linux-tests/rerocc_pipeline_runtime-linux`
  - FireMarshal workload
    `rerocc-lc-linux-coupleddma-bertmini-pipeline-runtime.json`
    已重新 `build + install`

- 新 Linux/F2 live run 的关键推进顺序已经看到：
  - Linux boot 完成
  - runtime init 进入
  - `validate-artifacts` 完成
  - `load-model-bin` 开始
  - `segment=0`
  - `stage=0`
  - `conv-sync ... dispatch=pointwise`
  - `conv-sync-pointwise-subcall-enter`
  - `pointwise-inner-precall`
  - `pointwise-inner-pre-matmul-call`
  - `matmul-auto -> tiled -> matmul-inner`
  - `matmul-os-biascfg-*`
  - `bm0c0`
  - `bm0ce`
  - `bm0ca`

- 这轮仍然没有看到：
  - `bm0cb`
  - `pointwise-inner-post-matmul-call`
  - `conv-sync-pointwise-subcall-return`

- 并且这个边界是稳定的，不是“只差一点点时间”：
  - 在 `bm0ca` 已打印后，再额外等待约 30 秒
  - `uartlog` 末尾仍停在 `bm0ca`
  - 同时 `heartbeat.csv` 继续从约 `10.83B` 推进到 `11.42B` 再到 `11.78B` target cycles

- 因此当前最稳的动态结论是：
  - 新增 runtime raw marker 已经生效
  - Linux/F2 的最深边界仍稳定停在“第一发 bias `MVIN3` custom 指令发出之前/处”
  - 但 focused baremetal 已证明“`vpage0/offset0 + alias PTW + first runtime-style mvin3` 本身”并不会单独复现这个问题

- 这意味着当前剩余嫌疑更集中在 Linux runtime 专属差异，而不是 baremetal 已覆盖过的最小子路径：
  - 同一 scope 内更长的前序控制面序列
  - `conv-sync -> oc-split -> pointwise-chunk` 这一层调用与 drain/fence 时序
  - 或者 first `MVIN3` issue 前已有某条 ReRoCC/Gemmini 前端状态被污染/占满

- 执行纪律：
  - 这轮已经拿到新的有效边界，runfarm 已及时回收：
    - instance: `i-0e4725fc87a4c770b`
    - `terminaterunfarm --forceterminate` 已执行
    - EC2 已确认不再处于 `running`

## 30. 2026-03-27 新静态结论与下一轮 focused baremetal：Linux stage0 artifact 自洽，下一步补高 VA 基址

- 这轮重新对了 Linux stage0 的 pipeline artifact 与 runtime 地址生成：
  - pipeline mapping 中 stage0 的关键字段是：
    - `execBaseVPage: 0`
    - `localSpmTensorAddrList: [0, 1024, 66560, 132096]`
    - `localSpmFirstVPageList: [0, 1, 65, 129]`
    - `localSpmPageCountList: [1, 64, 64, 64]`
  - runtime 中：
    - `stage_tensor_exec_addr()` 用
      `alias_base + exec_base_vpage * page_bytes + local_spm_tensor_addr`
    - `stage_prepare_exec_views()` 绑定页表时用
      `exec_base_vpage + local_spm_first_vpage`

- 对 stage0 这组实参来说，上面两条是自洽的，不存在“地址算到一页、页表却绑到另一页”的静态错配：
  - bias:
    `addr = alias_base + 0`, `vpage = 0`
  - weights:
    `addr = alias_base + 1024`, `vpage = 1`
  - input:
    `addr = alias_base + 66560`, `vpage = 65`
  - output:
    `addr = alias_base + 132096`, `vpage = 129`

- 同时也静态排除了一个常见误判：
  - stage0 虽然 `acc_util=2`
  - 但 runtime 的 `oc-split` 实现是顺序 issue：
    - tile0 -> `mgr0`
    - tile1 -> `mgr1`
  - 并不是两个 manager 同时并发进入同一个 stage0 first tile
  - 所以当前 `bm0ca` 卡点不能直接归因成“stage 内双 manager 并发争抢同一 cfg”

- 这轮剩余最像 Linux-only 的差异主要只剩两层：
  - Linux action-private alias window 的高位 VA 基址
  - Linux PTBR/PTE 的实际物理分配路径

- 因而新增了下一轮更贴近 Linux 的 focused baremetal：
  - wrapper:
    `bareMetalC/learn-gemmini/rerocc_lc_pointwise_stage0_runtime_highva_vpage0_focus.c`
  - host-init:
    `rerocc-baremetal-tests-coupleddma/workload/host-init-pointwise-stage0-highva-vpage0-focus.sh`
  - workload json:
    `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-stage0-highva-vpage0-focus.json`
  - FireSim runtime config:
    `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_stage0_highva_vpage0_focus.yaml`

- 这个新 case 与原 `pointwise_stage0_runtime_interleaved_focus` 的区别只在“把 runtime 实参压得更像 Linux”：
  - `REROCC_SHARED_SPAD_XLATE_RANGE_BASE = 0x3f9ce22000`
  - `PW_VADDR_PAGE_OFFSET = 0`
  - `PW_CHUNK_BIAS_VPAGE = 0`
  - `PW_B_VPAGE = 1`
  - `PW_A_VPAGE = 65`
  - `PW_C_VPAGE = 129`

- 它要回答的问题非常具体：
  - 如果把 baremetal focused case 的 alias base 也抬到 Linux 风格的高 VA，
    并且把 stage0 的 `vpage` 布局精确压成 `0/1/65/129`，
    当前 first-tile pointwise path 会不会更稳定地逼近或复现 `bm0ca` 边界？

## 31. 2026-03-27 新 F2 focused baremetal 结论：高 VA + `0/1/65/129` 能逼近 `bm0ca`，但不会稳定卡死在 `bm0ca`

- 这轮先补齐了 focused baremetal 的 FireSim 接线缺口：
  - 新增 deploy workload：
    `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-highva-vpage0-focus.json`
  - 新增 deploy workload 目录：
    `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-highva-vpage0-focus/README`
  - 本地 `host-init-pointwise-stage0-highva-vpage0-focus.sh` 已成功生成：
    `rerocc_lc_pointwise_stage0_runtime_highva_vpage0_focus.riscv`

- 同时确认了 FireSim 运行这套目标时不能再用 sample 配置：
  - `sample_config_hwdb.yaml` 不包含
    `firesim_gemmini_rerocc_globalnoc_coupleddma_small_10mhz`
  - 正确组合是：
    - `sims/firesim/deploy/config_hwdb.yaml`
    - `sims/firesim/deploy/config_build_recipes_f2_gemmini_rerocc_globalnoc_coupleddma_10mhz.yaml`

- 这轮 focused baremetal F2 run 的真实推进顺序已经看到：
  - `CASE_START ... xlate_base=0x3f9ce22000 vpage_a=65 vpage_b=1 vpage_bias=0 vpage_c=129`
  - `matmul-nn-stride-auto-*`
  - `matmul-auto-*`
  - `matmul-config-*`
  - `matmul-os-biascfg-enter`
  - `matmul-os-biascfg-first-iter-enter`
  - `bm0c0`
  - `bm0ce`
  - `bm0ca`
  - `bm0cb`
  - `bm0c1`
  - `matmul-os-post-bias-mvin3`
  - `matmul-os-after-bias`
  - `matmul-os-post-b-mvin2`
  - `matmul-os-post-a-mvin0`
  - `matmul-os-preload-debug-enter`

- 这条结果非常关键：
  - 新 focused baremetal 的确把 Linux-like 高位 alias base 和
    `vpage 0/1/65/129` 布局压到了真正的 first bias `MVIN3` 路径
  - 但它不会稳定停在 `bm0ca`
  - 继续等待后，`bm0cb` 和后续 `mvin2/a-mvin0/preload` 路径都能打印出来

- 因而当前可以明确排除一个更强的错误归因：
  - “只要 alias base 抬到 Linux 风格高 VA，并且 tensor vpage 变成
    `0/1/65/129`，硬件就会稳定卡在第一发 bias `MVIN3`”
  - 这个命题现在被 focused baremetal 否定了

- 当前剩余嫌疑因此进一步收缩到 baremetal 未覆盖的 Linux/runtime 专属差异：
  - Linux runtime 的 PTBR/PTE 物理分配与 install 路径
  - Linux runtime 在进入 first pointwise tile 之前更长的控制面序列
  - `conv-sync/oc-split/pointwise chunk` 上下文里额外的 cfg/opcode/fence/release 状态
  - 或者 guest Linux 环境下更深一层的页表/同步 side effect

- 这轮拿到关键结论后已经及时回收 runfarm：
  - instance: `i-0e56f71078cb1fe6b`
  - `terminaterunfarm --forceterminate` 已执行
  - EC2 状态已确认进入 `shutting-down`

## 32. 2026-03-27 新静态结论：之前的 focused baremetal 还没有复刻 Linux stage0 的物理页摆放

- 这轮重新核对了 Linux/F2 live `uartlog` 里的 stage0 alloc-page 细节，确认当前卡点前的真实物理页摆放是：
  - bias:
    - `v0 -> ppn64@a0/l64`
  - weights:
    - `v1 -> ppn1088@a1/l64`
    - `v2 -> ppn65@a0/l65`
    - `v3 -> ppn1089@a1/l65`
  - input:
    - `v65 -> ppn0@a0/l0`
    - `v66 -> ppn1024@a1/l0`
  - output:
    - `v129 -> ppn32@a0/l32`
    - `v130 -> ppn1056@a1/l32`

- 对应的物理 shared-spad 摆放规律是：
  - input:
    - local pages `0..31`，按 `a0/lp, a1/lp` 交错
  - output:
    - local pages `32..63`，按 `a0/lp, a1/lp` 交错
  - bias:
    - `a0/l64`
  - weight:
    - 从 `a1/l64` 开始，再与 `a0/l65, a1/l65, ...` 交错

- 而之前的 focused baremetal
  `bareMetalC/learn-gemmini/rerocc_lc_pointwise_stage0_runtime_highva_vpage0_focus.c`
  只复刻了：
  - 高位 alias base
  - `vpage 0/1/65/129`
  但没有复刻上述物理页摆放。

- 直接原因已经静态确认：
  - 共享实现文件
    `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
    在 `REROCC_FOCUSED_POINTWISE_INTERLEAVED` 路径里原本写死了：
    - A local page base = `480`
    - B local page base = `560`
    - bias local page base = `704`
    - C local page base = `656`
    - slot offsets = `0/1/4/3`
  - 这与 Linux stage0 的 `0/64/64/32` 和 `0/1/0/0` 不一致

- 因此，之前那个高-VA focused baremetal 只能证明：
  - “高 VA + Linux 风格 `vpage` 布局”本身不足以复现卡点
  - 但它还不能证明“Linux stage0 的完整页表/物理页布局”也不足以复现卡点

- 为了补上这个空缺，这轮已做两处代码修改：
  - 把
    `bareMetalC/learn-gemmini/rerocc_lc_resadd_explicit_interleaved.c`
    中 focused stage0 case 的 local-page / slot-offset 参数改成可覆盖宏
  - 新增一个严格复刻 Linux stage0 物理摆放的 wrapper：
    `bareMetalC/learn-gemmini/rerocc_lc_pointwise_stage0_runtime_linuxphys_focus.c`

- 同时也已经把这个新 case 的 FireSim baremetal 入口接好：
  - host-init:
    `rerocc-baremetal-tests-coupleddma/workload/host-init-pointwise-stage0-linuxphys-focus.sh`
  - workload json:
    `rerocc-baremetal-tests-coupleddma/workload/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus.json`
  - deploy workload:
    `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus.json`
  - runtime config:
    `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_stage0_linuxphys_focus.yaml`

- 这个新 wrapper 现在固定为：
  - `REROCC_SHARED_SPAD_XLATE_RANGE_BASE = 0x3faf751000`
  - `PW_CHUNK_BIAS_VPAGE = 0`
  - `PW_B_VPAGE = 1`
  - `PW_A_VPAGE = 65`
  - `PW_C_VPAGE = 129`
  - A local page base / slot offset = `0 / 0`
  - B local page base / slot offset = `64 / 1`
  - bias local page base / slot offset = `64 / 0`
  - C local page base / slot offset = `32 / 0`

- 本地静态验证结果：
  - 新增 baremetal 已成功编过：
    `rerocc_lc_pointwise_stage0_runtime_linuxphys_focus-baremetal`
  - 这说明新的宏覆盖和 wrapper 至少在编译层面是自洽的
  - 新 host-init 也已本地跑通，并产出：
    `rerocc-baremetal-tests-coupleddma/workload/rerocc_lc_pointwise_stage0_runtime_linuxphys_focus.riscv`
  - build-config 已记录当前关键参数：
    - alias base `0x3faf751000`
    - `vpage 0/1/65/129`
    - local page bases `0/64/64/32`
    - slot offsets `0/1/0/0`

- 现在最合理的下一步不是再猜：
  - 直接运行这个 `linuxphys_focus` baremetal
  - 看它是否会比旧的 `highva_vpage0_focus` 更接近 Linux 的 `bm0ca` 边界

- 如果这个新 case 仍然过掉而不复现，
  剩余嫌疑会进一步收缩到：
  - runtime 在 first tile 前额外插入的控制面序列
  - Linux runtime 的 PTBR/PTE install / flush 完成时序
  - 或更深层的 guest-only side effect

## 33. 2026-03-27 新 F2 focused baremetal 结论：精确复刻 Linux 物理页摆放后，仍然能跑过 `bm0ca`

- 这轮实际跑的是新的 exact-layout case：
  - runtime config:
    `sims/firesim/deploy/config_runtime_f2_rerocc_lc_baremetal_pointwise_stage0_linuxphys_focus.yaml`
  - workload:
    `rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus`
  - runfarm instance:
    `i-03f7adafb3630876d`

- 这轮先暴露了一个新的流程坑，并已修掉：
  - 只添加 deploy workload json 不够
  - FireSim 会把 rootfs / bootbinary 路径按
    `workloads/<benchmark_name>/...`
    做相对解析
  - 如果缺少同名目录，`infrasetup` 会在 rsync rootfs 时失败
  - 直接报错是：
    - `change_dir ".../workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus/../../../../../software/firemarshal/boards/default/installers/firesim" failed`
  - 修正方式：
    - 增加目录
      `sims/firesim/deploy/workloads/rerocc-lc-baremetal-coupleddma-pointwise-stage0-linuxphys-focus/`
    - 放一个 `README` 占位即可

- 修完后重跑 `infrasetup` 成功，live `uartlog` 证明这个新 case 的关键地址与页摆放确实已经对齐 Linux：
  - `xlate_base=0x3faf751000`
  - `vpage_bias=0`
  - `vpage_b=1`
  - `vpage_a=65`
  - `vpage_c=129`
  - region summary:
    - A first pages:
      `0x40000000, 0x40100000`
    - B first pages:
      `0x40110000, 0x40010400`
    - BIAS:
      `0x40010000`
    - C first pages:
      `0x40008000, 0x40108000`

- 更关键的是，这轮已经动态证明：
  - 它不但到达了 `bm0ca`
  - 还继续推进到了：
    - `bm0cb`
    - `bm0c1`
    - `matmul-os-post-bias-mvin3`
    - `matmul-os-post-b-mvin2`
    - `matmul-os-post-a-mvin0`
    - `matmul-os-preload-debug-enter`
    - `matmul-os-post-compute-preloaded0`

- 因而现在可以更强地排除一个假设：
  - “Linux/F2 卡在 `bm0ca` 的主因，是 stage0 first tile 这组 tensor 的物理 shared-spad 页摆放”
  - 这个命题现在也被否定了

- 结合前面的结论，当前剩余嫌疑进一步收缩为 Linux/runtime-only 差异：
  - runtime 在进入 first pointwise tile 前额外发出的控制面序列
  - action-local PTBR/PTE install / flush 的完成时序
  - runtime scope/cfg/opcode/fence/release 状态
  - 或 guest Linux 环境引入的额外 side effect

- 这轮拿到结论后已及时回收 runfarm：
  - `terminaterunfarm --forceterminate` 已执行
  - `i-03f7adafb3630876d` 已确认进入 `shutting-down`

## 34. 2026-03-27 新静态结论：runtime 的 `spm_xlate` helper 之前实际被编成空桩

- 对 `pipeline-runtime/src/prt_rerocc.c` 做静态排查后确认：
  - `prt_gemmini_spm_xlate_cfg()`
  - `prt_gemmini_spm_xlate_range()`
  - `prt_gemmini_spm_xlate_flush()`
  - `prt_gemmini_spm_xlate_fault_read()`
  - 原先全都被
    `#if defined(__riscv) && defined(PRT_ENABLE_GEMMINI_SPM_XLATE_INSN)`
    包住
- 但整个仓库里没有任何地方定义
  `PRT_ENABLE_GEMMINI_SPM_XLATE_INSN`
- 结果就是：
  - runtime 仍然会生成 action-local alias VA
  - 仍然会打印 software-side 的 `xlate-install / exec-bind / alias addr`
  - 但真正发给 Gemmini 的
    `spm_xlate_cfg/range/flush`
    指令 helper 会被编译成 no-op

- 这和当前 live Linux/F2 的最深边界非常一致：
  - runtime 准备阶段本身不一定立刻暴露错误
  - 第一条真正拿 alias 地址去做 Gemmini memory access 的路径，
    即 pointwise 的第一发 bias `mvin3`，才最容易卡死
  - 这也解释了为什么 exact-layout baremetal 无法复现：
    baremetal 明确调用了
    `rerocc_gemmini_spm_xlate_cfg/range/flush`
    去真正编程硬件 xlate

- 这轮已修补：
  - 在 `__riscv` 上默认
    `#define PRT_ENABLE_GEMMINI_SPM_XLATE_INSN 1`
  - 仍允许后续如果确有需要，再通过显式传 `0` 关闭

## 35. 2026-03-27 新静态修补：xlate control path 不再复用 stage0 pointwise 的 cfg

- 原先 `prt_gemmini_spm_xlate_*()` 固定走：
  - `prt_rr_acquire_scope(NULL, 0, manager_id, 3U, ...)`
  - 也就是 stage `0` / opcode `3`
  - 对当前 `rr_cfg_id_for_stage()` 映射，这会落到 cfg `1`
- 而当前最深 stall 的 stage0 pointwise 也恰好使用：
  - stage `0`
  - opcode `3`
  - cfg `1`

- 这意味着 runtime 的 xlate control path 和真实 stage0 Gemmini exec path
  共用同一条 ReRoCC cfg/opcode lane。

- 这轮已修补：
  - `prt_rerocc.c` 新增 dedicated xlate control cfg：
    - `PRT_RR_SPM_XLATE_CFG_ID = RR_MAX_CFGS - 1`
  - `prt_gemmini_spm_xlate_cfg/range/flush/fault_read`
    统一改为先 acquire 这条 dedicated cfg，再发 xlate 指令
  - stage-derived exec cfg 保持不变

- 这一步的目的不是宣称已经完全修好 stall，而是先去掉一个明显的
  runtime-only 控制面污染源，让下一轮 Linux/F2 更接近真实执行面。

## 36. 2026-03-27 新断点：把 `config_ld(id=2)` 前后边界压到单条指令

- 在 `include/gemmini.h` 又补了两条极短 raw marker：
  - `[graw] matmul-config-pre-ld-d-instr`
  - `[graw] matmul-config-post-ld-d-instr`
- 它们分别位于：
  - `gemmini_extended3_config_ld(..., id=2)` 之前
  - `gemmini_extended3_config_ld(..., id=2)` 之后

- 下一轮若日志停在：
  - `pre-ld-d-instr` 之后、`post-ld-d-instr` 之前
    - 才能更明确地说卡在这条 `config_ld(id=2)` 指令本身
  - 若两条都能打印出来
    - 就应继续把关注点放回第一发 `bias mvin3`

## 37. 2026-03-27 本地编译核对

- native `pipeline_runtime` 已重新编译，PASS
- RISC-V Linux runtime 已按 `host-init` 同样的 make 入口重编，PASS
- 带日志版 cross-build 已确认最终 binary 中包含：
  - `[prt-progress] spm-xlate-cfg ...`
  - `[prt-progress] spm-xlate-range ...`
  - `[prt-progress] spm-xlate-flush ...`
  - `[graw] matmul-config-pre-ld-d-instr`
  - `[graw] matmul-config-post-ld-d-instr`

## 38. 2026-03-27 当前 live F2 旧镜像确认仍卡在 `bm0ca`，已回收

- 对 live run 再次核对后确认：
  - 实例：`i-0a84d9844eeead910`
  - 机型：`f2.6xlarge`
  - 私网地址：`192.168.1.131`
  - `uartlog` 字节数停在：`483256`
  - 最深可见边界仍是：
    - `[graw] bm0ca`
  - 但 `heartbeat.csv` 持续增长到：
    - `28733716395, 2956`

- 因而这台机器上跑的仍然是“修补前的旧 Linux payload”，继续等待没有调试价值。

- 本轮已经只回收这一套 runfarm：
  - `firesim terminaterunfarm --forceterminate`
  - 目标实例 `i-0a84d9844eeead910`
  - 已确认进入 `shutting-down`

## 39. 2026-03-27 新修补：runtime 侧 xlate install/reset 改成单次 cfg scope 内完成

- 之前 runtime 的几个关键调用点仍然是三段式 helper：
  - `cfg`
  - `range`
  - `flush`
- 每一步各自 `acquire -> issue -> release`
- 这和 focused baremetal 中连续执行
  `cfg -> range -> flush`
  的方式不同，存在被其他 ReRoCC 控制流插入的窗口。

- 本轮已把以下调用点改成 grouped helper：
  - `configure_action_spm_xlate()`
    - 改为 `prt_gemmini_spm_xlate_program(...)`
  - `disable_action_spm_xlate()`
    - 改为 `prt_gemmini_spm_xlate_reset(...)`
  - `prt_action_install_spm_context()`
    - 改为 `prt_gemmini_spm_xlate_program(...)`
  - `runtime_bootstrap_spm_xlate()`
    - 改为 `prt_gemmini_spm_xlate_reset(...)`

- 同时 `prt_spm_xlate_release_scope()` 现在会在 release 前显式：
  - `rr_fence(scope->cfg_id)`

- 当前工作假设更新为：
  - 如果上一轮 Linux/F2 的 `bm0ca` stall 与 runtime-only xlate control serialization 有关，
    这一轮应该最有机会直接把边界往后推。

## 40. 2026-03-27 新 Linux 产物已重编并写入 overlay

- 用 coupled-DMA workload 的 `host-init.sh` 完整重编了
  `rerocc_pipeline_runtime-linux`
  并重新 stage overlay，PASS。
- 中途交叉编译额外暴露了一个纯 C 顺序问题：
  - `prt_rr_acquire_scope_cfg()` 被提前调用但没有前置声明
  - 已补上 static prototype
  - 重编后通过

- 当前最终 Linux binary 已确认包含：
  - `[prt-progress] spm-xlate-program ...`
  - `[prt-progress] spm-xlate-program-restore ...`
  - `[graw] matmul-config-pre-ld-d-instr`
  - `[graw] bm0ca`

- 因而下一轮 FireSim/F2 不会再是旧镜像，应能直接验证：
  - `bm0ca` 是否被推进
  - 还是仍然卡在第一发 bias `mvin3`
