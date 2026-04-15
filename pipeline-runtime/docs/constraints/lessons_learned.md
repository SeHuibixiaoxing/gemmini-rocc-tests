# Pipeline Runtime Lessons Learned

## 1. 探针会扰动前沿

- 热路径文本日志本身就可能改变 guest 时序。
- 结论前提必须是：
  先确认新前沿不是 probe 造成的“假前移”。
- 因此当前默认策略是：
  coarse file log + breadcrumb + 极窄条件日志。

## 2. NUL 尾和页对齐日志不等于真实停点

- 如果 guest log 停在某一行，同时：
  - heartbeat 继续前进
  - 文件大小卡在页对齐附近
  - 文件尾出现大量 `NUL`
  那更可能是文件追加/刷盘路径也在阻塞。

## 3. stale image 比想象中更常见

- host shell 里 env 已经变了，不代表 guest image 一定是新内容。
- `firemarshal.env`、runner、runtime binary 三者任何一个没更新，现场推理都会失真。

## 4. boot 早期静默不能乱判 hang

- Linux boot 阶段，只要 heartbeat 还在前进，就不要单凭 `uartlog` 静默判新 blocker。

## 5. runner / crumb 文件不要 foreground sync

- 逐条 crumb 后立即 `sync` 会把前沿假性前移到 shell 层。
- 当前正确做法是依赖 wrapper 周期性 `sync`。

## 6. guest 文件比 UART 更可信

- 当前这条主线已经多次证明：
  `uartlog` 很容易停滞或丢失细节；
  guest 文件系统日志才是主证据面。
