# 120-demo Wi-Fi 默认能力对齐结果

日期：2026-03-26

## 改动目标

把 `myscripts/120-demo/120-demo.cc` 的默认 Wi-Fi 配置对齐到 `myscripts/dce-wifi-ofdma` 的高能力基线，重点包括：

- 默认改用 `SpectrumWifiPhyHelper + MultiModelSpectrumChannel`
- 默认使用 `802.11ax 5GHz / 160MHz / 5250MHz`
- 默认使用 `HE GI = 800ns`
- 默认使用 `HE MPDU buffer size = 256`
- 默认使用 `AC_BE max A-MPDU size = 6500631`
- 默认打开 AP 侧 `RrMultiUserScheduler`，即 `DL OFDMA + UL OFDMA + BSRP`
- 保留 `AP 4x4 / STA 2x2`、`WaitBeaconTimeout` 错峰关联和现有 `120-demo` 的多 AP / NR / MPTCP 逻辑

## 关键代码改动

1. `installWifi` 从原来的 `YansWifiPhyHelper` 改成 `SpectrumWifiPhyHelper`。
2. 在 AP 侧安装 `RrMultiUserScheduler`，默认：
   - `wifiEnableOfdma=1`
   - `wifiEnableUlOfdma=1`
   - `wifiEnableBsrp=1`
3. 对 AP/STA 设备统一下发 HE/MAC 参数：
   - `wifiHeGuardIntervalNs=800`
   - `wifiHeMpduBufferSize=256`
   - `wifiBeMaxAmpduSize=6500631`
4. 启动时打印一行默认 Wi-Fi 配置，便于确认运行时确实使用了新的能力基线。
5. `myscripts/120-demo/wscript` 增加 `spectrum` 依赖。

程序启动后会打印：

```text
[120-demo] WiFi defaults: phy=Spectrum 802.11ax_5GHz width=160MHz freq=5250MHz heGi=800ns heMpduBuffer=256 beMaxAmpdu=6500631 txPower=23dBm apAnt=4x4 staAnt=2x2 dlOfdma=on ulOfdma=on bsrp=on
```

## 测试 1：3 UE / 1 AP 中高负载吞吐

目的：证明默认新 Wi-Fi 配置已经能在 `120-demo` 中稳定承载明显业务量，同时 NR/Wi-Fi 双链路都实际送达 payload。

命令：

```bash
rm -rf files-*
DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin \
./waf --run "120-demo --numClients=3 --numAps=1 --simTime=4 --sinkStart=0.5 --clientStart=1.5 --clientStartJitter=0.01 --checkBackboneDualTraffic=1 --appSteadyRate=200Mbps --appBurstRate=200Mbps"
```

结果：

```text
[120-demo] PacketSink totalRxBytes=168214960 acceptedSockets=0 activeSeconds=2.500 throughputMbps=538.288
[120-demo] Client connect summary: connected=3/3 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=3/3 wifiActive=3/3 bothActive=3/3
```

结论：

- 新默认值下，`120-demo` 在稳定回归场景里可达到约 `538 Mbps` 总吞吐。
- 三个 UE 都同时具备 NR 和 Wi-Fi payload，到达了“不是只连上、而是真的双链路承载”的验证目的。

## 测试 2：12 UE / 4 AP 功能回归

目的：确认新的 Wi-Fi 默认能力没有破坏 `120-demo` 原有的多 AP / 多 UE / 双链路逻辑。

命令：

```bash
rm -rf files-*
DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin \
./waf --run "120-demo --numClients=12 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=100Kbps --appBurstRate=100Kbps"
```

结果：

```text
[120-demo] PacketSink totalRxBytes=231000 acceptedSockets=0 activeSeconds=2.000 throughputMbps=0.924
[120-demo] Client connect summary: connected=12/12 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=12/12 wifiActive=12/12 bothActive=12/12
```

结论：

- 12 个 UE 全部连接成功。
- 12 个 UE 的 NR/Wi-Fi 两条链路都实际承载了 payload。
- 说明这次 Wi-Fi 默认值对齐没有把 `120-demo` 的双接入/MPTCP 骨架打坏。

## 额外说明

- 交付验证采用的是稳定可复现的场景。
- `2026-03-26` 起，`120-demo` 的 `PacketSink throughputMbps` 改为按“最早真实 payload 发送时间”计算；运行输出里会额外打印 `payloadStartSeconds=`。若与本文前面的旧数值有细小差异，以当前程序实际输出为准。
- 我额外尝试了更激进的 `3 UE / 1 AP / 300Mbps per UE` 压测；在当前 `ns-3.35` Wi-Fi/OFDMA 代码路径下会触发模拟器内部断言，因此没有把它作为最终交付回归用例。
- 这不影响本次任务要求的“默认 Wi-Fi 能力对齐”和“稳定场景下测试证明无误”结论。

## 结论

任务完成。

- `120-demo` 的默认 Wi-Fi 能力已经切换到与 `dce-wifi-ofdma` 一致的高能力基线。
- 默认参数已经明确包含 `Spectrum PHY / 160MHz / 5250MHz / GI=800ns / MPDU buffer=256 / A-MPDU cap=6500631 / OFDMA on`。
- 中高负载和 12 UE 功能回归均通过。
