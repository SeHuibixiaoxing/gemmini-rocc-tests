# Pipeline Runtime TracerV Integration SOP

更新时间：`2026-04-19 08:28 UTC`

## 1. 目标

- 给 `pipeline-runtime` 提供一条可重复的 `TracerV` 接入与 bring-up 路线。
- 先在本机 FireSim metasim 跑通：
  `program -> trigger -> host pull -> TRACEFILE copy-back`
  再决定是否升级到更重的 baremetal / Linux / FPGA 路线。
- 把 `TracerV` 调试判据固定成：
  1. host 是否发生过 nonzero pull
  2. `TRACEFILE-C*` 是否非零
  3. trace 起止是否与 trigger 语义一致

## 2. 适用范围

- 适用于 FireSim manager 驱动的 baremetal metasim、
  以及后续 Linux / FPGA 路线的 `TracerV` bring-up。
- 不适用于仓库外独立 `sims/verilator` 回归；
  Verilator 路线必须走 FireSim metasim。

## 3. 端到端链路

`TracerV` 接入要验证的是整条链路，而不是只看某一个文件：

1. target 程序发出 trace window：
   - 周期窗口
   - PC 窗口
   - 或精确指令 marker
2. runtime config 中的 `tracing` 段正确 arm 了 selector / start / end
3. 硬件侧 `TracerV` bridge 命中 trigger 后开始向 host 输出 token
4. host 侧
   [`generators/firechip/bridgestubs/src/main/cc/bridges/tracerv.cc`](../../../../../../firechip/bridgestubs/src/main/cc/bridges/tracerv.cc)
   的 `process_tokens()` 真正拉到 nonzero bytes
5. host 侧 `serialize()` 把数据写入 `TRACEFILE-C*`
6. workload `.json` 把 `TRACEFILE*` 与 `metasim_stderr.out` 拷回 manager

只要这 6 步里有一步没闭环，
就不要凭 `TRACEFILE` 是空还是非空直接猜根因。

## 4. 硬约束

- FireSim manager 只走：
  `/home/ubuntu/chipyard/scripts/firesim-tmux-run.sh`
- FireSim 正规流固定为：
  `launchrunfarm -> infrasetup -> runworkload -> terminaterunfarm`
- 任何编译、FireMarshal、FireSim 之前，先：
  - `cd /home/ubuntu/chipyard/sims/firesim`
  - `set +u; source sourceme-manager.sh --skip-ssh-setup; source /home/ubuntu/chipyard/env.sh; set -u`
- metasim runtime config 中：
  `target_config.default_hw_config`
  必须引用 build recipe 名字，
  不能误填成 `config_hwdb.yaml` 里的 AGFI 条目。
- 静态读代码与 artifact 先于新 rerun；
  不要一上来扩大热路径日志面。
- Linux / FireMarshal 路线若依赖 marker，
  必须同时做：
  - wrapper env 透传校验
  - source binary marker 校验
  - image 内 runtime binary marker 校验

## 5. 代码与配置静态核对清单

### 5.1 Host 侧 bridge

先核对
[`generators/firechip/bridgestubs/src/main/cc/bridges/tracerv.cc`](../../../../../../firechip/bridgestubs/src/main/cc/bridges/tracerv.cc)
和
[`generators/firechip/bridgestubs/src/main/cc/bridges/tracerv.h`](../../../../../../firechip/bridgestubs/src/main/cc/bridges/tracerv.h)：

- `init()` 会打印 trigger 配置
- `process_tokens()` 会在第一次 nonzero pull 时打印：
  `TracerV[*]: first nonzero host pull ...`
- `flush()` 会打印：
  `TracerV[*]: flush summary ...`

如果这些 host-side 判据不存在，
先不要继续猜 target 程序有没有命中 marker。

### 5.2 Runtime config

最少核对：

- `tracing.enable: yes`
- `tracing.output_format: 0`
- `tracing.selector`
- `tracing.start`
- `tracing.end`
- 短窗口精确触发时，
  可额外设置：
  `plusarg_passthrough: "+trace-min-batch=0"`

说明：

- `+trace-min-batch=0`
  适合最小窗口 bring-up，
  便于尽快看到第一次 host pull
- 长窗口 metasim 可保留默认批量拉取
- 但批量参数解决不了
  “marker 放在超长 prefill 之后”
  这种根因

### 5.3 Workload outputs

workload `.json` 至少要回收：

- `TRACEFILE*`
- `metasim_stderr.out`
- `uartlog`

否则即使 target 真跑通了，
manager 侧也拿不到判据。

### 5.4 Bootbinary / image freshness

对 baremetal：

- 直接 `objdump` 或字节搜索确认 marker 在最终 ELF 里

对 Linux / FireMarshal：

- 不能只看源码里有 marker
- 必须确认：
  - wrapper 已透传相关 env
  - 本地 runtime binary 有 marker
  - guest image 内 runtime binary 也有 marker

## 6. 推荐 bring-up 阶梯

### 6.1 Stage A：最小链路 smoke

先用最小 baremetal 程序验证：

- `TracerV bridge`
- `host pull`
- `serialize`
- `copy-back`

推荐对象：

- 硬件：
  `firesim_rocket_singlecore_no_nic_l2_lbp_30mhz`
- workload：
  `hello-baremetal-tracerv-metasim`
- trigger：
  `selector=0`

通过后，才进入精确 trigger 验证。

### 6.2 Stage B：精确指令 trigger

在已跑通的最小路径上切到 `selector=3`，
不要一上来就用长 workload。

当前已知最小可复用样例：

- 程序：
  [`tests/hello-marker.c`](../../../../../../../tests/hello-marker.c)
- 运行配置：
  [`sims/firesim/deploy/config_runtime_local_metasim_rocket_hello_baremetal_tracerv_inst.yaml`](../../../../../../../sims/firesim/deploy/config_runtime_local_metasim_rocket_hello_baremetal_tracerv_inst.yaml)
- workload：
  [`sims/firesim/deploy/workloads/hello-baremetal-tracerv-inst-metasim.json`](../../../../../../../sims/firesim/deploy/workloads/hello-baremetal-tracerv-inst-metasim.json)

该样例把链路压缩成：

`start marker -> printf -> end marker`

能最低成本验证：

- exact instruction compare 是否命中
- trace 是否从 start marker 附近开始
- `TRACEFILE-C0` 是否非零

### 6.3 Stage C：工作负载形态轻量化

当最小样例通过后，
再切到更接近真实问题形态的 baremetal / Linux workload，
但必须先做轻量化：

- 缩短 prefill
- 把 marker 前移
- 或做 shape-preserving 轻量 variant

当前这轮最大的教训是：

- 如果 marker 放在两个长 prefill 之后，
  即使 `TracerV` 本身完全正常，
  你也会看到：
  - `TRACEFILE-C* == 0`
  - frontier 看起来“不断移动”
  - 误以为 trigger 路径坏了

因此，
重 workload 的 first step 不是继续等，
而是先证明 target 当前 PC 还没到 marker。

### 6.4 Stage D：full-size / FPGA

只有满足以下条件时，才升级到 full-size 或 FPGA：

- Stage A 已证明 bridge / host pull / copy-back 正常
- Stage B 已证明 `selector=3` 精确 trigger 正常
- Stage C 已证明 workload-shaped 路线也能命中 marker

否则先不要消耗 FPGA 机时。

## 7. 精确指令 marker 规范

### 7.1 推荐 marker

当前推荐的 exact marker 为：

- start：`.word 0x00008013`
- end：`.word 0x00010013`

对应配置：

```yaml
tracing:
  enable: yes
  output_format: 0
  selector: 3
  start: "ffffffff00008013"
  end: "ffffffff00010013"
```

说明：

- `selector=3` 的 `start` / `end`
  上 32 位是 mask，
  下 32 位是比较值
- `ffffffff00008013`
  表示“指令值精确等于 `0x00008013`”

### 7.2 为什么用 `.word`

对精确 trigger，
优先直接写原始 `.word`：

- 避免汇编助记符在不同工具链下的重写或别名差异
- 便于用 `objdump` 做字节级验真
- 更接近 `TracerV` 的 exact instruction compare 语义

### 7.3 必须做最终产物验真

对任何 marker 路线都要确认：

- 源码里有 marker
- 最终 ELF / runtime binary 里也有 marker

只看源码，不看产物，容易掉进 stale binary / stale image。

## 8. 已知最小可跑通命令范式

### 8.1 构建与静态校验

```bash
cd /home/ubuntu/chipyard/sims/firesim
set +u
source sourceme-manager.sh --skip-ssh-setup
source /home/ubuntu/chipyard/env.sh
set -u

cd /home/ubuntu/chipyard
./.conda-env/bin/cmake -S tests -B tests/build -D CMAKE_BUILD_TYPE=Debug
./.conda-env/bin/cmake --build tests/build --target hello-marker
./.conda-env/riscv-tools/bin/riscv64-unknown-elf-objdump -d tests/hello-marker.riscv | rg "00008013|00010013"
cp -f tests/hello-marker.riscv \
  sims/firesim/deploy/workloads/hello-baremetal-tracerv-inst-metasim/hello-marker.riscv
```

### 8.2 FireSim manager 正规流

```bash
cd /home/ubuntu/chipyard

scripts/firesim-tmux-run.sh \
  --session-name hello-marker-tracerv-inst-launchrunfarm \
  launchrunfarm \
  -c config_runtime_local_metasim_rocket_hello_baremetal_tracerv_inst.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes_f2_rocket_30mhz.yaml

scripts/firesim-tmux-run.sh \
  --session-name hello-marker-tracerv-inst-infrasetup \
  infrasetup \
  -c config_runtime_local_metasim_rocket_hello_baremetal_tracerv_inst.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes_f2_rocket_30mhz.yaml

scripts/firesim-tmux-run.sh \
  --session-name hello-marker-tracerv-inst-runworkload \
  runworkload \
  -c config_runtime_local_metasim_rocket_hello_baremetal_tracerv_inst.yaml \
  -a config_hwdb.yaml \
  -r config_build_recipes_f2_rocket_30mhz.yaml
```

说明：

- 即使是本机 metasim，也继续走 manager 正规流
- `launchrunfarm` 不省略，
  这样配置与后续 FPGA 路线保持一致

## 9. 推荐判据顺序

按以下优先级判断是否跑通：

1. `metasim_stderr.out` 中是否出现：
   `TracerV[*]: first nonzero host pull ...`
2. `TRACEFILE-C*` 是否非零
3. trace 开头是否与 start marker / 目标窗口一致
4. `uartlog` 是否打印 trigger arm 信息与 workload 完成信息

当前最小 `hello-marker` 已知正向证据是：

- `uartlog` 打印：
  `TracerV: Trigger enabled from start trigger instruction 8013 ...`
- `metasim_stderr.out` 打印：
  `first nonzero host pull bytes=128 selector=3 ...`
- `TRACEFILE-C0` 非零
- trace 开头即从 start marker 地址开始

## 10. 失败分流

### 10.1 `uartlog` 没看到 trigger arm 信息

优先检查：

- `tracing.enable` 是否打开
- `selector/start/end` 是否生效
- 是否用了错误的 runtime config
- manager 是否真的跑到了你以为的配置文件

### 10.2 trigger arm 了，但 host 没有 nonzero pull

优先检查：

- marker 根本还没执行到
- 最终 binary / image 没有 marker
- marker 前有超长 prefill
- `selector=3` 的 mask/value 填错

此时优先做：

- 反汇编确认 marker 地址
- live PC 采样或 gdb 附着确认当前卡在哪条指令
- 缩短 prefill 或前移 marker

不要先继续堆日志碰运气。

### 10.3 host 有 nonzero pull，但 `TRACEFILE-C*` 仍异常

优先检查：

- workload `.json` 是否声明了 `TRACEFILE*`
- `metasim_stderr.out` 与 `flush summary` 是否完整
- `serialize()` / copy-back 是否被截断

### 10.4 加日志后 frontier 改了

优先归类为 observability 扰动，
而不是“已经穿过旧卡点”。

回退到：

- 静态审计
- live PC / gdb
- 最小样例 control rerun

## 11. 长窗口 workload 的专门守则

- 不要守 full-size repro 数小时，只为了等 marker
- 先证明当前 PC 还在 marker 之前
- 若 hart 还在：
  - `fill_pattern()`
  - barrier 自旋
  - 或其他纯软件 prefill
  则当前 root cause 不是 `TracerV` bridge
- 第一优先级是把 marker 提前到可观测窗口，
  而不是继续扩大日志

## 12. 何时升级到 FPGA

只在以下情况升级到 FPGA：

- local metasim 已证明 exact trigger 路线正常
- 问题只在 FPGA 主线复现
- 或 metasim 已把问题收敛到硬件/时序差异

若 metasim 还没证明 marker 能命中，
先不要上 FPGA。
