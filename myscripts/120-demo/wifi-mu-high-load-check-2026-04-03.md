# 120-demo 高负载 Wi-Fi/MU 路径排查记录

## 1. 结论

本次排查的目标是解释：为什么 `myscripts/120-demo/120-demo.cc` 在 `30` 个 client、`1` 个 Wi-Fi AP（Access Point，接入点）、每节点 `100 Mbps`、`simTime=3s` 时，会在 Wi-Fi 多用户（MU, Multi-User）路径上触发断言。

最终确认有两个独立问题：

1. 之前出现的“`connected=0/...`、`PacketSink totalRxBytes=0`”并不是 optimized 编译本身坏了，而是运行环境里 `DCE_PATH` 漏了 `/root/bake/build/sbin`，导致 DCE 内部调用的 `ip` / `ss` 可执行文件找不到。
2. 在修正 `DCE_PATH` 后，`30 client / 1 AP / 100 Mbps / 3s` 场景会真正进入 Wi-Fi MU 路径，并触发一条真实的断言：`HE_MU` 传输里混入了一个非 HE（High Efficiency, 802.11ax）模式 `OfdmRate6Mbps`。

针对第 2 个问题，我做了一个局部修复：在 ns-3 的 `RrMultiUserScheduler` 中，如果 MU 路径意外拿到非 HE 的单用户速率，就先钳制为 `HeMcs0`，避免把非法 `mode` 塞进 `HE_MU` TXVECTOR。修完后，原来的 MU 断言不再立即出现，但该高负载场景在 `180s` 墙钟时间内仍未结束，说明后续还有性能/逻辑问题需要继续查。

---

## 2. 本次改动

### 2.1 全局改动：ns-3-dce 运行环境问题修复

文件：

- `build.md`
- `/root/.codex/skills/ns3-dce-dev/SKILL.md`

改动：

- 把 optimized/debug 运行环境里的 `DCE_PATH` 补成包含：
  - `/root/bake/build/bin_dce`
  - `/root/bake/build/sbin`

原因：

- `liblinux.so` 在 `/root/bake/build/bin_dce`
- `ip`、`ss` 这类 DCE 内部要执行的工具在 `/root/bake/build/sbin`

如果漏掉 `/root/bake/build/sbin`：

- debug 版常见现象：直接断言 `Executable 'ip' not found`
- optimized 版常见现象：程序不一定立刻崩，但 `ip route` 没有真正执行，最终表现为：
  - `connected=0/...`
  - `PacketSink totalRxBytes=0`

### 2.2 全局改动：ns-3 Wi-Fi MU 路径修复

文件：

- `../ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc`
- `../ns-3.35/src/wifi/model/wifi-tx-vector.cc`

改动：

1. 在 `wifi-tx-vector.cc` 中增强了断言信息，额外打印：
   - `staId`
   - `mode`
   - `preamble`
2. 在 `rr-multi-user-scheduler.cc` 中增加 `NormalizeHeMuMode(...)`：
   - 如果 MU 路径里拿到的 `WifiMode` 已经是 `HE`，原样保留
   - 如果拿到的是 `HT/VHT/OFDM` 等非 HE mode，则先替换成 `HeMcs0`

这样做的目的不是“最终优化速率策略”，而是先保证：

- `HE_MU` 的 `WifiTxVector` 不再带非法的非 HE mode
- 高负载场景可以继续往后运行，便于暴露下一个真正瓶颈

### 2.3 全局改动：ns-3-dce 中一个明确的 UB 修复

文件：

- `model/dce-time.cc`

改动：

- 修复了 `timer_t` 写入处的越界/未定义行为（Undefined Behavior, UB）

这不是本次 Wi-Fi/MU 断言的直接根因，但它是编译器已经明确报出的高风险问题，保留修复更稳。

---

## 3. 关键参数解释

### 3.1 `numClients=30`

- 含义：同时创建 `30` 个 UE/STA client
- 影响：client 越多，AP 侧队列、关联、BA（Block Ack，块确认）、MPTCP 子流并发和 MU 调度压力都会增大
- 本次作用：这是触发问题的关键规模，`3` 个 client 时问题不明显，`30` 个 client 时会进入更激进的 MU/OFDMA 路径

### 3.2 `numAps=1`

- 含义：只使用 `1` 个 Wi-Fi AP
- 影响：所有 Wi-Fi 终端竞争同一个 AP 的空口资源
- 本次作用：把负载集中到单个 AP，更容易触发 AP 侧的 MU 调度问题

### 3.3 `appSteadyRate=100Mbps` / `appBurstRate=100Mbps`

- 含义：每个 client 的 `RateDualModeApplication` 在 steady/burst 两个状态都按 `100 Mbps` 发流
- 单位：`Mbps`（Megabit per second，兆比特每秒）
- 影响：值越大，队列和链路调度压力越大，更容易把无线与协议栈边界情况暴露出来
- 本次作用：这是本次“高负载”定义的核心

### 3.4 `simTime=3.0`

- 含义：仿真在 `3.0 s` 时停止
- 单位：秒（s）
- 影响：窗口越短，启动阶段抖动、建链延迟、路由下发延迟的影响越大
- 本次作用：它足够短，因此任何初始化问题都会明显影响最终结果

### 3.5 `wifiEnableOfdma=1` / `wifiEnableUlOfdma=1` / `wifiEnableBsrp=1`

- `OFDMA`：Orthogonal Frequency Division Multiple Access，正交频分多址
- 含义：
  - `wifiEnableOfdma=1`：开启 AP 侧多用户调度器
  - `wifiEnableUlOfdma=1`：允许 UL（Uplink，上行）OFDMA
  - `wifiEnableBsrp=1`：允许 BSRP（Buffer Status Report Poll）相关上行调度
- 本次作用：这是触发 `RrMultiUserScheduler` 的必要条件

---

## 4. 复现命令

### 4.1 干净构建一套新的 optimized 目录

`ns-3.35`：

```bash
cd /root/bake/source/ns-3.35
env CCFLAGS='-O2 -g0' CXXFLAGS='-O2 -g0' \
python3 ./waf configure -d optimized \
  --disable-python --disable-tests --disable-examples \
  --prefix=/root/bake/build-opt-rel-check \
  -o build-opt-rel-check

python3 ./waf build install -j4 -o build-opt-rel-check
```

`ns-3-dce`：

```bash
cd /root/bake/source/ns-3-dce
env CCFLAGS='-O2 -g0' CXXFLAGS='-O2 -g0' \
python3 ./waf configure \
  --enable-opt \
  --disable-debug --disable-assert --disable-log --disable-python \
  --with-ns3=/root/bake/build-opt-rel-check \
  --with-glibc=/root/bake/build/glibc \
  --with-libaspect=/root/bake/build \
  --enable-kernel-stack=/root/bake/source/net-next-nuse-mptcp-0.92/arch \
  --prefix=/root/bake/build-opt-dce-rel-check \
  -o build-opt-dce-rel-check

python3 ./waf build install -j4 -o build-opt-dce-rel-check
```

### 4.2 运行时环境

```bash
export LD_LIBRARY_PATH=/root/bake/build-opt-rel-check/lib:/root/bake/build-opt-dce-rel-check/lib:$LD_LIBRARY_PATH
export DCE_PATH=/root/bake/build-opt-dce-rel-check/bin:/root/bake/build-opt-dce-rel-check/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce:$DCE_PATH
```

注意：

- 这里的 `/root/bake/build/sbin` 不能省
- 否则 `ip`、`ss` 找不到，结果会失真

### 4.3 小场景 sanity check

命令：

```bash
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel-check/lib:/root/bake/build-opt-dce-rel-check/lib \
DCE_PATH=/root/bake/build-opt-dce-rel-check/bin:/root/bake/build-opt-dce-rel-check/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
/root/bake/build-opt-dce-rel-check/bin/120-demo \
  --numClients=3 \
  --numAps=1 \
  --simTime=3.0 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --appSteadyRate=10Kbps \
  --appBurstRate=10Kbps \
  --checkBackboneDualTraffic=1 \
  --dumpMptcpConfig=1
```

当前结果：

```text
[120-demo] PacketSink totalRxBytes=4500 acceptedSockets=0 payloadStartSeconds=2.599 activeSeconds=0.401 throughputMbps=0.090
[120-demo] Client connect summary: connected=3/3 inProgress=0 totalConnectFailures=0
[120-demo] Backbone dual-traffic summary: nrActive=3/3 wifiActive=0/3 bothActive=0/3
```

说明：

- 当前修改没有把小场景跑坏
- 这说明环境修正和 MU 修复至少没有破坏最小基线

### 4.4 高负载复现命令

命令：

```bash
/usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' \
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel-check/lib:/root/bake/build-opt-dce-rel-check/lib \
DCE_PATH=/root/bake/build-opt-dce-rel-check/bin:/root/bake/build-opt-dce-rel-check/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
timeout 180s \
/root/bake/build-opt-dce-rel-check/bin/120-demo \
  --numClients=30 \
  --numAps=1 \
  --simTime=3.0 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --appSteadyRate=100Mbps \
  --appBurstRate=100Mbps
```

---

## 5. 修复前观测到的核心现象

在修复 `RrMultiUserScheduler` 之前，使用正确 `DCE_PATH` 运行高负载命令，会在大约 `1.347579400 s` 仿真时间触发断言：

```text
Only HE (or newer) modes authorized for MU
staId=28
mode=OfdmRate6Mbps
preamble=HE_MU
```

这说明：

- 当前 TXVECTOR 明确处于 `HE_MU`
- 但给 `staId=28` 塞入的用户 MCS 却是传统 OFDM 的 `OfdmRate6Mbps`

这在语义上就是非法组合，因此断言是合理的。

---

## 6. 根因分析

根因位于：

- `../ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc`

关键逻辑是：

1. 调度器在尝试构造 DL MU PPDU 时，会先为每个候选 STA 调一次：

```cpp
GetWifiRemoteStationManager ()->GetDataTxVector (mpdu->GetHeader ())
```

2. 这个调用返回的是“单用户数据帧”的 TXVECTOR。
3. 原逻辑直接把这个 SU（Single User，单用户）mode 拿来填 MU user info。
4. 在本场景下，部分 STA 会拿到站管器的回退默认值 `OfdmRate6Mbps`。
5. 这个 mode 随后被用于 `HE_MU` 的 `SetHeMuUserInfo()`，于是断言触发。

从定位上看，最关键的失配点不是 `WifiTxVector` 自己，而是：

> `RrMultiUserScheduler` 默认假设 `GetDataTxVector()` 在 MU 场景下一定返回 HE mode，但这个假设在高负载下不成立。

---

## 7. 修复后的当前状态

修复后，高负载命令的行为变成：

- 不再立即触发原来的 `Only HE (or newer) modes authorized for MU` 断言
- 但在 `timeout 180s` 内没有自然结束
- 当前 `stderr` 里能看到的明显错误只有：

```text
RTNETLINK answers: File exists
```

目前可以确定的是：

- 原来的 MU 非法 mode 问题已经被绕过
- 但 `30 client / 1 AP / 100 Mbps / 3s` 仍然是一个非常重的场景
- 后续慢/卡住的原因还需要继续拆分

---

## 8. 本次保留的代码修改

### 8.1 ns-3

- `../ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc`
- `../ns-3.35/src/wifi/model/wifi-tx-vector.cc`

### 8.2 ns-3-dce

- `model/dce-time.cc`

### 8.3 运行文档

- `build.md`

---

## 9. 你现在可以直接跑的命令

如果你只想验证“环境没问题 + 小场景正常”：

```bash
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel-check/lib:/root/bake/build-opt-dce-rel-check/lib \
DCE_PATH=/root/bake/build-opt-dce-rel-check/bin:/root/bake/build-opt-dce-rel-check/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
/root/bake/build-opt-dce-rel-check/bin/120-demo \
  --numClients=3 \
  --numAps=1 \
  --simTime=3.0 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --appSteadyRate=10Kbps \
  --appBurstRate=10Kbps \
  --checkBackboneDualTraffic=1 \
  --dumpMptcpConfig=1
```

如果你想验证“原来的 MU 断言已经不再立刻出现”：

```bash
/usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' \
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel-check/lib:/root/bake/build-opt-dce-rel-check/lib \
DCE_PATH=/root/bake/build-opt-dce-rel-check/bin:/root/bake/build-opt-dce-rel-check/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
timeout 180s \
/root/bake/build-opt-dce-rel-check/bin/120-demo \
  --numClients=30 \
  --numAps=1 \
  --simTime=3.0 \
  --sinkStart=0.5 \
  --clientStart=1.0 \
  --appSteadyRate=100Mbps \
  --appBurstRate=100Mbps
```

---

## 10. 后续建议

如果下一步继续排查，建议优先做下面两条之一：

1. 先把 `30 client / 100 Mbps` 缩成更小但仍触发 MU 的最小复现，比如：
   - `10 client / 1 AP / 100 Mbps`
   - `20 client / 1 AP / 50 Mbps`
2. 在当前 `30 client / 100 Mbps` 命令上继续开更少量、针对性的日志，只盯：
   - `RrMultiUserScheduler`
   - `HeFrameExchangeManager`
   - `WifiDefaultAckManager`

这样更容易判断“断言修完以后，后面的卡住到底是调度、ACK 序列、还是事件队列爆炸导致的”。
