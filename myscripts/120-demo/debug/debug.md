# 120-demo `9 clients / 100Mbps` 当前调试结论

日期：`2026-04-04`

说明：

- 本次只看 `wifiEnableOfdma=0`、`wifiEnableUlOfdma=0`、`wifiEnableBsrp=0`
- 额外参考了：
  - `myscripts/120-demo/link-info/wifi-only/测试记录.md`
  - `myscripts/120-demo/link-info/wifi-only/link-info-wifi-only.cc`
- 需要先纠正一个前提：`link-info-wifi-only` 只能做“WiFi 负载量级”的对照，不能完全等价替代 `120-demo` 的 `NR + WiFi + MPTCP` 双链路分流场景

## 1. 当前还能成立的结论

旧版结论里最重的两句话：

1. `9 clients / 100Mbps` 会稳定超时到 `90s+`
2. 主要根因是 `120-demo` 的 `SpectrumWifiPhy` 没配传播损耗模型

对当前 `build-opt-dce-rel` 已经不再成立，至少不能这样下结论。

我在当前二进制上重新跑了下面几组：

### 1.1 `120-demo`，`6 clients / 1 AP / 3s / 100Mbps / MPTCP on`

命令：

```bash
/usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' \
timeout 180s \
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib \
DCE_PATH=/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo \
  --numClients=6 --numAps=1 --simTime=3.0 \
  --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
  --appSteadyRate=100Mbps --appBurstRate=100Mbps \
  --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
  --checkBackboneDualTraffic=1
```

结果：

- 正常结束
- `real=27.77`
- `PacketSink throughputMbps=424.746`
- `nrActive=6/6`
- `wifiActive=6/6`
- `nrThroughputMbps=181.881`
- `wifiThroughputMbps=243.759`

这说明在当前版本里，`6 clients` 时确实是“双链路都在真正承载 payload”，而且 WiFi 侧负载大约在 `244 Mbps` 这个量级。

### 1.2 `120-demo`，`9 clients / 1 AP / 3s / 100Mbps / MPTCP on`

命令：

```bash
/usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' \
timeout 180s \
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib \
DCE_PATH=/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo \
  --numClients=9 --numAps=1 --simTime=3.0 \
  --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
  --appSteadyRate=100Mbps --appBurstRate=100Mbps \
  --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
  --checkBackboneDualTraffic=1
```

结果：

- 正常结束
- `real=43.81`
- `PacketSink throughputMbps=465.446`
- `nrActive=9/9`
- `wifiActive=6/9`
- `nrThroughputMbps=298.804`
- `wifiThroughputMbps=252.485`
- `Backbone missing WiFi payload UEs: 3,5,8`

所以当前版本里，`9 clients` 确实比 `6 clients` 明显更慢，但它不是“稳定卡死到跑不完”的状态。  
更值得注意的是：只有 `6/9` 个 UE 真正把 WiFi 地址上的 payload 发到了 server backbone 上。

### 1.3 `120-demo`，`9 clients / 1 AP / 4s / 100Mbps / MPTCP on`

命令和上面相同，只把 `--simTime=4.0`。

结果：

- 正常结束
- `real=73.88`
- `PacketSink throughputMbps=532.430`
- `nrActive=9/9`
- `wifiActive=6/9`
- `nrThroughputMbps=342.401`
- `wifiThroughputMbps=275.355`
- `Backbone missing WiFi payload UEs: 3,5,8`

这说明：

1. `wifiActive=6/9` 不是因为 `simTime=3s` 太短，至少拉到 `4s` 之后这个现象还在
2. 当前更像是在 `9 clients` 时，只有一部分 UE 的 WiFi 子流真正进入了承载 payload 的状态

## 2. `link-info-wifi-only` 能说明什么，不能说明什么

### 2.1 它不能直接替代 `120-demo`

原因很直接：

1. `120-demo` 是 `NR + WiFi + MPTCP + Linux/DCE + TCP`
2. `link-info-wifi-only` 是 `single-path WiFi + InternetStack + UDP`
3. MPTCP 场景里本来就会有一部分流量走 NR，所以“完全一比一对齐”做不到，这一点是正常的

因此它更适合回答：

> “单 AP、当前这套 802.11ax Spectrum 建模下，WiFi 本身的 wall-clock 慢阈值大概在哪个负载区间？”

而不是直接回答：

> “120-demo 的慢问题一定就是某一个 WiFi 参数单独导致的。”

### 2.2 之前最可疑的偏差：`Spectrum` 传播损耗模型

`link-info-wifi-only.cc` 之前默认做了这件事：

```cpp
Ptr<FriisPropagationLossModel> lossModel =
    CreateObject<FriisPropagationLossModel> ();
lossModel->SetFrequency (static_cast<double> (wifiFrequencyMhz) * 1e6);
spectrumChannel->AddPropagationLossModel (lossModel);
```

而当前 `120-demo.cc` 的 `Spectrum` channel 仍然没有加这一步。  
所以之前把两边直接拿来互证，本身就带了一个隐含变量。

为避免后面继续混淆，我给 `link-info-wifi-only` 新增了：

```bash
--wifiSpectrumUseFriisLoss=true|false
```

这样以后可以明确区分：

- `true`：沿用当前 `link-info-wifi-only` 的 `Friis + MaxLossDb`
- `false`：更接近当前 `120-demo` 的 `Spectrum channel` 写法

### 2.3 这处差异会影响阈值，但方向并不是“没损耗模型就一定更慢”

我重新跑了 `6 clients / 1 AP / 3s / OFDMA off`：

#### A. `Friis` 打开

```bash
.../link-info-wifi-only \
  --numClients=6 --numAps=1 --simTime=3.0 \
  --sinkStart=0.5 --clientStart=1.0 \
  --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
  --offeredLoadMbpsPerClient=39 \
  --wifiSpectrumUseFriisLoss=true
```

- 正常结束
- `real=13.98`
- `aggregateThroughputMbps=226.016`

再把 `offeredLoadMbpsPerClient=40`：

- `120s` 墙钟超时

#### B. `Friis` 关闭

```bash
.../link-info-wifi-only \
  --numClients=6 --numAps=1 --simTime=3.0 \
  --sinkStart=0.5 --clientStart=1.0 \
  --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
  --offeredLoadMbpsPerClient=39 \
  --wifiSpectrumUseFriisLoss=false
```

- 正常结束
- `real=10.25`
- `aggregateThroughputMbps=226.314`

再把 `offeredLoadMbpsPerClient=40`：

- 仍然正常结束
- `real=9.86`
- `aggregateThroughputMbps=231.934`

所以现在能确认的是：

1. `link-info-wifi-only` 里这处 `Friis` 开关确实会改变 wall-clock 行为
2. 但它并不支持“`120-demo` 没加传播损耗模型，所以一定更慢”这个旧结论
3. 至少在当前单 AP、小范围拓扑下，`Friis` 开关带来的影响方向和之前猜测并不一致

## 3. 当前更像“真实问题”的点

现在最值得继续追的，不是“WiFi Spectrum 一定已经被彻底打爆”，而是：

> 为什么在 `9 clients / 100Mbps / MPTCP on` 时，只有 `6/9` 个 UE 的 WiFi 地址真正出现在 server backbone payload 统计里？

目前已经能排除的一点：

- 这不是简单的 `ip rule` / `ip route table 2` 没有下发

我直接看了缺 WiFi payload 的 UE（`nodeId=3,5,8`）在 `files-*` 里的 DCE `cmdline`，能看到它们都执行了：

```text
ip rule add from 10.10.0.x table 2
ip route add 10.10.0.0/24 dev sim0 scope link table 2
ip route add default via 10.10.0.1 dev sim0 table 2
```

也就是说：

- WiFi 地址有配
- policy rule 有配
- table 2 的 WiFi 默认路由也有配

因此“WiFi 子流没 payload”至少不是一个最粗粒度的路由脚本漏配问题。

更像的方向有两个：

1. MPTCP fullmesh 子流建立/使用层面，只让 `6/9` 个 UE 的 WiFi 地址真正进入了有 payload 的 subflow
2. 某些 UE 的 WiFi 子流建立了，但在当前时间窗和调度下没有真正发出 payload

我还尝试过在这个参数点加 `--dumpSockets=1 --dumpSocketCount=9` 去抓 `ss -tin`，但 `9 clients / simTime=4.0` 下这组调试本身会把墙钟拖到 `240s` 超时，因此这条线索还没拿到足够干净的 socket 证据。

## 4. 这次改动了什么

### 4.1 `120-demo.cc`

`--checkBackboneDualTraffic=1` 现在除了打印：

- `nrActive`
- `wifiActive`
- `bothActive`

还会额外打印：

- `nrPayloadBytes`
- `wifiPayloadBytes`
- `nrThroughputMbps`
- `wifiThroughputMbps`

这样能直接看当前参数点下，双链路到底各自分到了多少 payload。

### 4.2 `link-info-wifi-only.cc`

新增：

```bash
--wifiSpectrumUseFriisLoss=true|false
```

这样以后做对照时，至少可以把“是否显式加 Friis 传播损耗”这个变量说清楚，而不是继续让它隐藏在脚本默认值里。

## 5. 当前版本的一句话结论

对于当前 `build-opt-dce-rel`：

- `9 clients / 100Mbps` 的问题仍然存在，但表现为“明显变慢 + 只有部分 UE 真正走到了 WiFi payload”，而不是旧版文档里写的“稳定超时到几小时”
- 旧版把根因直接定成“`120-demo` 的 Spectrum channel 没传播损耗模型”证据不足，当前实验已经不能支持这个说法
- 当前最值得继续查的点，是为什么 `nodeId=3,5,8` 在 `9 clients` 时始终没有 WiFi payload
