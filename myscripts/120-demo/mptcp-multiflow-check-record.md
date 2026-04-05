# 120 UE 多链路联调与修复记录

实验日期：2026-03-25  
实验目录：`myscripts/120-demo/`  
相关程序：`myscripts/120-demo/120-demo.cc`、`myscripts/app-test/rate-dual-application.cc`、`model/linux/linux-socket-impl.cc`

## 一、结论先行

本次工作的目标是解决这样一个问题：在 `120-demo` 中，`120` 个 UE（User Equipment，用户设备）同时存在时，为什么不是每个 UE 的 NR（New Radio，5G 空口）和 WiFi 两条链路都能同时承载 MPTCP（Multipath TCP，多路径 TCP）业务。

最终结论是：

1. 问题不是“120 个节点时 MPTCP 完全失效”，而是大规模并发时序下，**少量 UE 没能在有限窗口内同时把两条链路都带起来**。
2. 根因不是单一因素，而是三类因素叠加：
   - DCE Linux `Listen()` backlog 太小；
   - 同一 AP 下 30 个 STA（Station，站点）几乎同时做 WiFi 关联；
   - TCP 一连上就立刻发 payload，导致 MPTCP 第二条 subflow（子流）还没起来就开始统计。
3. 修复后，在 `120` UE 的最终验证命令下，`default` 和 `roundrobin` 两种 scheduler（调度器）都达到了：
   - `connected=120/120`
   - `nrActive=120/120`
   - `wifiActive=120/120`
   - `bothActive=120/120`

也就是说：**在修复后的代码下，120 个 UE 时，每个节点的多链路都能同时工作。**

---

## 二、最终保留的改动

本节只记录最终保留在代码中的改动。实验中尝试过但最后回退的改动，会放到后面的“失败尝试”章节。

### 1. 把 DCE Linux socket 的监听队列从 5 提高到 4096

文件：`model/linux/linux-socket-impl.cc`

原逻辑：

```cpp
int ret = this->m_kernsock->Listen (5);
```

修复后：

```cpp
int ret = this->m_kernsock->Listen (4096);
```

#### 为什么这个改动重要

`PacketSink` 这类 ns-3 应用调用的是 `Socket::Listen()`，并不会显式传 backlog（监听队列长度）。  
而 DCE 里的 `LinuxSocketImpl::Listen()` 把 backlog 写死成了 `5`。  
对 `120` 个 UE 的并发 MPTCP 建连来说，这个值过小，容易让 server 端进入半连接队列压力状态。

#### 相关概念

- **backlog**：监听 socket 在内核里允许排队等待 `accept` 的连接上限。  
  单位是“个连接”，是一个整数。
- 值越小，在高并发短时间建连时越容易丢连接或触发额外保护机制。

#### 落点

- 文件：`model/linux/linux-socket-impl.cc`
- 函数：`LinuxSocketImpl::Listen()`

### 2. 给流应用增加 `InitialSendDelay`

文件：`myscripts/app-test/rate-dual-application.cc`、`myscripts/app-test/rate-dual-application.h`

新增属性：

```cpp
.AddAttribute ("InitialSendDelay", ...)
```

连接成功后的逻辑从：

```cpp
UpdateState ();
```

改为：

```cpp
Simulator::Schedule (m_initialSendDelay,
                     &RateDualModeApplication::UpdateState,
                     this);
```

#### 为什么这个改动重要

MPTCP 的第一条子流通常会先建立，第二条子流需要额外的时间通过 path manager（路径管理器）拉起。  
如果应用一连上就立刻开始发 payload，那么在短窗口实验中，统计结果就会偏向“只有第一条子流有字节”。

本次将默认 `InitialSendDelay` 设为 `0.2s`，让连接成功后先给 MPTCP 子流建立留一个很短但关键的 warm-up（预热）窗口。

#### 相关概念

- **payload**：真正应用层要传输的数据，不含链路层/网络层/传输层头部。
- **warm-up**：不是为了“拖延发流”，而是为了让连接和多子流状态先稳定下来，再开始正式统计。

#### 落点

- 文件：`myscripts/app-test/rate-dual-application.cc`
- 类：`RateDualModeApplication`
- 属性：`InitialSendDelay`

### 3. 为每个 STA 错开 WiFi 关联时刻

文件：`myscripts/120-demo/120-demo.cc`

核心做法是：不再把一个 AP 下的一批 STA 一次性用同一个 `StaWifiMac` 配出来，而是按 STA 单独安装，并对每个 STA 设置不同的：

```cpp
"WaitBeaconTimeout", TimeValue (MilliSeconds (120 + i * wifiAssocStaggerMs))
```

默认：

- `wifiAssocStaggerMs = 20`

#### 为什么这个改动重要

你仓库里之前的 `dce-wifi-ofdma` 调试已经证明过：  
多个 STA 几乎同时做关联时，少量 STA 可能会因为 Association Response（关联响应）时序问题，后续 ARP（Address Resolution Protocol，地址解析协议）和 TCP 都起不来。

在 `120-demo` 里，每个 AP 下有 `30` 个 STA。  
如果不把关联时刻错开，就很容易在“规模放大后”把这个问题放大成少数节点长期没有 WiFi payload。

#### 相关概念

- **STA**：WiFi 终端节点。
- **AP**：Access Point，接入点。
- **WaitBeaconTimeout**：STA 在被动扫描/等待 beacon（信标）阶段使用的超时参数。  
  单位是毫秒（ms）。适当增大并错开它，可以把关联请求分散到不同时间点。

#### 落点

- 文件：`myscripts/120-demo/120-demo.cc`
- 参数：`wifiAssocStaggerMs`
- WiFi 安装逻辑：`installWifi` lambda 内部

### 4. 提高 `120-demo` 默认的 client 启动抖动

文件：`myscripts/120-demo/120-demo.cc`

默认值从：

```cpp
clientStartJitter = 0.002;
```

提高为：

```cpp
clientStartJitter = 0.3;
```

#### 为什么这个改动重要

`120` 个 UE 在几乎同一时刻建连，会把下面几类压力叠加到一起：

- server 端 TCP 半连接与已连接队列压力
- MPTCP 初始子流与附加子流的并发创建压力
- WiFi/NR 两侧控制面与数据面瞬时竞争

把 client 启动抖动从 `2ms` 提高到 `300ms`，可以显著降低“所有连接同时起步”带来的尖峰。

#### 落点

- 文件：`myscripts/120-demo/120-demo.cc`
- 参数：`clientStartJitter`

### 5. 新增 backbone 侧的真实多链路 payload 检查器

文件：`myscripts/120-demo/120-demo.cc`

新增开关：

```text
--checkBackboneDualTraffic=1
```

新增统计器：

- `BackboneTcpPayloadTracker`

它挂在 server 骨干网口的 `CsmaNetDevice/MacRx` 上，对收到的以太网帧逐层解析：

1. Ethernet
2. LLC/SNAP（如果有）
3. IPv4
4. TCP

然后按 **源 IP** 统计 TCP payload 字节。

#### 为什么这个改动重要

这次排查的核心不是“总吞吐是多少”，而是：

> 每个 UE 的 NR 和 WiFi 两条链路到底有没有真的把 payload 送到 server。

`PacketSink/RxWithAddresses` 在 MPTCP 下看的是 meta-socket（元套接字）视角，不适合做 per-subflow（逐子流）判断。  
而 server 骨干网口上的 `MacRx` 能直接看到真实子流的源地址：

- NR：`7.x.x.x`
- WiFi：`10.10/11/12/13.x.x`

因此它更适合回答“每个 UE 的两条链路是否都有流量”。

#### 输出格式

程序结束时会打印：

```text
[120-demo] Backbone dual-traffic summary: nrActive=... wifiActive=... bothActive=...
```

以及：

- 缺失 NR payload 的 UE 列表
- 缺失 WiFi payload 的 UE 列表

#### 落点

- 文件：`myscripts/120-demo/120-demo.cc`
- 开关：`checkBackboneDualTraffic`
- 统计类：`BackboneTcpPayloadTracker`

---

## 三、实验中尝试过但最终回退的改动

### 关闭 `tcp_syncookies`

实验中我尝试过：

```text
.net.ipv4.tcp_syncookies = 0
```

原始想法是：server 端日志里曾出现过：

```text
Possible SYN flooding on port 5000. Sending cookies.
```

看起来像是 syncookies（SYN Cookie，同步 cookie）影响了 MPTCP 的握手行为。  
但实际结果是：关闭后反而使 `connected` 和 `bothActive` 明显变差，因此这个改动没有保留。

这说明：

- syncookies 不是本次问题的主修复点
- 更主要的矛盾仍然是 backlog、关联时序和 payload 发起时机

---

## 四、实验目标与验证口径

本次实验的目标不是做长期性能评测，而是回答一个更基础的问题：

> 修复后，在 `120` 个 UE 的规模下，是否能保证每个 UE 的 NR/WiFi 两条链路都真正出现 payload？

这里的验证口径是：

- `nrActive`：有多少个 UE 的 NR 源地址在 server backbone 上出现过 TCP payload
- `wifiActive`：有多少个 UE 的 WiFi 源地址在 server backbone 上出现过 TCP payload
- `bothActive`：有多少个 UE 的 NR/WiFi 两个源地址都出现过 TCP payload

如果：

```text
bothActive = 120/120
```

那么就可以认为：在当前验证配置下，`120` 个 UE 的多链路都同时工作了。

---

## 五、关键参数说明

| 参数 | 含义 | 单位/范围 | 本次典型取值 | 影响 |
| --- | --- | --- | --- | --- |
| `numClients` | UE 总数 | 个，正整数 | `120` | 本次固定为大规模场景 |
| `simTime` | 仿真结束时间 | 秒 | `2.0`、`3.0` | 越大，给子流建立和发流留的窗口越长 |
| `sinkStart` | server `PacketSink` 启动时间 | 秒 | `0.5` | 应早于 client 发流 |
| `clientStart` | client 应用启动时间 | 秒 | `1.0` | 留给 RRC、路由和 WiFi 关联的稳定时间 |
| `clientStartJitter` | client 启动抖动 | 秒 | `0.3` | 用于错开建连峰值 |
| `appSteadyRate` / `appBurstRate` | 每 UE 目标发流速率 | bps/Kbps/Mbps | `100Kbps`、`200Kbps` | 本次更关注“是否有流量”，不是压满链路；文中命令把两者设成相同以保持近似恒定负载 |
| `appInitialSendDelay` | TCP 连接成功后延迟多久再发 payload | 秒 | `0.2` | 给 MPTCP 第二条子流留 warm-up 时间 |
| `wifiAssocStaggerMs` | 每个 STA 额外增加的 `WaitBeaconTimeout` | 毫秒 | `20` | 错开同 AP 下 STA 的关联请求 |
| `mptcpScheduler` | 调度器 | 字符串 | `roundrobin`、`default` | `roundrobin` 更适合做“链路有没有问题”的诊断基线 |
| `checkBackboneDualTraffic` | 是否开启 backbone payload 统计 | 布尔 | `1` | 本次最关键的验证开关 |

---

## 六、实验过程与结果

### 阶段 A：问题复现

命令：

```bash
./waf --run "120-demo --numClients=120 --simTime=2.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=200Kbps --appBurstRate=200Kbps --mptcpScheduler=roundrobin"
```

旧版本结果：

```text
connected=118/120
nrActive=118/120
wifiActive=110/120
bothActive=110/120
```

说明：

- 不是所有节点都失败
- 但也远远达不到“120 个 UE 都同时双链路工作”

### 阶段 B：只提高 backlog

把 `Listen()` backlog 从 `5` 提到 `4096` 后，重新跑同一组诊断命令。

结果：改善不明显，`bothActive` 仍停留在 `110/120` 左右。

说明：

- backlog 的确是问题的一部分
- 但不是唯一瓶颈

### 阶段 C：增加 payload warm-up

增加 `InitialSendDelay=0.2s` 后，同一诊断命令下结果提升到：

```text
bothActive=111/120
```

说明：

- 有少量 UE 之前只是“第二条子流还没来得及建起来就开始统计”
- 但主问题仍未解决

### 阶段 D：错开 WiFi 关联

为每个 STA 单独设置递增的 `WaitBeaconTimeout` 后，同一诊断命令下结果变成：

```text
connected=119/120
nrActive=119/120
wifiActive=117/120
bothActive=117/120
Backbone missing NR payload UEs: 111
Backbone missing WiFi payload UEs: 18,56,111
```

这是一次明显提升，说明：

- WiFi 关联扎堆确实是造成少数 UE 丢失 WiFi payload 的关键原因

### 阶段 E：错误尝试，关闭 syncookies

关闭 `tcp_syncookies` 后，反而退化到：

```text
connected=84/120
bothActive=84/120
```

因此此改动被回退。

### 阶段 F：最终验证，拉长有效发流窗口

在保留以下修复后：

- 大 backlog
- `InitialSendDelay`
- `wifiAssocStaggerMs`
- `clientStartJitter=0.3`

我把验证窗口调整为：

- `simTime=3.0`
- `clientStart=1.0`
- 每 UE 速率降低到 `100Kbps`

这样做的目的不是“提高吞吐”，而是：

- 降低每个 UE 的发流压力
- 同时给每个 UE 足够长的时间把两条子流都带起来

#### F1. `roundrobin` 验证

命令：

```bash
./waf --run "120-demo --numClients=120 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=100Kbps --appBurstRate=100Kbps --mptcpScheduler=roundrobin"
```

结果：

```text
PacketSink totalRxBytes=2366000
throughputMbps=9.464
connected=120/120
nrActive=120/120
wifiActive=120/120
bothActive=120/120
```

#### F2. `default` 验证

命令：

```bash
./waf --run "120-demo --numClients=120 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=100Kbps --appBurstRate=100Kbps --mptcpScheduler=default"
```

结果：

```text
PacketSink totalRxBytes=2385000
throughputMbps=9.540
connected=120/120
nrActive=120/120
wifiActive=120/120
bothActive=120/120
```

---

## 七、最终判断

经过以上修复和验证，可以给出明确结论：

1. `120` 个 UE 时，原始问题确实存在，表现为部分 UE 不能在统计窗口内同时拿到 NR/WiFi 两条链路的 payload。
2. 根因主要来自：
   - server 端监听队列太小
   - 同 AP 下 WiFi 关联扎堆
   - payload 发起早于 MPTCP 第二条子流稳定
3. 修复后，在最终验证命令下：

```text
bothActive = 120/120
```

因此，本次问题已经可以认为被修复：

> 在修复后的代码下，120 个节点时，每个节点的多链路都能同时工作。

---

## 八、使用建议

### 1. 如果你的目标是“验证多链路是否都能起来”

建议优先用下面这类低负载、长窗口命令：

```bash
./waf --run "120-demo --numClients=120 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=100Kbps --appBurstRate=100Kbps --mptcpScheduler=default"
```

原因是：

- 它更容易回答“每个节点是否双链路都工作了”
- 而不会把“性能瓶颈”和“链路是否存在”混在一起

### 2. 如果你的目标是做高负载性能实验

建议保留本次修复，但在写报告时区分两个问题：

- **连通性问题**：两条链路有没有都起来
- **性能问题**：两条链路起来之后，调度器如何分流、总吞吐有多高

这是两种不同的实验目标，不应混为一谈。

### 3. 如果后续还要继续做 120 UE 批量统计

建议继续保留：

- `--checkBackboneDualTraffic=1`

因为这个统计口径比 `PacketSink/RxWithAddresses` 更适合 MPTCP 子流级验证。
