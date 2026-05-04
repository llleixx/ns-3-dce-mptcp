# p3-sub-peak-4: steady 固定 NR 与 peak 抑制 NR 实验总结

## 结论

在 `p3-sub-peak-4` 场景中，单纯让 steady 流量固定走 NR（New Radio，5G NR 链路）并不能稳住 steady tail latency：`pamnr` 下 p2 steady 的 p99 仍为 `17.314 ms`，overall steady p99 仍为 `18.483 ms`。这说明 steady 在 NR 上的 4-5 ms 中位数和 17-18 ms tail 都真实来自 NR 路径。

当 peak 流量被可靠识别并基本限制在 Wi-Fi 上时，steady tail 明显下降：`pamnrpkwionly` 下 p2 steady p99 从 `17.314 ms` 降到 `5.248 ms`，overall steady p99 从 `18.483 ms` 降到 `6.350 ms`。代价是 peak 完成时间变差，peak p99 从 `280.219 ms` 上升到 `350.758 ms`。

因此，“steady 固定 NR，peak 仍使用 default 但对 NR 做准入/配额抑制”是值得继续推进的方向。关键不是完全禁止 peak 使用 NR，而是避免 peak 在 NR 上挤占 steady 的低时延资源。

## 实验文件

主要结果文件：

- `pamnr-stream1-10s.json`
  - steady 固定走 NR。
  - peak 仍使用 default 行为。
  - 路径：`myscripts/pamptcp/flow-priority-30/p3-sub-peak-4/pamnr-stream1-10s.json`

- `pamnrpkwionly-stream1-10s.json`
  - steady 固定走 NR。
  - peak 通过 DSCP 标记识别后限制走 Wi-Fi，不允许主动回退到 NR。
  - 路径：`myscripts/pamptcp/flow-priority-30/p3-sub-peak-4/pamnrpkwionly-stream1-10s.json`

对照文件：

- `default-stream1-10s.json`
  - default MPTCP scheduler 基线。
  - 路径：`myscripts/pamptcp/flow-priority-30/p3-sub-peak-4/default-stream1-10s.json`

## 关键概念

MPTCP（Multipath TCP，多路径 TCP）会把一个应用层 TCP 连接拆成多条 subflow（子流），例如本场景中的 NR 子流和 Wi-Fi 子流。scheduler（调度器）决定每个待发送数据段走哪条 subflow。

steady 流量指每秒稳定产生的小流量；peak 流量指突发的大流量。在本场景中，p1/p2 都是 steady，p3 同时包含 steady 和 peak。`delay_ms` 只统计 steady 包的应用层到达时延；`peak_delay_ms` 近似表示 peak 突发流的完成时间。

tail latency（尾部时延）通常看 p95/p99。p99 表示 99% 的样本不超过该值，剩余 1% 更慢。对低时延业务来说，p99 往往比平均值更重要。

DSCP（Differentiated Services Code Point，差分服务代码点）是 IP 头里的 6 bit 分类字段。本实验用 DSCP 标记 peak 流，让 scheduler 能可靠区分 peak 和 steady。只靠 `skb->len >= 64KB` 判断 peak 不可靠，因为应用层大包进入 MPTCP scheduler 前可能已经被拆成较小 skb。

## 结果对比

单位：时延为毫秒（ms），`NR%` / `Wi-Fi%` 表示统计窗口内对应路径承载的 payload 字节占比。

| scheduler | scope | mean | p50 | p95 | p99 | max | NR% | Wi-Fi% | peak mean | peak p99 |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| default | overall | 3.511 | 0.692 | 18.483 | 18.483 | 18.483 | 34.344 | 65.656 | 265.884 | 316.102 |
| default | p1 | 1.597 | 0.588 | 3.471 | 3.471 | 3.471 | 33.333 | 66.667 | 0.000 | 0.000 |
| default | p2 | 2.836 | 0.629 | 11.104 | 17.572 | 17.732 | 24.242 | 75.758 | 0.000 | 0.000 |
| default | p3 | 5.612 | 0.694 | 18.483 | 18.483 | 18.483 | 34.367 | 65.633 | 265.884 | 316.102 |
| pamnr | overall | 6.162 | 4.996 | 18.483 | 18.483 | 18.483 | 30.589 | 69.411 | 232.359 | 280.219 |
| pamnr | p1 | 3.369 | 3.372 | 3.471 | 3.471 | 3.471 | 100.000 | 0.000 | 0.000 | 0.000 |
| pamnr | p2 | 5.823 | 4.996 | 17.228 | 17.314 | 17.465 | 100.000 | 0.000 | 0.000 | 0.000 |
| pamnr | p3 | 8.440 | 5.635 | 18.483 | 18.483 | 18.483 | 30.406 | 69.594 | 232.359 | 280.219 |
| pamnrpkwionly | overall | 4.472 | 4.976 | 5.749 | 6.350 | 6.503 | 0.693 | 99.307 | 301.435 | 350.758 |
| pamnrpkwionly | p1 | 3.369 | 3.372 | 3.471 | 3.471 | 3.471 | 100.000 | 0.000 | 0.000 | 0.000 |
| pamnrpkwionly | p2 | 4.428 | 4.784 | 5.236 | 5.248 | 5.248 | 100.000 | 0.000 | 0.000 | 0.000 |
| pamnrpkwionly | p3 | 5.261 | 5.239 | 6.078 | 6.496 | 6.503 | 0.429 | 99.571 | 301.435 | 350.758 |

## 观察

### 1. steady 固定 NR 后，p1/p2 确实没有走 Wi-Fi

`pamnr` 中：

- p1: `NR 100.000%`, `Wi-Fi 0.000%`
- p2: `NR 100.000%`, `Wi-Fi 0.000%`

因此，p1/p2 的时延可以直接视为 NR 上的 steady 表现。p2 的 p50 是 `4.996 ms`，p99 是 `17.314 ms`，说明 NR 在该场景下存在明显 tail。

### 2. peak 使用 default 时，steady tail 仍然较差

`pamnr` 下 peak p99 为 `280.219 ms`，比当前 `default` 文件中的 `316.102 ms` 更好；但 steady overall p99 仍为 `18.483 ms`，p2 p99 仍为 `17.314 ms`。

这说明只把 steady 固定到 NR 不够。peak 继续 unrestricted 使用 NR 时，NR 仍会被 peak 挤占，steady tail 没有被稳住。

### 3. 真正压下 peak 的 NR 使用后，steady tail 明显改善

`pamnrpkwionly` 中，overall NR payload share 降到 `0.693%`，p3 NR payload share 降到 `0.429%`。这表明 peak 基本被限制在 Wi-Fi 上。

对应地，steady tail 下降明显：

- overall p99: `18.483 ms` -> `6.350 ms`
- p2 p99: `17.314 ms` -> `5.248 ms`
- p3 steady p99: `18.483 ms` -> `6.496 ms`

这支持一个判断：`p3-sub-peak-4` 中 steady NR tail 的主要诱因之一是 peak 抢占 NR 资源。

### 4. 完全禁止 peak 使用 NR 会损害 peak 完成时间

`pamnrpkwionly` 的 peak p99 为 `350.758 ms`，比 `pamnr` 的 `280.219 ms` 更差。也就是说，完全把 peak 挡在 Wi-Fi 上虽然能保护 steady，但会牺牲 peak。

这不是最终策略，而是一个上界实验：它证明“释放 NR 给 steady”有效，但也暴露了“完全禁用 NR 给 peak”的代价。

## 中间实验暴露的问题

早期尝试过让 peak 优先 Wi-Fi、Wi-Fi 不可用时再回退 default/NR，但结果没有明显压低 NR share。原因不是思路必然无效，而是 peak 识别方式不可靠。

最初 PAM 用 `skb->len >= 64KB` 判断 peak-like 包。问题是：应用层的 peak 写入很大，但进入 MPTCP scheduler 时可能已经被 TCP/MPTCP 拆成多个较小的 `sk_buff`。如果单个 skb 小于阈值，scheduler 就会把它误判成 steady。

后来改为在 `pamptcp.cc` 中给 peak 流设置 DSCP，并在 PAM scheduler 中读取 DSCP 判断 peak，`pamnrpkwionly` 才真正把 peak 的 NR share 压到接近 0。

## 策略建议

推荐继续推进的策略不是“peak 完全不用 NR”，而是：

```text
steady:
  默认保留/优先使用 NR，作为低时延保护路径。

peak:
  仍允许使用 default 的多路径聚合思想。
  但 peak 进入 NR 前必须通过准入控制。
```

准入控制可以从简单到复杂逐步做：

1. NR credit 配额
   - 每个时间窗给 peak 固定 NR 字节额度。
   - 额度用完后 peak 只能走 Wi-Fi，直到下一个时间窗恢复。

2. NR queue 阈值
   - 当 NR 发送队列、inflight 或预计排队时延低于阈值时，peak 才能用 NR。
   - 当 NR 变忙时，peak 暂时退回 Wi-Fi。

3. steady tail 反馈
   - 如果最近 steady p95/p99 或估计时延升高，就收紧 peak 的 NR 准入。
   - 如果 steady 稳定，再放宽 peak 的 NR 使用。

4. peak 分片级限制
   - peak 不要一次性占满 NR。
   - 可以只允许 peak 的一部分切片走 NR，其余继续走 Wi-Fi。

## 当前最可信结论

`p3-sub-peak-4` 中，steady 固定 NR 是一个有用的隔离基线，但不是完整方案。它暴露出 NR 自身在 peak 干扰下会产生 17-18 ms tail。

真正有效的是保护 NR，不让 peak 无限制占用。完全限制 peak 走 Wi-Fi 可以把 steady p99 压到约 5-6 ms，但 peak p99 会升到约 350 ms。

因此，下一版算法应当是“steady 保留 NR + peak 有条件使用 NR”，而不是 steady redundant，也不是 peak unrestricted default。

## 复现命令

`pamnr`：

```bash
./run-myscript.sh --mode opt --timeout 1800 --clean-files \
  myscripts/pamptcp/pamptcp.cc -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak-4 \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=pamnr
```

`pamnrpkwionly`：

```bash
./run-myscript.sh --mode opt --timeout 1800 --clean-files \
  myscripts/pamptcp/pamptcp.cc -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak-4 \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=pamnrpkwionly
```

