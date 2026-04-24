# default_v1 思想说明

## 1. 它想解决什么问题

`default_v1` 的目标不是做“优先级隔离”，也不是像旧版 `default` 那样偏向最小 RTT。
它想模拟**当前主线 Linux MPTCP v1 默认调度器**的核心思路：

- 先区分 active / backup subflow
- 在可发送的 active subflow 中，优先选择**排空当前发送队列时间最短**的路径
- 估计量不是单纯 RTT，而是：
  - 当前子流待发送字节数 `sk_wmem_queued`
  - 当前子流平均 pacing rate `avg_pacing_rate`
  - 近似比较 `linger_time = sk_wmem_queued / avg_pacing_rate`

直觉上，这比“永远选最小 RTT”更接近“哪条路现在最能尽快把手上的包发出去”。

## 2. 当前主线 Linux 默认调度器怎么做

当前主线 `net/mptcp/sched.c` 里的 `default` scheduler 本身很薄，`get_send()` 直接调用
`mptcp_subflow_get_send()`；真正的选路逻辑在 `net/mptcp/protocol.c` 中。

主线核心步骤可以概括为：

1. 遍历所有 subflow，只看 active 的路径；backup 只在没有 active 时兜底。
2. 对每条候选路径读取：
   - `sk_wmem_queued`
   - `avg_pacing_rate`
3. 计算近似排空时间：
   - `linger_time = (sk_wmem_queued << 32) / avg_pacing_rate`
4. 选择 `linger_time` 最小的子流。
5. 选中后，用当前 `sk_pacing_rate`、本次预计 burst、已有 `wmem` 对 `avg_pacing_rate`
   做一次加权更新。

因此，主线默认调度器更像“最短排队完成时间优先”，而不是“最小 RTT 优先”。

## 3. 我在 v0.92 上怎么实现 `default_v1`

由于当前实验内核还是 MPTCP v0.92，框架和主线不一样，不能逐行照搬，只能保留核心语义：

- 保留主线的核心打分思想：按 `linger_time` 选路。
- 保留 v0.92 原有发送框架：
  - `get_subflow()`
  - `next_segment()`
  - reinject queue
  - receive-buffer optimization
- 保留 v0.92 的 active / backup 语义。
- 在 scheduler 私有区里保存每条 subflow 的 `avg_pacing_rate`。

也就是说，`default_v1` 不是把主线文件原封不动搬过来，而是在 v0.92 发送框架中，把
“最短队列排空时间优先”这件事植入进去。

## 4. 它和当前主线“哪些地方一致”

一致的部分：

- **核心决策目标一致**：优先选排空时间最短的子流，而不是最小 RTT。
- **active 优先、backup 兜底** 的大方向一致。
- **使用 pacing rate + queued bytes** 作为选路依据。
- **发送后更新平均 pacing rate** 的思路一致。

如果只看“默认调度器到底依据什么指标做 send path 选择”，那么 `default_v1` 和当前主线是同一思想。

## 5. 它和当前主线“哪些地方不完全一致”

这里要说清楚：`default_v1` 和主线**不是逐行等价实现**。

主要差异有：

1. **框架不同**
   - 主线基于 `struct mptcp_sock / mptcp_subflow_context`
   - 当前 v0.92 基于 `struct mptcp_cb / tcp_sock / mptcp_sched_ops`
   - 所以函数入口、数据结构、状态存储位置都不同。

2. **可发送性判断不同**
   - 主线默认路径选择主要依赖 `mptcp_subflow_active()`、`sk_stream_memory_free()` 等现代路径状态。
   - `default_v1` 复用了 v0.92 的 `mptcp_is_def_unavailable()` 与临时不可发判断。
   - 这会把 cwnd、窗口、重注入路径掩码等旧框架行为一起带进来。

3. **重传/补发路径不同**
   - 主线把 `get_send()` 和 `get_retrans()` 分开。
   - `default_v1` 仍然运行在 v0.92 的 `next_segment()` 路径上，并沿用了旧版 receive-buffer optimization。

4. **stale / timeout 逻辑不同**
   - 主线会维护 subflow stale 状态，并更新 MPTCP 超时。
   - `default_v1` 没有把主线这套 stale 机制完整搬过来。

5. **细节 tie-break 不同**
   - `default_v1` 在 `linger_time` 相同时，用 `srtt_us` 做次级比较。
   - 主线代码里没有这一步显式 tie-break。

6. **`avg_pacing_rate` 的存储位置不同**
   - 主线把它放在 subflow context。
   - `default_v1` 放在 scheduler 的 per-subflow 私有区。

所以更准确的表述是：

> `default_v1` 与当前主线 Linux 默认调度器在“核心思想和主要打分指标”上是一致的，
> 但在发送框架、可发送性判断、重传路径、stale 处理等实现细节上并不完全一致。

## 6. 为什么实验结果可能和主线直觉不一样

即使核心思想一致，实验结果也不一定好，原因包括：

- 当前场景不是裸 TCP 主机，而是 ns-3-dce + MPTCP v0.92 + Wi-Fi/NR 双接入。
- v0.92 的 old scheduler 框架会把旧的 queue/reinject/窗口约束一起带进来。
- Wi-Fi 与 NR 的 `sk_pacing_rate`、排队行为、驱动节奏在该场景里可能让“最短排空时间”更容易把流量引到 Wi-Fi。
- 这个场景的目标是“保护 P1/P2 延迟和抖动”，而 `default_v1` 并没有显式优先级隔离机制。

因此：

- 从“是否模仿主线默认调度思路”看，`default_v1` 是成立的。
- 从“是否适合这个优先级业务场景”看，它未必是好算法。

## 7. 一句话总结

`default_v1` 不是“当前主线 Linux 默认调度器的逐行复刻”，而是“在 v0.92 老框架中，对主线默认调度核心思想的一次近似回移植”。

## 8. 参考源码

当前主线 Linux 参考：

- `net/mptcp/sched.c`
- `net/mptcp/protocol.c`

当前实验树实现参考：

- `net/mptcp/mptcp_default_v1.c`
- `net/mptcp/mptcp_ctrl.c`
- `net/mptcp/Kconfig`
