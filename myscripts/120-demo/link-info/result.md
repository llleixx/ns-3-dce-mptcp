# `120-demo` 链路信息测试结果

日期：2026-03-28

## 本次测试口径

- 按任务要求，单独新建了两个程序：
  - `myscripts/120-demo/link-info/wifi-only/link-info-wifi-only.cc`
  - `myscripts/120-demo/link-info/nr-only/link-info-nr-only.cc`
- 两个程序都复刻了 `myscripts/120-demo/120-demo.cc` 中对应链路的关键无线参数、拓扑布局和上行方向，只移除了另一条链路和 MPTCP/DCE 相关逻辑，改成更直接的 UDP 上行测量。
- Wi-Fi 测试按补充要求使用 `1 AP`。
- 在你指出“NR 延迟偏高、Wi-Fi 延迟偏低”之后，我又把默认参数往现实方向做了一次小幅对齐：
  - Wi-Fi：保留 `120-demo` 的高能力空口参数，但把 `AP -> server` 回传改成 `PointToPoint 10Gbps / 1ms`，并给 Wi-Fi 频谱信道补了 `ConstantSpeedPropagationDelayModel`
  - NR：默认改成 `numerology=2`、`gNB=30dBm`，同时把 EPC `S1uLinkDelay` 设为 `1ms`，`PGW -> server` 回传改成 `PointToPoint 10Gbps / 0.5ms`
- 重要修正：
  - 原先如果把“单设备高负载场景下的平均时延”直接拿来对比 Wi-Fi 和 NR，这是不严谨的
  - 现在 NR 的延迟统计已经和 Wi-Fi 一样，统一改成应用层 `SeqTsSizeHeader + PacketSink` 口径
  - 同时补做了“同 offered load 的低负载单设备时延”测试，用于做真正可比的时延判断

## 当前默认参数

### Wi-Fi

- `802.11ax 5GHz`
- `160 MHz`
- `5250 MHz`
- `HE GI = 800 ns`
- `HE MPDU buffer = 256`
- `BE max A-MPDU = 6500631`
- `AP 4x4 / STA 2x2`
- `DL OFDMA + UL OFDMA + BSRP = on`
- `AP -> server backbone = 10Gbps / 1ms`

### NR

- `4.9 GHz`
- `100 MHz`
- `numerology = 2`
- `TDD = DL|F|UL|UL|UL|`
- `NrEesmCcT2 / Table2 / 256QAM`
- `UL fixed MCS = 25`
- `UE 23 dBm / gNB 30 dBm`
- `UE 1x2 / gNB 2x2`
- `S1-U = 1ms`
- `PGW -> server backbone = 10Gbps / 0.5ms`

## 构建方式

当前工作区里直接执行 `./waf --run ...` 会触发与本任务无关的其他目标链接问题，所以本次采用“定向 build + 直接运行二进制”的方式：

```bash
./waf build --target=bin/link-info-wifi-only --target=bin/link-info-nr-only
```

生成的程序位置：

```bash
build/myscripts/120-demo/bin/link-info-wifi-only
build/myscripts/120-demo/bin/link-info-nr-only
```

## 测试 1：1 个设备，逼近单链路能力

说明：

- 这一组不是为了“公平给两条链路同一个 offered load”，而是为了把单设备场景尽量推到接近各自链路能力上限。
- 因此 Wi-Fi 和 NR 分别使用了“足够高但能稳定完成”的 offered load。
- 所以这一组里的“平均时延”主要反映的是接近饱和时的排队代价，不适合拿来直接比较两种链路的基础时延。

### 1. Wi-Fi，1 STA，1 AP

命令：

```bash
timeout 120s build/myscripts/120-demo/bin/link-info-wifi-only \
  --numClients=1 \
  --simTime=5 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --clientStartStepMs=10 \
  --offeredLoadMbpsPerClient=1000
```

结果：

- 聚合吞吐：`983.567 Mbps`
- 平均时延：`1.351 ms`
- 平均抖动：`0.012 ms`
- 推断丢包率：`0.182 %`

结论：

- 在单 AP / 单 STA 下，按这版“稍微向现实对齐”的 Wi-Fi 参数，单链路上行仍可接近 `1 Gbps`。
- 由于现在额外加入了 `1ms` 的有线回传，单设备高负载时延不再落在不现实的几十微秒量级，而是约 `1.35ms`。

### 2. NR，1 UE，1 gNB

命令：

```bash
timeout 120s build/myscripts/120-demo/bin/link-info-nr-only \
  --numClients=1 \
  --simTime=5 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --clientStartStepMs=10 \
  --offeredLoadMbpsPerClient=500
```

结果：

- 聚合吞吐：`388.989 Mbps`
- 平均时延：`424.332 ms`
- 平均抖动：`0.046 ms`
- 丢包率：`0.000 %`

结论：

- 在单 UE 上行场景下，这组 NR 参数的稳定上行能力约为 `389 Mbps`。
- 即便已经把 numerology 提到 `2`，当 offered load 远高于链路可承载能力时，发送端排队仍然会把时延拉到 `424 ms` 左右。
- 这说明此结果应理解为“单链路接近饱和时的能力和排队代价”，而不是“轻负载时延”。

## 测试 1B：1 个设备，同 offered load 的低负载时延

说明：

- 为了回答“为什么 NR 高、Wi-Fi 低”的问题，我额外补了一组真正可比的低负载测试：
  - 两边都只用 `1` 个设备
  - 两边都用 `10 Mbps` offered load
  - 两边都用同一套应用层 `SeqTsSizeHeader` 时延统计

### 1. Wi-Fi，1 STA，1 AP，10 Mbps

命令：

```bash
timeout 60s build/myscripts/120-demo/bin/link-info-wifi-only \
  --numClients=1 \
  --simTime=5 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --clientStartStepMs=10 \
  --offeredLoadMbpsPerClient=10
```

结果：

- 聚合吞吐：`9.842 Mbps`
- 平均时延：`1.077 ms`
- 平均抖动：`0.008 ms`
- 推断丢包率：`0.112 %`

### 2. NR，1 UE，1 gNB，10 Mbps

命令：

```bash
timeout 60s build/myscripts/120-demo/bin/link-info-nr-only \
  --numClients=1 \
  --simTime=5 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --clientStartStepMs=10 \
  --offeredLoadMbpsPerClient=10
```

结果：

- 聚合吞吐：`9.848 Mbps`
- 平均时延：`4.598 ms`
- 平均抖动：`0.906 ms`
- 丢包率：`0.000 %`

结论：

- 原来 `426 ms` 的 NR 延迟并不是“NR 天生就这么高”，而是把单链路打到明显过饱和以后，发送端排队堆出来的。
- 在把默认值改成 `numerology=2 + 1ms core + 0.5ms backbone` 后，同 offered load 的低负载场景下，NR 时延约为 `4.598 ms`。
- Wi-Fi 低负载时延也不再是原来不现实的 `0.08 ms`，而是约 `1.077 ms`。

## 测试 2：30 个设备并发

说明：

- 为了让 Wi-Fi 和 NR 的 `30` 设备结果可直接横向对比，这一组统一采用：
  - `offered load = 1 Mbps / 设备`
  - `simTime = 2.0 s`
  - `sinkStart = 0.2 s`
  - `clientStart = 0.5 s`
  - `clientStartStepMs = 20`
- 这里的聚合吞吐是按整个观测窗口 `[clientStart, simTime]` 统计，所以它反映的是“整个 30 设备并发系统”的平均上行能力；因为启用了错峰启动，最后几个设备的实际活跃时间会更短一些。

### 1. Wi-Fi，30 STA，1 AP

命令：

```bash
timeout 300s build/myscripts/120-demo/bin/link-info-wifi-only \
  --numClients=30 \
  --simTime=2.0 \
  --sinkStart=0.2 \
  --clientStart=0.5 \
  --clientStartStepMs=20 \
  --offeredLoadMbpsPerClient=1
```

结果：

- 聚合吞吐：`23.721 Mbps`
- 平均时延：`1.501 ms`
- 平均抖动：`0.271 ms`
- 丢包率：`0.000 %`
- 活跃流：`30 / 30`

### 2. NR，30 UE，1 gNB

命令：

```bash
timeout 120s build/myscripts/120-demo/bin/link-info-nr-only \
  --numClients=30 \
  --simTime=2.0 \
  --sinkStart=0.2 \
  --clientStart=0.5 \
  --clientStartStepMs=20 \
  --offeredLoadMbpsPerClient=1
```

结果：

- 聚合吞吐：`23.449 Mbps`
- 平均时延：`14.689 ms`
- 平均抖动：`0.100 ms`
- 丢包率：`0.000 %`
- 活跃流：`30 / 30`

### 3. 30 设备对比结论

在这个统一口径下：

- Wi-Fi：`23.721 Mbps`，`1.501 ms`
- NR：`23.449 Mbps`，`14.689 ms`

可以看到：

- 两条链路在 `30` 设备、`1 Mbps/设备` 的稳定并发点上，系统总吞吐非常接近。
- 但时延差异仍然明显：
  - Wi-Fi 单 AP 这一组约 `1.5 ms`
  - NR 这一组约 `14.7 ms`
- 也就是说，在当前这组轻到中等稳定负载下，NR 和 Wi-Fi 的总上行吞吐接近，但 Wi-Fi 的端到端时延仍然明显更低。

## 最终结论

如果按这次任务要求，把两条链路拆开、单独测试，并把参数稍微往现实情况对齐：

- 单设备上限能力：
  - Wi-Fi 单 AP 上行约 `984 Mbps`
  - NR 单 UE 上行约 `389 Mbps`
- `30` 设备并发、统一 `1 Mbps/设备` 时：
  - Wi-Fi 单 AP 聚合吞吐约 `23.721 Mbps`
  - NR 单 gNB 聚合吞吐约 `23.449 Mbps`
  - 两者总吞吐接近，但 Wi-Fi 时延仍明显更低

因此，本次更准确的结论是：

- 在这版“稍微向现实情况对齐”的参数下，单设备上行能力方面，Wi-Fi 仍明显高于 NR。
- 在 `30` 设备统一轻中负载并发下，两者总吞吐接近，但 Wi-Fi 的平均时延仍更低。

## 额外说明

- 本次 Wi-Fi 测试已经按补充要求改为 `1 AP`。
- 当前结果比最初版本更接近现实，但仍然不是“完全现实”的网络：
  - Wi-Fi 现在已经加入 `1ms` 点到点回传和 `ConstantSpeedPropagationDelayModel`
  - NR 现在已经加入 `numerology=2`、`1ms S1-U` 和 `0.5ms` 回传
  - 但 Wi-Fi 仍未额外加入更复杂的衰落/干扰/跨 BSS 竞争模型
  - NR 也仍然是单小区、单 gNB、理想化核心网的简化场景
- 因此，这版结果更适合作为“比原来更合理的仿真对比基线”，而不是现实网络的绝对 KPI。
- 高于当前交付参数的 `30` 设备压测点会显著增加仿真墙钟时间；当前 `ns-3.35` 的 Wi-Fi OFDMA / NR 调度路径在更激进的高负载点上也可能出现内部限制，因此最终结果采用的是“可稳定完成、可复现、可对比”的工作点，而不是盲目继续向上堆 offered load。
