# 120-demo NR 默认能力对齐结果

日期：2026-03-26

## 改动目标

把 `myscripts/120-demo/120-demo.cc` 的默认 NR 配置对齐到 `myscripts/nr-dce` 的高能力配置，重点是：

- 默认启用 `NrEesmCcT2`，即 Table2 / 256QAM
- 默认把 UL 固定到更现实且更稳的高 MCS：`MCS=25`
- 默认使用 `4.9GHz / 100MHz / numerology=1 / DL|F|UL|UL|UL|`
- 默认 UE 发射功率 `23dBm`、gNB 发射功率 `30dBm`
- 默认 UE 天线改为 `1x2`，gNB 维持 `2x2`
- 默认把 EPC `S1uLinkDelay` 设为 `0ms`

## 关键默认值

当前程序启动时会打印：

```text
[120-demo] NR defaults: freq=4.9GHz bw=100MHz numerology=1 pattern="DL|F|UL|UL|UL|" errorModel=NrEesmCcT2(Table2,256QAM) amcModel=ErrorModel fixedMcsUl=1 startingMcsUl=25 fixedMcsDl=0 startingMcsDl=25 ueTxPower=23dBm gnbTxPower=30dBm ueAnt=1x2 gnbAnt=2x2
```

## 测试 1：NR-only 饱和吞吐对比

目的：验证默认 5G 能力确实比旧默认值高。

命令：

```bash
./waf --run "120-demo --numClients=3 --simTime=6 --sinkStart=1 --clientStart=2 --clientStartJitter=0.01 --enableMptcp=0 --appSteadyRate=150Mbps --appBurstRate=150Mbps"
```

结果：

- 修改前旧默认值：`throughputMbps=289.174`
- 修改后新默认值：`throughputMbps=362.579`

结论：

- 同一类 NR-only 饱和场景下，总吞吐提升约 `25.4%`
- 说明默认 64QAM 天花板已经被抬到更接近 `nr-dce` 中 256QAM / 高 MCS 的量级

## 测试 2：12 UE 双链路功能回归

目的：确认新的 NR 默认值没有把 `120-demo` 的双链路/MPTCP 场景打坏。

命令：

```bash
./waf --run "120-demo --numClients=12 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=100Kbps --appBurstRate=100Kbps"
```

结果：

```text
[120-demo] Client connect summary: connected=12/12 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=12/12 wifiActive=12/12 bothActive=12/12
```

## 测试 3：120 UE 目标规模回归

目的：确认目标规模下仍然保持原有联通性与双链路承载。

命令：

```bash
./waf --run "120-demo --numClients=120 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.3 --checkBackboneDualTraffic=1 --appSteadyRate=100Kbps --appBurstRate=100Kbps"
```

结果：

```text
[120-demo] Client connect summary: connected=120/120 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=120/120 wifiActive=120/120 bothActive=120/120
```

## 结论

任务完成。

- `120-demo` 的默认 NR 配置已经切到 256QAM / Table2 路线
- 默认 UL 速率能力明显高于修改前旧默认值
- 12 UE 和 120 UE 双链路回归均通过，没有破坏原有 MPTCP/双接入行为
