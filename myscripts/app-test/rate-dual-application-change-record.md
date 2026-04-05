# rate-dual-application 改动记录

记录时间：`2026-03-26`  
相关文件：

- `myscripts/app-test/rate-dual-application.h`
- `myscripts/app-test/rate-dual-application.cc`
- `myscripts/app-test/rate-dual-helper.cc`
- `myscripts/app-test/app-test.cc`
- `myscripts/app-test/app-test1.cc`
- `myscripts/120-demo/120-demo.cc`

## 一、结论先行

这次改动的目标，是把 `RateDualModeApplication` 从“能跑”的自定义 TCP 发流程序，整理成更接近 ns-3 现有 `Application` 风格、也更适合 DCE（Direct Code Execution，直接代码执行）环境的实现。  
核心变化有三类：第一，补上 TCP 发送回压处理，避免在 Linux/DCE 非阻塞发送路径上静默丢掉应用层负载；第二，把原先的 `AvgRate/PeakRate` 改成更直观的 `SteadyRate/BurstRate`，并把固定时长升级成和 `OnOffApplication` 一样的 `RandomVariableStream` 接口；第三，把“当前是否还连着”和“是否曾经成功建连”分开，减少后续统计和调试时的语义混淆。  
本次修改后，`120-demo` 和 `app-test1` 都已重新编译通过，并用一个带 `UniformRandomVariable` 的小场景完成了最小回归。  
从当前结果看，`RateDualModeApplication` 已经可以比较稳定地表达“多数时间稳态发流，偶尔进入突发发流”的业务模型。

## 二、本次改了什么

### 1. 局部改动：`myscripts/app-test/` 下的 `RateDualModeApplication`

#### 1.1 补齐 TCP/DCE 发送回压处理

原来的实现是定时调用 `m_socket->Send(packet)`，但没有像 `BulkSendApplication` 那样处理：

- `Send()` 返回 `-1`
- `Send()` 只发出部分字节（partial send）
- 内核发送缓冲区暂时满了，后续要靠 `SetSendCallback()` 继续发送

这在普通 ns-3 TCP 下不一定马上暴露，但在 DCE 下更容易出现。  
因此现在的实现新增了：

- `m_unsentPacket`
- `HandleSendReady()`
- `TrySendPacket()`
- `RecordSuccessfulSend()`

只有当字节真正发出后，才会记录：

- `HasSentPayload()`
- `GetFirstPayloadTxTime()`

这样后续 `120-demo` 里按“最早真实 payload 发送时间”计算吞吐，才有意义。

#### 1.2 把速率接口改成 `SteadyRate/BurstRate`

原来的接口是：

- `AvgRate`
- `PeakRate`
- `BurstProb`

这会让人误以为程序内部真的精确维持“平均速率”，而实际上还需要结合 burst 模型去理解。  
现在改成：

- `SteadyRate`：稳态速率
- `BurstRate`：突发速率

这样接口语义更直接，也更符合“这个业务大多数时间平稳、偶尔突发”的建模目标。

#### 1.3 把固定时长改成 `RandomVariableStream`

原来的 duration 模式使用固定 `Time`：

- `SteadyDuration`
- `BurstDuration`

现在改成和 `OnOffApplication` 同风格的随机变量接口：

- `SteadyTime`
- `BurstTime`

这两个属性的类型都是 `RandomVariableStream`。  
它们不是某一种具体分布，而是“随机变量接口”；真正的分布由你传入的表达式决定，例如：

- `ns3::ConstantRandomVariable[Constant=1.0]`
- `ns3::UniformRandomVariable[Min=0.45|Max=0.55]`
- `ns3::ExponentialRandomVariable[Mean=0.1]`

这意味着：

- 如果你想保持固定时长，依然可以传 `ConstantRandomVariable`
- 如果你想让 steady 或 burst 持续时间带抖动，就可以换成 `Uniform/Exponential/Normal` 等分布

#### 1.4 保留两种 traffic model

当前支持两种模型：

- `Duration`
  含义：在 steady 和 burst 两种状态之间切换，状态持续时间由 `SteadyTime/BurstTime` 决定
- `LegacyProbability`
  含义：保留旧行为，在每个状态评估时刻用 `BurstProb` 判断是否进入 burst，评估间隔由 `StateInterval` 决定

因此：

- 新代码默认更推荐 `Duration`
- 老实验如果必须复现旧逻辑，还可以继续走 `LegacyProbability`

#### 1.5 增加协议保护，防止误用

这个类现在已经明确是“面向 TCP/流式 socket”的应用。  
虽然 `Protocol` 仍然是个属性，但创建 socket 之后会检查 `GetSocketType()`，只允许：

- `NS3_SOCK_STREAM`
- `NS3_SOCK_SEQPACKET`

如果传进来的不是 TCP 类流式 socket，会直接报错。  
这样能避免后续把它错误地拿去配 UDP，然后出现“程序能运行、行为却不对”的隐蔽问题。

#### 1.6 分离“当前连接态”和“历史建连成功态”

现在有两个不同含义的接口：

- `IsConnected()`：当前 socket 是否还处于已连接状态
- `HasConnected()`：本次应用生命周期中是否曾经成功连上过

这样做的原因是：

- 运行中调试时，更关心“当前还连着没有”
- 仿真结束做汇总统计时，更关心“是否曾经成功建连”

以前这两个语义混在一起，容易造成误判。

### 2. 联动改动：`120-demo` 和两个 app-test 示例

为了不让上层脚本还沿用旧参数，本次同步改了调用侧：

- `myscripts/120-demo/120-demo.cc`
- `myscripts/app-test/app-test.cc`
- `myscripts/app-test/app-test1.cc`

其中 `120-demo` 的命令行参数改成：

- `appSteadyRate`
- `appBurstRate`
- `appSteadyTime`
- `appBurstTime`
- `appTrafficModel`
- `appBurstProb`
- `appStateInterval`
- `appStreamBase`

另外，`120-demo` 里 client 汇总统计现在调用 `HasConnected()`，避免应用 stop 之后把“曾成功建连”的统计也清掉。

## 三、关键参数与概念说明

### 1. `SteadyRate`

- 定义：steady 状态下应用层希望交给 TCP 的目标发流速率
- 单位：`bps / Kbps / Mbps / Gbps`
- 典型值：`50.9Kbps`、`100Kbps`、`20Mbps`
- 影响：越大，平稳阶段给 TCP 的 offered load（提供负载）越高
- 选择理由：如果你的业务主体是“多数时间平稳”，这个参数应该对应业务的常态负载

### 2. `BurstRate`

- 定义：burst 状态下应用层希望交给 TCP 的目标发流速率
- 单位：同上
- 约束：当前实现要求 `BurstRate >= SteadyRate`
- 影响：越大，突发阶段的瞬时负载越高
- 选择理由：它用来表达“偶尔突增”的业务阶段

### 3. `SteadyTime`

- 定义：steady 状态持续多久
- 单位：秒
- 类型：`RandomVariableStream`
- 典型表达式：
  - 固定 `1s`：`ns3::ConstantRandomVariable[Constant=1.0]`
  - 在 `0.45s` 到 `0.55s` 间均匀抖动：`ns3::UniformRandomVariable[Min=0.45|Max=0.55]`
- 影响：越大，系统越长时间停留在稳态；越小，steady/burst 切换越频繁

### 4. `BurstTime`

- 定义：burst 状态持续多久
- 单位：秒
- 类型：`RandomVariableStream`
- 典型表达式：
  - 很短 burst：`ns3::ConstantRandomVariable[Constant=0.0001]`
  - 较长 burst：`ns3::ExponentialRandomVariable[Mean=0.05]`
- 影响：越大，突发高负载持续越久，越容易把系统推向拥塞或更充分地激活双链路传输

### 5. `RandomVariableStream`

- 定义：ns-3 中统一的随机变量接口，不是具体某一种随机分布
- 常见分布：
  - `ConstantRandomVariable`
  - `UniformRandomVariable`
  - `ExponentialRandomVariable`
  - `NormalRandomVariable`
- 为什么和本实验相关：本次改动的一个重点，就是把 steady/burst 的持续时间从“固定值”升级成“可抽样的随机变量”

### 6. `TrafficModel`

- `Duration`
  含义：按 steady/burst 两态模型交替切换，持续时间来自 `SteadyTime/BurstTime`
- `LegacyProbability`
  含义：兼容旧版本，每个评估周期用 `BurstProb` 决定是否进入 burst，评估周期由 `StateInterval` 决定

如果你是新写实验，优先建议用 `Duration`。  
如果你是为了复现旧结果，再用 `LegacyProbability`。

### 7. `InitialSendDelay`

- 定义：TCP connect 成功后，再等多久才开始真正发 payload
- 单位：秒
- 本次默认值：`0.2`
- 为什么重要：在 MPTCP（Multipath TCP，多路径 TCP）场景里，主连接建立和第二条子流建立之间可能有时间差；这个延迟能给第二条子流一个 warm-up（预热）窗口

### 8. `appStreamBase`

- 定义：应用内部随机流的起始 stream 编号
- 单位：无量纲整数
- 作用：让 `SteadyTime/BurstTime/StateInterval` 的随机过程可复现
- 为什么重要：如果没有显式分配 stream，同一个场景多次对比时，steady/burst 时长序列可能不完全一致

## 四、代码落点

### 1. `RateDualModeApplication` 属性定义

文件：`myscripts/app-test/rate-dual-application.cc`

- `SteadyRate/BurstRate`：第 `49` 到 `57` 行
- `TrafficModel`：第 `64` 到 `70` 行
- `SteadyTime/BurstTime`：第 `71` 到 `80` 行
- `StateInterval`：第 `81` 到 `85` 行
- `InitialSendDelay`：第 `86` 到 `90` 行
- `Tx/TxWithAddresses` trace：第 `91` 到 `96` 行

### 2. TCP 发送回压与协议保护

文件：`myscripts/app-test/rate-dual-application.cc`

- 协议类型检查：第 `205` 到 `214` 行
- `SetSendCallback()`：第 `247` 到 `248` 行
- `HandleSendReady()`：第 `342` 到 `349` 行
- `TrySendPacket()`：第 `437` 到 `481` 行
- `RecordSuccessfulSend()`：第 `485` 到 `502` 行

### 3. duration 模式随机持续时间

文件：`myscripts/app-test/rate-dual-application.cc`

- `SampleStateDuration()`：第 `170` 到 `178` 行
- steady 初始状态调度：第 `291` 到 `299` 行
- steady/burst 切换：第 `353` 到 `381` 行

### 4. 连接状态语义拆分

文件：`myscripts/app-test/rate-dual-application.cc`

- `IsConnected()`：第 `112` 到 `116` 行
- `HasConnected()`：第 `118` 到 `122` 行

文件：`myscripts/120-demo/120-demo.cc`

- client 汇总统计使用 `HasConnected()`：第 `1117` 行

### 5. `120-demo` 命令行参数接入

文件：`myscripts/120-demo/120-demo.cc`

- `appSteadyRate/appBurstRate`：第 `294` 到 `295` 行
- `appSteadyTime/appBurstTime`：第 `301` 到 `304` 行
- 命令行参数定义：第 `352` 到 `371` 行
- `RateDualHelper` 属性下发：第 `981` 到 `1001` 行

## 五、验证方法

### 1. 编译验证

```bash
./waf build --targets=bin/120-demo
./waf build --targets=bin/app-test1
```

两条命令都已通过。

### 2. 最小回归场景

目的：确认下列几件事同时成立：

- `SteadyTime/BurstTime` 的随机变量表达式可以直接工作
- `RateDualModeApplication` 仍能正常建连和发流
- `120-demo` 中的双链路统计没有被这次重构打坏

执行前清理 DCE 旧目录：

```bash
/root/.codex/skills/ns3-dce-dev/scripts/clean_dce_files.sh --yes
```

运行命令：

```bash
./waf --run "120-demo --numClients=3 --numAps=1 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.01 --checkBackboneDualTraffic=1 --appSteadyRate=1Mbps --appBurstRate=5Mbps --appSteadyTime=ns3::UniformRandomVariable[Min=0.45|Max=0.55] --appBurstTime=ns3::ConstantRandomVariable[Constant=0.05] --appStreamBase=200"
```

关键输出：

```text
[120-demo] PacketSink totalRxBytes=873000 acceptedSockets=0 payloadStartSeconds=1.231 activeSeconds=1.769 throughputMbps=3.947
[120-demo] Client connect summary: connected=3/3 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=3/3 wifiActive=3/3 bothActive=3/3
```

说明：

- `connected=3/3` 说明本次三条 client 应用都至少成功建连过
- `nrActive=3/3 wifiActive=3/3 bothActive=3/3` 说明双链路 payload 统计仍然正常
- `UniformRandomVariable` 的 `appSteadyTime` 能被正确解析并参与运行，说明新的 `RandomVariableStream` 接口已经打通

## 六、当前结论与使用建议

### 1. 如果你想表达“多数时间平稳，偶尔突发”

建议直接使用：

- `TrafficModel=Duration`
- `SteadyRate`
- `BurstRate`
- `SteadyTime`
- `BurstTime`

这是当前最符合直觉、也最接近 `OnOffApplication` 风格的写法。

### 2. 如果你想表达“近似恒定发流”

有两种简单方法：

- 让 `SteadyRate == BurstRate`
- 或者让 `BurstTime` 很小、`SteadyTime` 很大

其中第一种更适合做功能回归，因为变量更少、解释更直接。

### 3. 如果你只是为了复现旧实验

可以继续使用：

- `TrafficModel=LegacyProbability`
- `BurstProb`
- `StateInterval`

但从建模可解释性看，这一套已经不如 duration 模式直观。

### 4. 当前实现的边界

目前这个应用仍然是“面向 TCP 流式 socket 的自定义两态发流器”，不是一个完全通用的 `SocketFactory` 适配器。  
如果后续要继续增强，下一步比较自然的方向是：

- 增加 `MaxBytes`
- 增加更详细的 trace source
- 增加状态切换统计（steady 次数、burst 次数、累计时长）

但就目前“稳态 + 偶发突发”的业务表达目标来说，这版已经足够用了。
