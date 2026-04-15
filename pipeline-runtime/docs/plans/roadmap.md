# Pipeline Runtime Roadmap

更新时间：`2026-04-13 16:36 UTC`

## Phase 0. 文档与 workflow 固化

- 完成 authoritative 文档体系重构
- 固化 fixed profile / workflow / freshness / guest-file-first
- 固化 debug_records / change_records

## Phase 1. Runtime 语义对齐

- 对齐 `DRAM_DEPEN`
- 对齐 `ALL_RINGBUFFER`
- 把 `conv + resadd` 当前尺寸规则写死并实现
- 保持 `tensor_id` 为 transport 主键

## Phase 2. Pairdummy sbus128 主线推进

- 继续低扰动追踪 late pointwise / Gemmini 同步区间
- 避免回退到旧 hugetlb / stale-image / export plateau 叙事
- 用固定 workflow 重复验证新结论

## Phase 3. Bertmini dummy 主线稳定

- 在 dummy-model 上拿到稳定完整执行路径
- 再决定是否回到更深 correctness / real-model 问题

## Phase 4. 多 action

- 保持 multi-action-ready 资源所有权分离
- 在单 action 主线恢复前，不恢复多 active action 验证

## Phase 5. Pair-wrapper / Hardware 旁路线

- 继续把 `pair-wrapper manager` 作为单独旁路线维护
- 不让它与当前 runtime 主线文档混在一起
