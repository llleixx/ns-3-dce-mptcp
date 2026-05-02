# Flow Priority Experiment Plan

## 1. 目标

使用 `myscripts/pamptcp/pamptcp.cc` 评估不同 MPTCP scheduler 在“多优先级上行业务”场景下对高优先级业务时延稳定性的影响。

本轮先固定业务模型为“每个实验场景目录直接给出自己的模板参数”，也就是 `avg-only` 目录里的模板就直接写平均速率，`p23-peak` 目录里的模板就直接写对应优先级的峰值或平均速率。这样做的原因是：

1. 先把优先级隔离能力测清楚，比一开始就把 burst 模型也引入更稳。
2. 平均速率和峰值速率是当前任务里最明确的业务约束。
3. 如果后续要继续做“平均+峰值突发”的模板流量，模板文件本身已经可以直接承载 `appSteadyTime/appBurstTime` 等参数，不需要再额外引入一层实验模式。

## 2. 实验场景

- 拓扑：1 个 AP，30 个 Client，Server 侧保持单接收端。
- 接入：每个 Client 同时具备 NR 与 Wi-Fi，两条接入链路由 MPTCP fullmesh 建立子流。
- Client 优先级分布：
  - 优先级 1：6 个
  - 优先级 2：11 个
  - 优先级 3：13 个
- 业务模板：
  - 优先级 1：稳态 `12 Kbps`，峰值 `20 Mbps`
  - 优先级 2：稳态 `42 Kbps`，峰值 `64 Mbps`
  - 优先级 3：稳态 `96 Kbps`，峰值 `25 Mbps`

## 3. 实验矩阵

建议至少跑下面 3 组负载场景。每一组都要统计全部优先级，不只是观察对象优先级。

1. `avg-only`
   - 模板直接写成 P1/P2/P3 全部平均速率。
   - 用于建立低负载基线。
2. `p3-peak`
   - 模板直接写成 P1/P2 平均速率、P3 峰值速率。
   - 用于观察低优先级冲高时，对高优先级时延的影响。
3. `p23-peak`
   - 模板直接写成 P1 平均速率、P2/P3 峰值速率。
   - 用于构造高压场景，重点看 P1 的时延尾部是否失控。

每个场景都建议对以下 scheduler 分别运行：

- `default`
- `roundrobin`
- `blest`
- `pablest`
- `redundant`

如果运行成本允许，建议每个组合至少重复 3 到 5 次，只改变 `clientStartJitterStream`，避免把单次启动时序当成稳定结论。

当前建议把 `clientStartJitter` 固定到 `1.0s`。原因是业务发送周期本身就是 `1s`，把启动抖动拉到同一量级后，能明显打散“首次 Wi-Fi 路径使用 + ARP + 子流建立”的同步冲击，避免把启动伪像误当成 scheduler 行为。

## 4. 统计窗口

建议仿真总时长先固定为 `10.0s`，统计窗口为 `[4.0s, 10.0s]`。

理由：

1. `clientStartJitter=1.0s` 时，各 UE 的真实启动时间会被拉散到一个完整发送周期内；如果总时长仍只有 `6s`，统计很容易继续混入启动期。
2. 把统计窗口放到 `4s` 之后，基本可以避开 Wi-Fi 首次 ARP、MPTCP 子流建立和首批 RTT 样本污染。
3. `4s -> 10s` 这段窗口已经足够长，能覆盖多个发送周期和调度轮次，同时比 `12s` 更节省仿真时间。

## 5. 核心指标

每次实验都按优先级输出以下指标：

- `activeFlows / expectedFlows`
- `rxPackets`
- `rxBytes`
- `throughputMbps`
- `meanDelayMs`
- `p50DelayMs`
- `p95DelayMs`
- `p99DelayMs`
- `p999DelayMs`
- `maxDelayMs`
- `meanJitterMs`
- `p95JitterMs`
- `p99JitterMs`
- `p999JitterMs`
- `maxJitterMs`
- `minFlowThroughputMbps`
- `maxFlowThroughputMbps`

其中：

- Delay 为应用层包从发送时间戳到接收回调的单向时延。
- Jitter 为同一 flow 相邻两个应用层包时延差的绝对值。
- 统计基于 `SeqTsSizeHeader + PacketSink::RxWithSeqTsSize`，适用于 TCP/MPTCP 字节流，不会被接收侧粘包/拆包破坏。

## 6. 合理性复核

当前计划是合理的，理由如下：

1. 场景层次清楚。`avg-only -> p3-peak -> p23-peak` 是逐步加压，而不是一次性把所有流量打满。
2. 指标足够支撑结论。均值只反映整体趋势，`P99/P99.9` 才能看出高优先级是否被低优先级拖出长尾。
3. 统计窗口避开了建链阶段。否则结论很容易被 SYN、MPTCP 子流建立、Wi-Fi 关联等瞬态污染。
4. 每次都统计全部优先级，而不是只盯着一个优先级。这样才能看到“保护高优先级”的代价是否是彻底牺牲低优先级。
5. 仍有一个残余风险：当前主结论基于“平均/峰值常发”，还不是完整 burst 业务。这个风险是可控的，因为它不会妨碍先回答“scheduler 是否能保护高优先级时延”这个核心问题。

## 7. 建议运行命令

示例配置目录位于 `myscripts/pamptcp/flow-priority-30/<scenario>/`。下面以 `default` scheduler 跑 `avg-only` 为例：

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/avg-only \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=default
```

把 `trafficProfileDir` 改成 `flow-priority-30/p3-peak` 或 `flow-priority-30/p23-peak`，再把 `--mptcpScheduler` 改成不同算法，即可完成整个实验矩阵。
