# Blocker Debug SOP Plan

更新时间：`2026-04-14 16:19 UTC`

## 1. 背景

- 近几轮 `bertmini` FPGA 调试已经反复证明：
  高频文本探针会改变 frontier，
  甚至把卡点从原窗口推到更后的位置。
- 仅凭“这轮日志多打了一点，前沿过了旧位置”不能算真正穿过 blocker；
  必须先区分：
  - `observability_only`
  - `semantic_software`
  - `hardware`
- 当前主线仍以软件侧、低扰动排查为主；
  不改硬件，
  不重新构建 bitstream，
  直到静态和动态证据都逼到硬件边界。

## 2. 目标

1. 固定卡点排查顺序，避免再次回到“先堆日志再解释”的旧模式。
2. 把日志面收敛到 page / token / manager / stage 等精确窗口。
3. 让下一轮 rerun 只引入一个新变量，便于判断 frontier 变化是否可信。
4. 把每轮 claim 明确分成：
   `stable_observation` /
   `observability_only` /
   `semantic_candidate` /
   `hardware_candidate`

## 3. 硬规则

- Linux boot 早期只要没有明确 boot error / panic / crash，
  且 `heartbeat.csv` 还在推进，
  就继续等；
  不把静默窗口记成新 blocker。
- 卡点 / 报错默认顺序固定为：
  1. artifact audit
  2. 静态读代码
  3. capture triage
  4. 最后才加新的窄 probe
- 默认 fixed profile 保持低扰动：
  breadcrumb 开，
  deep log 关，
  trigger log 默认关。
- 每次 rerun 只允许一个新增高风险观测变量。
- 若 frontier 变化只伴随 observability 变化，
  必须补 control rerun，
  不得直接记成“问题解决”。
- 若后续证据逼到硬件，
  先停下来写清“为什么必须改硬件”，
  再决定 baremetal / metasim / bitstream 路线。

## 4. 执行 SOP

### 4.1 静态阶段

1. 跑 `audit_pipeline_runtime_artifact.py`，
   先排 stale image / artifact / workflow 漂移。
2. 对照当前 frontier，
   静态阅读对应代码窗口，
   先确认该窗口里是否真的还有硬件语义动作。
3. 跑 `triage_prt_capture.py`，
   生成：
   - `frontier`
   - `claim_class`
   - `disturbance_risk`
   - `requires_control_rerun`
   - `recommended_next_probe`

### 4.2 触发日志阶段

仅当 breadcrumb / sparse frontier 仍不足以分开相邻边界时，才进入这一阶段。

1. 运行：
   `triage_prt_capture.py <capture> --emit-trigger-env`
2. 只使用脚本生成的
   `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
   env block。
3. 先跑：
   `pairdummy_sbus128_workflow.sh debug-preflight`
4. 通过后再做单变量 rerun。

当前 trigger-gated 观测面设计：

- 默认关闭
- 未激活且当前事件不在目标
  family / stage / subbatch / manager / tensor / page / token
  维度内时，直接返回
- 只在命中目标维度后激活
- 只写短行
- pre-ring 只保留目标维度内的近邻事件，
  不再为 page0..page23 这类远离窗口的路径做预采样
- 再写有限 post-budget
- 默认 `match_once=1`

新增硬经验：

- 若某轮 trigger rerun 的 authoritative frontier
  比最近的低扰动 control rerun 明显更早，
  且 `heartbeat.csv` 仍推进，
  优先归入
  `observability_only / probe-disturbance`
  检查 trigger 热路径是否仍在命中前做了额外工作，
  不要直接覆盖旧 frontier。
- 若
  `guest-trigger-log.txt`
  的有效行数
  恰好等于
  `match line + post_budget`
  （默认就是
  `33 = 1 + 32`），
  优先判定为
  `trigger window exhausted`：
  这时最后一条 trigger 只能算 capture cutoff，
  不能直接当新的 frontier。
  当前
  `triage_prt_capture.py`
  已自动输出这条告警。

## 5. 已实现交付

- 新增 `prt_trigger_log` 运行时触发日志基础设施
- `dma / rr / runtime worker / gemmini-pointwise` 已接入短行 trigger 事件
- `triage_prt_capture.py` 可输出下一轮推荐 trigger env
- `pairdummy_sbus128_workflow.sh` 新增 `debug-preflight`
- fixed profile `v20` 默认保留 trigger 关闭，
  但允许仅对 `PIPELINE_RUNTIME_DEBUG_TRIGGER_*`
  做受控 overlay
- `prt_trigger_log_note()` 已修正为 strict capture：
  命中前若事件不在目标维度内，
  不再继续做
  `seq / format_line / ring_push`

## 6. 下一步

1. 用最近一次可信 capture 跑 `triage_prt_capture.py --emit-trigger-env`
2. 在单独 subshell 中叠加 trigger env
3. 先 `debug-preflight`
4. 再做单变量 rerun
5. 将 rerun 结果按
   `claim_class + requires_control_rerun`
   记入 `debug_records/`
