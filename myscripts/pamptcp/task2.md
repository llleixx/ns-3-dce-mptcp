# task2 根因定位

## 问题

场景：

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-4x150m \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --pathMode=wifi-only \
  --mptcpScheduler=default
```

观察到：

- `p3-only-4x150m/default-wifi-only-stream1-6s.json` 中 Wi-Fi-only 的 delay 仍然明显偏高。
- 直觉上 `160 MHz / 802.11ax / 2x2 / 4x4 / 近距离` 的链路能力不该这么弱。

## 最终结论

已经定位到更具体的根本原因。

根因不是“PHY 太差”，也不是“这条 Wi-Fi 链路只能跑到 145 Mbps/flow”。  
真正的根因是：

1. 当前 ns-3 的 `RrMultiUserScheduler` 在 UL OFDMA 中，不是按“发送端真实 backlog”来给 STA 分配上行时长，而是按 AP 最近收到的 `Buffer Status Report` 来决定 `TB PPDU` 时长。
2. 这个 `Buffer Status Report` 反映的是 **STA 当前 Wi-Fi MAC 队列** 的字节数，不是 Linux TCP socket backlog，也不是应用层持续 offered load。
3. 你的发送应用 `RateDualModeApplication` 是按固定间隔平滑发包的，不是一次性把大量数据灌进 MAC 队列，所以很多时刻 STA 的 Wi-Fi MAC 队列只有几 KB 到几十 KB。
4. 于是 AP 经常只发出 **很短的 UL TB PPDU**，而不是接近 `5.484 ms` 上限的长上行突发。这样每轮都要反复支付 `DL MU + BSRP + QoS Null + Basic Trigger + Multi-STA BA + SIFS` 的固定开销，最终把总 payload goodput 压到大约 `0.53~0.58 Gbps`，折算后就是你看到的 `~145 Mbps/flow`。

再往下一层：

5. `RrMultiUserScheduler` 的 UL 调度对象来自上一轮 DL MU 的 `m_candidates`。而在纯 TCP 上行场景里，AP 下行主要只有 TCP ACK，所以 DL MU 并不总能把 4 个 STA 都一起拉进来。
6. 这导致很多 UL 轮次实际上只服务 `1` 个或 `2` 个 STA，而不是 `4` 个 STA 一起上行，进一步放大了控制开销。
7. `Buffer Status Report` 还只有 `20 ms` 生命周期，过期后 AP 会把状态当成 `255 = unknown`。一旦发生这种情况，调度器会退回到默认 `m_ulPsduSize=500B`，直接产生 **70.4 us** 量级的超短上行 grant。

这才是这次 `4x150` 场景里“为什么不是你直觉中的高能力 Wi-Fi”的确切实现级根因。

## 关键证据

### 1. PHY 本身没有弱到离谱

我对 `ns-3.35` 的 `RrMultiUserScheduler` 做了运行期探针，直接看到了实际 UL/DL OFDMA 选到的 `RU/MCS/NSS`。

实测日志里，4 用户轮次已经能稳定出现：

- `ru=484-tones`
- `mcs=11`
- `nss=2`

也就是：

- `40 MHz RU_484`
- `HE MCS 11`
- `2 spatial streams`

这对应的 PHY 速率量级并不低，链路质量本身没问题。

所以问题不在“无线条件太差”，而在 MAC/OFDMA 调度方式。

### 2. UL OFDMA 是按 Wi-Fi MAC queue size 定时长，不是按 TCP backlog

上行 buffer status 的来源：

- `QosFrameExchangeManager::ForwardMpduDown()` 会把队列大小写进 QoS 头：
  - [qos-frame-exchange-manager.cc](/root/bake/source/ns-3.35/src/wifi/model/qos-frame-exchange-manager.cc:526)
- 这个队列大小来自 `QosTxop::GetQosQueueSize()`：
  - [qos-txop.cc](/root/bake/source/ns-3.35/src/wifi/model/qos-txop.cc:133)

而 `GetQosQueueSize()` 读的是：

- `m_queue->GetNBytes(tid, receiver)`

也就是 **Wi-Fi MAC 队列**，不是 TCP socket 里的 backlog。

调度器拿到这个值以后，在 `TrySendingBasicTf()` 里用它算 `maxBufferSize`，再把 `TB PPDU` 时长裁成“刚好够发这么多”：

- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:327)
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:341)
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:440)
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:446)

这意味着：

- 只要 MAC queue 里此刻只有 `6 KB / 20 KB / 50 KB`
- AP 就只会给一个很短的上行 grant

而不会因为“应用还在持续 150 Mbps 发包”就主动给长 grant。

### 3. 发送应用本身是平滑 pacing，不会主动把 MAC queue 撑满

`RateDualModeApplication` 不是 bulk send 风格，而是每次调度一个固定间隔的发送事件：

- [rate-dual-application.cc](/root/bake/source/ns-3-dce/myscripts/app-test/rate-dual-application.cc:449)
- [rate-dual-application.cc](/root/bake/source/ns-3-dce/myscripts/app-test/rate-dual-application.cc:456)
- [rate-dual-application.cc](/root/bake/source/ns-3-dce/myscripts/app-test/rate-dual-application.cc:461)

对于：

- `packetSize = 1400 B`
- `rate = 150 Mbps`

发送间隔就是：

- `1400 * 8 / 150e6 = 74.67 us`

也就是说应用层是在 **平滑喂包**，不是一次塞一大坨。

所以：

- Linux/TCP/Wi-Fi MAC 队列里经常只有少量待发包
- AP 看到的 queue size 也经常很小
- UL grant 也就经常很短

### 4. 调度器确实大量只做了 1-user / 2-user UL，而不是 4-user UL

我补的 6 秒汇总里，AP 一共做了：

- `950` 次 Basic Trigger UL MU

其中：

- `ulUsers=1 count=139`
- `ulUsers=2 count=524`
- `ulUsers=4 count=287`

也就是：

- 只有 `30.2%` 的 UL 轮次是 4 用户
- `69.8%` 的 UL 轮次只是 1 用户或 2 用户

平均每个 UL 轮次只有：

- `2.46` 个用户

这不是“偶尔没分满”，而是大量轮次本来就没拉齐 4 个用户。

### 5. 这些 UL grant 经常非常短

从同一次运行得到的汇总：

- `ulUsers=1 avgTbUs = 1542`
- `ulUsers=2 avgTbUs = 3385`
- `ulUsers=4 avgTbUs = 3748`

而 HE TB PPDU 的上限本来可以到：

- `5484 us`

所以即便在 4-user UL 轮次里，平均也只用了上限的大约 `68%`。

前 20 个样本里更明显，出现过很多这种 grant：

- `tbPpduMs=0.0704`
- `tbPpduMs=0.0848`
- `tbPpduMs=0.1856`
- `tbPpduMs=0.4016`
- `tbPpduMs=0.8048`

这些都太短了。

在这种情况下，固定控制开销占比就会非常高：

- 先要有 DL MU
- 然后 BSRP Trigger
- 再是 QoS Null 回报
- 再是 Basic Trigger
- 才轮到真实 UL data
- 之后还有 Multi-STA BA

所以 payload efficiency 被压得很厉害。

### 6. `255 = unknown` 会直接退化成 `500B` grant

AP 侧如果没有 fresh 的 BSR，就返回 `255`：

- [ap-wifi-mac.cc](/root/bake/source/ns-3.35/src/wifi/model/ap-wifi-mac.cc:1575)
- [ap-wifi-mac.cc](/root/bake/source/ns-3.35/src/wifi/model/ap-wifi-mac.cc:1579)
- [ap-wifi-mac.cc](/root/bake/source/ns-3.35/src/wifi/model/ap-wifi-mac.cc:1601)

BSR 生命周期默认只有：

- `20 ms`
- [ap-wifi-mac.cc](/root/bake/source/ns-3.35/src/wifi/model/ap-wifi-mac.cc:82)

而一旦 `TrySendingBasicTf()` 看到 `queueSize == 255`，它就会：

- 把 `maxBufferSize` 当成默认 `m_ulPsduSize=500`
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:327)
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:331)

这会直接产生：

- `tbPpduMs = 0.0704 ms`

我在实际日志里已经看到了多次这种模式：

- `q=255`
- `maxBufferSize=500`
- `tbPpduMs=0.0704`

这就是一种非常具体的“被实现细节钉死”的吞吐损失来源。

### 7. 为什么很多轮次只有部分用户能上行

`RrMultiUserScheduler` 的状态机不是“随时都能从 4 个 STA 里挑上行用户”，而是：

- 先尝试 `DL_MU`
- 然后 `BSRP`
- 然后 `Basic Trigger`

代码在：

- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:158)
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:169)
- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:174)

而 `TrySendingBasicTf()` 遍历的对象是上一轮 DL 里留下的 `m_candidates`：

- [rr-multi-user-scheduler.cc](/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc:325)

纯 TCP 上行时，AP 下行主要只有 TCP ACK。  
所以：

- 哪些 STA 在上一轮 DL 里有 ACK 可发
- 就决定了下一轮 UL 有哪些 STA 能被挑中

这也是为什么实际运行里会有大量 `1-user`、`2-user` UL 轮次。

## 为什么会落到 `145 Mbps/flow`

现在这个数字可以解释成：

1. PHY 其实不差，4-user 轮次能上 `RU_484 + MCS11 + NSS2`
2. 但调度器大量只给了短 UL grant
3. 还大量只调度到 1 或 2 个用户
4. 每轮都要支付完整 trigger/ack 开销

结果就是：

- 总 payload goodput 只剩 `~0.53 - 0.58 Gbps`
- 除以 4 条流，大约就是 `~145 Mbps/flow`

这不是 Wi-Fi 空口物理能力只有 `145 Mbps/flow`。  
这是 **当前 ns-3 802.11ax UL OFDMA 调度实现 + 当前应用发包方式 + TCP ACK 驱动的 DL/UL 耦合** 共同制造出来的结果。

## 一句话总结

真正的根因是：

> `RrMultiUserScheduler` 按 STA 当前 Wi-Fi MAC queue 的瞬时 BSR 来裁 UL grant，而不是按持续 offered load / TCP backlog 来裁；同时 UL 用户集合又依赖上一轮 DL ACK 候选集。对于当前这种平滑 paced 的 TCP 上行流，这会制造出大量 1/2 用户、短 TB PPDU 的 OFDMA 轮次，最终把总 goodput 压到 ~0.55 Gbps，表现为 ~145 Mbps/flow 和明显排队时延。  

这才是这次问题的具体实现级根因。
