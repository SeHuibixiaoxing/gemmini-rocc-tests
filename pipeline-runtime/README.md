# Pipeline Runtime

`pipeline-runtime` 是 HybridMapper 编排结果在 Gemmini/ReRoCC/CoupledDMA 上的执行时实现。

当前真实状态：

- 编排期已经决定 segment 需要多少加速器、多少 SPM 页、各 stage/tensor/buffer 的逻辑布局。
- 运行时只负责兑现这些要求：
  - 分配物理 Gemmini/DMA manager
  - 为 action 申请一段连续 alias VA window
  - 为该 action 建立私有 shared-spad 页表和 PTBR/PTE backing
  - 把逻辑 tensor/buffer 视图绑定到真实物理页
- `C1` 到 `C8` 八类 pipeline buffer 仍然由同一套 pipebuf/ringbuf 拓扑执行。
- 本轮重构后，拓扑与执行态已经从 `prt_runtime_t` 全局单例迁到 `action->exec` 私有容器。
- 但“多个 active action 同时执行”还没有完成；当前 `prt_runtime_run()` 仍一次只推进一个 active action。

## 文档地图

- `README.md`
  入口和当前状态
- `ARCHITECTURE.md`
  运行时架构、action/exec ownership、buffer 拓扑
- `DECISIONS.md`
  已冻结约束、硬件接口边界、调试规则
- `ROADMAP.md`
  下一步实现顺序
- `TESTPLAN.md`
  本地验证、baremetal、Linux/F2 的验证阶梯
- `docs/multi_action_runtime.md`
  多 active action runtime 的当前实现边界与后续缺口
- `docs/blockers_and_lessons.md`
  历史 Linux/F2 卡点、日志经验、runfarm 纪律
- `docs/linux_dma_guardrails.md`
  Linux userspace DMA 特定 guardrails
- `docs/archive/2026Q1_history.md`
  旧时间线和历史记录

历史协作文档已经归档到：

- `conference/mudnac_hybridmapper_collab_docs/archive/2026Q1/`

## 当前代码入口

- Host/Linux 共用 runtime：
  `generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/`
- Artifact exporter：
  `conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py`
- Canonical artifacts：
  `conference/HybridMapper/output/pipeline_runtime/bertmini/`

## 最短检查

1. 导出 artifacts

```bash
cd /home/ubuntu/chipyard
python3 conference/HybridMapper/scripts/create-pipeline-runtime-artifacts.py --model bertmini
```

2. 全量重编 runtime

```bash
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime clean
make -C /home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime -j4
```

3. CLI sanity

```bash
/home/ubuntu/chipyard/generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/pipeline_runtime --help
```

4. 最小 host closure

```bash
cd /home/ubuntu/chipyard
METHODS=ours2 BATCH=1 SKIP_BUILD=1 SKIP_EXPORT=1 \
bash generators/gemmini/software/gemmini-rocc-tests/pipeline-runtime/scripts/run_bertmini_host_closure.sh
```

## 本轮已验证

- `make -C .../pipeline-runtime -j4`
  PASS
- `pipeline_runtime --help`
  PASS
- `METHODS=ours2 BATCH=1 SKIP_BUILD=1 SKIP_EXPORT=1 run_bertmini_host_closure.sh`
  PASS
