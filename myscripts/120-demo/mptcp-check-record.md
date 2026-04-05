# 12 UE 场景下 MPTCP Scheduler 检查记录

实验日期：2026-03-24  
实验目录：`myscripts/120-demo/`  
相关程序：`myscripts/120-demo/120-demo.cc`

## 一、结论先行

本次排查的目标是确认：在 `120-demo` 中，把 `mptcpScheduler` 从 `default` 改成 `blest` 后，是否真的会影响传输表现。

结论是：**会，但前提是每条连接的 offered load（应用层提供给 TCP 的发送负载）要足够高。**

- 在 `12` 个 UE（User Equipment，用户设备）且每个 UE 仅 `20 Mbps` 恒定发流时，`default` 和 `blest` 的总吞吐与子流分配几乎一致。
- 当每个 UE 提高到 `50 Mbps`、`100 Mbps` 时，两者开始出现可观测差异；这种差异先体现在 `ss -tin` 看到的 **subflow（子流）** 字节分配上，随后在最高负载下也体现在总吞吐上。
- 因此，先前“`default` 和 `blest` 看起来一样”的现象，不是参数没生效，而是 **负载不够高，未把两个调度器的决策差异放大出来**。

## 二、实验目标

本次实验只回答一个问题：

> 在 `12` 个 UE 的固定拓扑下，继续提高单连接速率后，`default` 与 `blest` 是否仍然表现一致？

这里的“表现”分成两层：

1. **总吞吐**：server 端 `PacketSink` 最终收到的总字节数，以及据此计算的平均吞吐。
2. **子流分配**：server 端各条 MPTCP 子流的 `bytes_received`，用于观察流量到底更偏向 NR（New Radio，5G 空口）还是 WiFi。

## 三、相关概念

### 1. MPTCP 与 scheduler

MPTCP（Multipath TCP，多路径 TCP）会在一个应用层连接下面维护多条 TCP 子连接，也就是 **subflow（子流）**。  
`scheduler` 的职责是：当应用有新数据要发时，决定优先把数据放到哪一条子流上。

本次对比的两个调度器是：

- `default`：默认调度器，通常倾向选择当前看起来更合适的子流。
- `blest`：一种以减少慢路径引发阻塞为目标的调度器。在两条路径条件差异变大、队列积压增多时，它更容易做出与 `default` 不同的选择。

### 2. offered load

offered load（提供负载）表示应用希望交给 TCP 发送的数据速率。  
本实验里，它主要由 `appSteadyRate`、`appBurstRate` 以及 traffic model 的状态切换参数决定。

- 本次把 `appSteadyRate=appBurstRate`
- 这样做的目的是把应用负载固定为近似恒定速率，减少随机 burst（突发）对结果的干扰

### 3. 为什么要同时看 `PacketSink` 和 `ss -tin`

- `PacketSink totalRxBytes` 反映的是 **整个实验窗口内最终收到的总数据量**
- `ss -tin` 反映的是 **某个时刻每条 TCP 子流的内核统计状态**

两者用途不同：

- 看“整体性能”，优先看 `PacketSink`
- 看“不同 scheduler 如何分配子流”，优先看 `ss -tin`

注意：`ss -tin` 是在仿真结束前约 `0.4s` 取的一次快照，所以它的 `bytes_received` 不一定和 `PacketSink totalRxBytes` 完全相等。这不是错误，而是统计口径不同。

## 四、代码与参数位置

### 1. scheduler 在哪里设置

`mptcpScheduler` 通过命令行参数传入，并最终写到 Linux DCE 内核的：

```text
.net.mptcp.mptcp_scheduler
```

代码位置：`myscripts/120-demo/120-demo.cc`

- 命令行参数定义：`mptcpScheduler`
- 实际设置：`stack.SysctlSet (nodes, ".net.mptcp.mptcp_scheduler", mptcpScheduler);`

### 2. 本次新增的诊断开关

为了排除“参数没切进去”的可能，我给程序加了：

```text
--dumpMptcpConfig=1
```

作用：在仿真开始后回读并打印：

- `.net.mptcp.mptcp_scheduler`
- `.net.mptcp.mptcp_path_manager`

这样可以直接确认当前 run 的内核配置值。

## 五、实验设计

### 1. 控制变量

以下参数在本组实验中保持一致：

| 参数 | 取值 | 含义 | 单位/范围 | 为什么这样设 |
| --- | --- | --- | --- | --- |
| `numClients` | `12` | UE 数量 | 个，正整数 | 固定规模，避免把“节点数变化”和“单连接速率变化”混在一起 |
| `simTime` | `6` | 仿真时长 | 秒，`>0` | 足够完成建连并观察稳定传输，同时墙钟时间仍可接受 |
| `sinkStart` | `1` | server 端 `PacketSink` 启动时间 | 秒 | 早于 client 发流 |
| `clientStart` | `2` | client 应用启动时间 | 秒 | 给 NR/WiFi 建链、路由下发留时间 |
| `clientStartJitter` | `0.002` | client 启动抖动 | 秒，`>=0` | 避免所有连接同一时刻抢占，减轻同步效应 |
| `clientStartJitterStream` | `1` | 抖动随机流编号 | 无量纲整数 | 保证不同 scheduler 的抖动一致，提升可比性 |
| `dumpSockets` | `1` | 运行 `ss -tin` | `0/1` | 用于读取各子流的真实内核统计 |
| `appSteadyRate` | `20/50/100 Mbps` | steady 状态发流速率 | bps/Kbps/Mbps | 这是本次唯一主动提高的核心负载参数 |
| `appBurstRate` | `20/50/100 Mbps` | burst 状态发流速率 | 同上 | 设成与 `SteadyRate` 一致，使负载近似恒定 |
| `mptcpScheduler` | `default` / `blest` | MPTCP 调度器 | 字符串 | 本次比较对象 |

### 2. 为什么先固定 12 UE，再提高单连接速率

如果同时改变 UE 数量和每个连接的速率，很难判断差异到底来自哪里。  
因此本次只固定 `12` 个 UE，然后逐步把每个 UE 的 offered load 从 `20 Mbps` 提高到 `50 Mbps`、再提高到 `100 Mbps`。

这样可以更直接回答：

- 是“节点多了”导致看不出差异
- 还是“每条连接负载太低”导致看不出差异

## 六、实验命令

### 1. 12 UE, 20 Mbps/UE

```bash
./waf --run "120-demo --numClients=12 --simTime=6 --sinkStart=1 --clientStart=2 --dumpSockets=1 --appSteadyRate=20Mbps --appBurstRate=20Mbps --mptcpScheduler=default"

./waf --run "120-demo --numClients=12 --simTime=6 --sinkStart=1 --clientStart=2 --dumpSockets=1 --appSteadyRate=20Mbps --appBurstRate=20Mbps --mptcpScheduler=blest"
```

### 2. 12 UE, 50 Mbps/UE

```bash
./waf --run "120-demo --numClients=12 --simTime=6 --sinkStart=1 --clientStart=2 --dumpSockets=1 --appSteadyRate=50Mbps --appBurstRate=50Mbps --mptcpScheduler=default"

./waf --run "120-demo --numClients=12 --simTime=6 --sinkStart=1 --clientStart=2 --dumpSockets=1 --appSteadyRate=50Mbps --appBurstRate=50Mbps --mptcpScheduler=blest"
```

### 3. 12 UE, 100 Mbps/UE

```bash
./waf --run "120-demo --numClients=12 --simTime=6 --sinkStart=1 --clientStart=2 --dumpSockets=1 --appSteadyRate=100Mbps --appBurstRate=100Mbps --mptcpScheduler=default"

./waf --run "120-demo --numClients=12 --simTime=6 --sinkStart=1 --clientStart=2 --dumpSockets=1 --appSteadyRate=100Mbps --appBurstRate=100Mbps --mptcpScheduler=blest"
```

## 七、实验结果

### 1. 总结果表

server 节点在 `12` UE 场景下对应 `files-17/`。  
下表中的 `WiFi bytes` 和 `NR bytes` 来自 `files-17/var/log/*/stdout` 中 `ss -tin` 的 `bytes_received` 汇总。

| 每 UE 负载 | Scheduler | `PacketSink totalRxBytes` | `PacketSink throughputMbps` | WiFi bytes (`ss`) | NR bytes (`ss`) | WiFi share |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `20 Mbps` | `default` | `92,423,116` | `184.846` | `71,498,796` | `12,007,044` | `85.62%` |
| `20 Mbps` | `blest` | `92,421,372` | `184.843` | `71,699,204` | `11,948,700` | `85.72%` |
| `50 Mbps` | `default` | `220,029,452` | `440.059` | `151,844,540` | `40,837,172` | `78.81%` |
| `50 Mbps` | `blest` | `219,972,956` | `439.946` | `146,328,280` | `46,378,576` | `75.93%` |
| `100 Mbps` | `default` | `430,501,680` | `861.003` | `319,329,276` | `80,659,088` | `79.83%` |
| `100 Mbps` | `blest` | `417,432,644` | `834.865` | `304,209,148` | `83,034,544` | `78.56%` |

### 2. 结果解读

#### 情况 A：`20 Mbps/UE`

`default` 与 `blest` 几乎完全一致：

- 总吞吐只差 `0.003 Mbps`
- WiFi share 只差 `0.10` 个百分点

这说明在 `12` 个 UE 且单连接 `20 Mbps` 的情况下，两个 scheduler 的决策差异几乎没有被放大出来。

#### 情况 B：`50 Mbps/UE`

这时总吞吐仍然几乎一样，但子流分配开始明显分化：

- 总吞吐只差 `0.113 Mbps`
- 但 `blest` 的 WiFi share 从 `78.81%` 降到 `75.93%`
- 对应地，NR 字节量从 `40.84 MB` 增加到 `46.38 MB`

这说明：**当负载继续上升时，`blest` 已经开始改变两条路径之间的分流策略，只是这种差异还没有明显反映到总体吞吐上。**

#### 情况 C：`100 Mbps/UE`

在更高负载下，差异不仅存在于分流，还开始体现在总吞吐上：

- `default`: `861.003 Mbps`
- `blest`: `834.865 Mbps`
- 吞吐差约 `26.138 Mbps`，约为 `3.04%`

同时：

- `default` 的 WiFi share 为 `79.83%`
- `blest` 的 WiFi share 为 `78.56%`

这说明在 `12` UE 的高 offered load 下，`blest` 已经不再与 `default` 表现一致。

## 八、为什么 12 UE 的差异比 3 UE 更难看出来

从本次实验可以看到，`12` UE 场景下的差异比 `3` UE 场景更“晚出现”，而且更多先反映在子流分配上，而不是总吞吐上。

一个合理推测是：

- `12` 个 UE 时，多条连接同时竞争 NR 与 WiFi 资源
- 无线 MAC（Medium Access Control，介质访问控制）层排队、重传、竞争等效应更强
- 调度器在单连接层面的决策差异，会被多连接竞争平均掉一部分

因此：

- 在低到中等负载下，`default` 和 `blest` 看起来几乎一样
- 在更高负载下，差异会逐步显现
- 最先显现的是每条子流的字节分配
- 最后才可能体现在总吞吐上

这里“多连接竞争平均掉差异”是基于本次现象做出的工程推测，不是严格理论证明。如果后续要把原因说得更严格，需要继续结合更细的内核 MPTCP trace、WiFi/NR MAC 队列统计和时延数据来验证。

## 九、实验结论

本次继续提高 `12` 个 UE 场景下的单连接速率后，可以得到更明确的判断：

1. `mptcpScheduler` 的切换是生效的，不是“参数没传进去”。
2. `12x20 Mbps` 时，`default` 与 `blest` 基本一致。
3. `12x50 Mbps` 时，两者已经出现明显的子流分配差异，但总吞吐几乎仍然相同。
4. `12x100 Mbps` 时，两者不仅子流分配不同，总吞吐也开始出现可观测差异。

因此，对于你当前的 `120-demo` 场景，可以把结论概括为：

> **增大节点流量确实能把 `default` 和 `blest` 的差异放出来，但更关键的是提高“每条连接”的 offered load，而不是只增加节点数。**

## 十、结果目录与复现文件

本次实验保存了以下快照目录，便于后续复查 `ss -tin` 输出：

- `scheduler-results/load12x20-default/`
- `scheduler-results/load12x20-blest/`
- `scheduler-results/load12x50-default/`
- `scheduler-results/load12x50-blest/`
- `scheduler-results/load12x100-default/`
- `scheduler-results/load12x100-blest/`

对 `12` UE 场景，本次主要保留了：

- `files-0`、`files-1`、`files-2`：前三个 UE 的 `ss -tin` 输出
- `files-17`：server 端的 `ss -tin` 输出（最关键）

## 十一、后续建议

如果后续要继续研究 `default` 与 `blest` 的差异，建议优先做两类扩展：

### 1. 更严格的“差异放大”实验

- 固定 `12` UE，进一步提高 `simTime`
- 在 WiFi 或 NR 链路上人为加入额外 delay（时延）或 loss（丢包）
- 观察 `blest` 是否更主动地减少慢路径上的发送

### 2. 更系统的统计

- 把 `files-17/var/log/*/stdout` 自动解析成表格
- 统计每个 UE 的 WiFi/NR 字节占比
- 计算均值、标准差、最小值、最大值

这样会比当前“人工看几条 `ss -tin` 输出”更适合写正式报告或论文附录。
