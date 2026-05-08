# Linux DMA Guardrails

更新时间：`2026-04-13 16:36 UTC`

## 1. Host 虚拟连续不等于物理连续

- Linux 用户态跨页 buffer 通常只保证虚拟地址连续。
- 只要一端是 host buffer，就必须按 host page 分 chunk。
- 每个 chunk 都单独 `virt_to_phys`。

## 2. `mod64` 不匹配时可能需要 bounce

- 当前 host <-> SPM DMA 路径里，
  `bytes >= 64`
  且
  `src_mod64 != dst_mod64`
  的请求不能想当然直接发。
- 当前 guardrail 是：
  用 stage-local bounce page 调整对齐。

## 3. Completion flag 也必须是硬件可访问地址

- 不能只把 payload 的 `src/dst` 转 PA，而把完成标志仍留在普通 host VA。
- completion flag 也必须落到可被硬件访问的物理地址。

## 4. 不要把 doneflag polling 当主完成逻辑

- 当前主线完成语义以 blocking fence 为准。
- doneflag 已验证有问题，只能作为辅助观测，不是新的主同步协议。
- 任何通过 doneflag polling 跳过 `hw_dma_fence()` / blocking wait 的结果，
  都不能作为 DMA 完成证据。

## 5. guest file log 优先

- 当前 Linux/F2 主线上的 runtime 判断以 guest 文件日志为主，不以 `uartlog` 为主。
- 高热路径日志优先 breadcrumb，不优先文本 `O_APPEND`。
