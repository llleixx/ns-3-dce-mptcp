# p3-only-8peak TCP 参数与调度实验总结

日期：2026-05-02

## 结论摘要

本次实验先排查了 `p3-only-8peak` 场景中 ECF（Earliest Completion First，最早完成优先）表现不如纯 Wi-Fi `testprio` 的问题，确认关键影响来自 TCP（Transmission Control Protocol，传输控制协议）参数，而不是 ECF 一定失效。将仿真脚本中的拥塞控制从 `olia` 改为 Ubuntu-like 的 `cubic`，并将 TCP/core socket buffer 从 16 MiB 大缓冲改回更接近 Ubuntu 默认值后，`default`、`blest`、`ecf` 在 75 Mbps peak 场景中都能超过纯 Wi-Fi 的 `testprio`。随后把 peak 业务量翻倍到 150 Mbps 后，算法差距被拉开：`default` 最好，`default_v1` 次之，`blest` 尾部变差，`ecf` 最差。当前结论是：在纯 P3 peak bulk 场景里，主矛盾是链路利用率，不是 HoL（Head-of-Line blocking，队头阻塞），因此更 work-conserving 的 `default` 反而更适合。

## 1. 本次改动

### 1.1 仿真脚本 TCP 参数改动

文件：[pamptcp.cc](/root/bake/source/ns-3-dce/myscripts/pamptcp/pamptcp.cc:237)

将 MPTCP（Multipath TCP，多路径 TCP）连接使用的 TCP 拥塞控制从：

```text
net.ipv4.tcp_congestion_control = olia
```

改为：

```text
net.ipv4.tcp_congestion_control = cubic
```

文件：[pamptcp.cc](/root/bake/source/ns-3-dce/myscripts/pamptcp/pamptcp.cc:713)

将 socket buffer 相关 sysctl 从 16 MiB 大缓冲：

```text
net.core.wmem_default = 16777216
net.core.wmem_max     = 16777216
net.core.rmem_default = 16777216
net.core.rmem_max     = 16777216
net.ipv4.tcp_rmem     = 4096 87380 16777216
net.ipv4.tcp_wmem     = 4096 65536 16777216
```

改为 Ubuntu-like 默认值：

```text
net.core.wmem_default = 212992
net.core.wmem_max     = 212992
net.core.rmem_default = 212992
net.core.rmem_max     = 212992
net.ipv4.tcp_rmem     = 4096 131072 6291456
net.ipv4.tcp_wmem     = 4096 16384 4194304
```

这些参数控制 Linux TCP socket 的发送/接收缓冲区。单位是 byte。缓冲区越大，越不容易因为 socket buffer 满而阻塞应用写入，但也更容易掩盖排队和乱序交付问题；缓冲区越小，越接近普通系统默认行为，也更容易暴露调度器对 HoL 和链路排队的影响。

`olia` 是 MPTCP 常用的耦合拥塞控制算法之一，目标是在多条 subflow 间协调拥塞窗口。`cubic` 是 Linux/Ubuntu 常见默认 TCP 拥塞控制算法，更偏单路径 TCP 的常规增长行为。本实验的目标不是证明 `cubic` 一定更适合 MPTCP，而是先排除“非默认 TCP 参数导致结果异常”的影响。

### 1.2 新增 x2 peak 场景

新增目录：[p3-only-8peak-x2](/root/bake/source/ns-3-dce/myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2/README.md)

该场景从 `p3-only-8peak` 派生，仅保留 8 条 P3 peak flow，并将每条 flow 的业务速率从 75 Mbps 翻倍到 150 Mbps：

```csv
template_id,priority,appSteadyRate,appBurstRate,appTrafficModel,appSteadyTime,appBurstTime,appInitialSendDelay
prio3_peak,3,150Mbps,150Mbps,duration,ns3::ConstantRandomVariable[Constant=1.0],ns3::ConstantRandomVariable[Constant=0.0],0
```

注意：当前 `RateDualModeApplication` 在 `duration` 模式下会先进入 steady 状态。原始 `p3-only-8peak` 的 `BurstTime=0`，所以实际负载主要由 `appSteadyRate` 决定。因此本次把 `appSteadyRate` 和 `appBurstRate` 都翻倍，而不是只改 `appBurstRate`。

## 2. 关键概念说明

MPTCP 会把一个应用层 TCP 连接拆成多条 subflow。subflow 是一条具体路径上的 TCP 子连接，例如 Wi-Fi subflow 和 NR subflow。调度器决定下一个数据段发到哪条 subflow。

HoL blocking 指队头阻塞。在 MPTCP 中，如果较晚序号的数据已经从快路径到达，但较早序号的数据还卡在慢路径上，接收端就不能把后续数据按序交付给应用。BLEST（BLocking ESTimation-based Scheduler，阻塞估计调度器）和 ECF 都是为了减少这种情况而设计的。

吞吐量表示统计窗口内实际收到的数据速率，单位是 Mbps。`peak_delay_ms` 表示 peak-like flow 的完成时间或近似完成延迟，单位是 ms；mean 是平均值，p95/p99 是第 95/99 百分位，max 是最大值。`NR share` 和 `Wi-Fi share` 表示统计到的 TCP payload 中分别走 NR 和 Wi-Fi 的比例。

## 3. 实验设置

共同设置：

| 参数 | 值 | 含义 |
|---|---:|---|
| `simTime` | 10 s | 仿真总时间 |
| `statsStart` | 4 s | 统计窗口开始时间 |
| `statsStop` | 10 s | 统计窗口结束时间 |
| `clientStartJitter` | 1.0 s | 每个 client 启动时间在 0 到 1 秒内随机抖动 |
| `clientStartJitterStream` | 901 | 随机数流编号，用于保证不同算法同种子可比 |
| `numAps` | 1 | Wi-Fi AP 数量 |
| `mptcp_path_manager` | `fullmesh` | MPTCP 为可用地址组合建立 subflow |
| `tcp_congestion_control` | `cubic` | 本次修正后的 TCP 拥塞控制 |

运行命令模板：

```bash
./run-myscript.sh --mode opt --timeout 1800 \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2 \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --clientStartJitterStream=901 \
  --mptcpScheduler=default
```

将最后一行的 `default` 替换为 `blest`、`ecf`、`default_v1` 即可复现实验矩阵。

## 4. 结果一：75 Mbps p3-only-8peak 中 TCP 参数影响

同一随机种子 `901` 下，先对 ECF 做旧 TCP 参数与 Ubuntu-like TCP 参数对比：

| 场景 | 算法 | 吞吐 Mbps | peak mean ms | peak p95 ms | peak max ms | NR share | Wi-Fi share |
|---|---|---:|---:|---:|---:|---:|---:|
| 旧 TCP 参数 | ecf | 500.000 | 523.242 | 1736.187 | 3283.770 | 56.209% | 43.791% |
| Ubuntu-like TCP 参数 | ecf | 600.000 | 141.367 | 259.060 | 272.233 | 33.633% | 66.367% |

这个差异说明，旧的 `olia + 16 MiB buffer` 组合会明显改变 ECF 的分流和完成时间表现。切换到 Ubuntu-like TCP 参数后，ECF 的吞吐达到 600 Mbps，peak mean 从 523 ms 降到 141 ms，tail 也大幅下降。

在 Ubuntu-like TCP 参数下，75 Mbps 场景的算法对比如下：

| 算法 | 吞吐 Mbps | peak mean ms | peak p50 ms | peak p95 ms | peak p99 ms | peak max ms | NR share | Wi-Fi share |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| default | 600.000 | 141.436 | 120.874 | 257.610 | 263.469 | 263.469 | 32.406% | 67.594% |
| blest | 600.000 | 142.728 | 140.366 | 238.487 | 277.351 | 290.757 | 34.137% | 65.863% |
| ecf | 600.000 | 141.367 | 131.991 | 259.060 | 271.647 | 272.233 | 33.633% | 66.367% |
| testprio | 600.000 | 191.568 | 165.385 | 384.546 | 411.242 | 413.042 | 0.000% | 100.000% |

结论：在 75 Mbps 负载下，`default`、`blest`、`ecf` 之间几乎没有拉开差距，但它们都优于纯 Wi-Fi 的 `testprio`。这说明多链路确实能带来收益；之前“只有 testprio 更好”的现象主要来自 TCP 参数。

## 5. 结果二：150 Mbps p3-only-8peak-x2 中算法差距

结果文件：

- [default-stream901-10s.json](/root/bake/source/ns-3-dce/myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2/default-stream901-10s.json)
- [default_v1-stream901-10s.json](/root/bake/source/ns-3-dce/myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2/default_v1-stream901-10s.json)
- [blest-stream901-10s.json](/root/bake/source/ns-3-dce/myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2/blest-stream901-10s.json)
- [ecf-stream901-10s.json](/root/bake/source/ns-3-dce/myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2/ecf-stream901-10s.json)

| 算法 | 吞吐 Mbps | peak mean ms | peak p50 ms | peak p95 ms | peak p99 ms | peak max ms | NR share | Wi-Fi share |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| default | 1175.000 | 594.706 | 570.751 | 907.235 | 976.258 | 1000.983 | 31.818% | 68.182% |
| default_v1 | 1075.000 | 623.773 | 553.736 | 955.986 | 1202.368 | 1288.268 | 33.224% | 66.776% |
| blest | 1100.000 | 616.150 | 530.127 | 1418.712 | 1810.722 | 1838.988 | 33.307% | 66.693% |
| ecf | 950.000 | 943.179 | 865.361 | 2256.071 | 2441.144 | 2463.260 | 36.672% | 63.328% |

结论：翻倍负载后，`default` 最好，`default_v1` 的 tail 明显好于 `blest`，`ecf` 最差。`ecf` 的 NR share 最高，但总吞吐最低，这不是“更好地利用 NR”，而是 Wi-Fi 有效吞吐下降后导致 NR 占比被动升高。

## 6. 为什么 default 在 x2 场景更好

`p3-only-8peak-x2` 是纯 P3 peak bulk 场景，没有 P1/P2 稳态小流，也没有需要保护的低延迟业务。此时主要目标是尽快把大量 peak 数据发完，所以链路利用率比 HoL 避免更重要。

`default` 更接近 work-conserving scheduler。work-conserving 的意思是：只要有可用链路和可发数据，就尽量不让发送机会空闲。当前结果中 `default` 的 Wi-Fi payload 吞吐达到约 822 Mbps，NR 约 384 Mbps，总吞吐最高。

`blest` 和 `ecf` 的核心思想是减少 HoL。它们在认为慢路径可能拖住快路径交付时，会选择等待，即返回 `NULL`，让当前发送机会暂停。这个机制在“慢路径贡献很小、但 HoL 代价很大”的场景里可能有价值；但在本实验中，NR 能稳定贡献约 383 Mbps，放弃发送机会的成本很高。

从 x2 结果看，NR 吞吐在几个算法中差异很小，主要差异来自 Wi-Fi 吞吐：

| 算法 | NR Mbps | Wi-Fi Mbps | 说明 |
|---|---:|---:|---|
| default | 383.578 | 821.963 | 两条链路都用得较充分 |
| default_v1 | 383.378 | 770.534 | NR 类似，Wi-Fi 少一些 |
| blest | 383.409 | 767.742 | NR 类似，Wi-Fi tail 更差 |
| ecf | 382.726 | 660.923 | NR 类似，Wi-Fi 有效吞吐明显下降 |

因此，x2 中算法差距不是来自 NR 多发了多少，而是来自 Wi-Fi 少发了多少。`default` 赢在更少等待、更高链路利用率。

## 7. 当前可得结论

1. 原始异常结果主要受 TCP 参数影响。`olia + 16 MiB buffer` 会让 ECF 在 `p3-only-8peak` 中表现很差；改为 Ubuntu-like TCP 参数后，ECF 明显改善。

2. 在 75 Mbps peak 场景中，`default`、`blest`、`ecf` 表现接近，并且都优于纯 Wi-Fi `testprio`。这支持“多链路可以改善 peak 完成时间”的判断。

3. 在 150 Mbps x2 peak 场景中，算法差距被拉开：`default` 最好，`default_v1` 次之，`blest` 的尾部较差，`ecf` 最差。

4. 对当前纯 peak bulk 场景，HoL 不是主矛盾。BLEST/ECF 的保守等待策略会牺牲发送机会，导致有效吞吐下降。

5. 如果后续要证明 BLEST/ECF 优于 default，需要构造“慢路径额外带宽贡献小，但 HoL 代价大”的场景，例如更高 RTT、更低带宽、更大 jitter 或更紧接收窗口的慢路径。

## 8. 后续建议

如果继续研究优先级感知调度器，不建议只在 `p3-only-8peak-x2` 这种纯 bulk 场景上优化。这个场景更适合验证链路聚合能力和 work-conserving 行为。

更有区分度的后续场景应同时包含：

- P1/P2 稳态小流，用于观察低延迟业务是否被 P3 peak 干扰；
- P3 有限 burst，而不是持续 bulk；
- 明显异构的 Wi-Fi/NR 链路，例如 NR RTT 更高或带宽更低；
- 统计 steady delay、peak completion time 和 per-link payload share 三类指标。

这样才能区分“吞吐型调度器”和“HoL/优先级保护型调度器”的价值。
