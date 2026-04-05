# MPTCP 多流验证

实验日期：2026-03-24  
实验目录：`myscripts/120-demo/`  
相关程序：`myscripts/120-demo/120-demo.cc`

## 一、结论先行

本次排查的核心结论是：`120-demo` 中的 WiFi 子流并不是“起不来”，而是我最开始使用的验证方法有问题。  
在开启多路径传输控制协议（Multipath TCP, MPTCP）后，`PacketSink` 的 `RxWithAddresses` 回调看到的是应用层的 **meta-socket** 视角，而不是每条 **subflow（子流）** 的真实承载地址，因此用它按源地址区分 NR 和 WiFi 会出现误判。  
真正可靠的证据来自两处：一是 DCE Linux 内核日志中的 `mptcp_add_sock` / `mptcp_init4_subsockets`，二是仿真末尾通过 `ss -tin` 看到的每条 TCP 子流状态与 `bytes_acked` / `bytes_received` 统计。  
这些证据表明，默认低负载下和高负载下，WiFi 子流都已经建立；在高负载下，WiFi 子流还实际承载了大量数据。  
因此，本次问题的根因不是路由缺失、接口未 up、或 MPTCP 无法建立 WiFi 子流，而是“实验观测口径选错了”。

## 二、问题背景

本次验证的目标是确认：在 `120-demo` 中，每个 client 是否真的同时通过 5G NR（New Radio，5G 空口）和 WiFi 两条链路参与 MPTCP 传输。

这里有三个很容易混淆的概念：

- **MPTCP（Multipath TCP，多路径 TCP）**：在应用层看来仍然像一个 TCP 连接，但内核可以在底层维护多条 TCP 子连接。
- **meta-socket（元套接字）**：MPTCP 对应用暴露的统一连接视图。应用层收发数据时看到的是它，而不是某一条具体子流。
- **subflow（子流）**：MPTCP 底层的单条 TCP 连接。每条子流都有自己的四元组，例如 `7.0.0.2:58457 -> 172.16.0.1:5000` 或 `10.10.0.2:54511 -> 172.16.0.1:5000`。

本次实验一开始误把应用层 `PacketSink` 的地址统计当成“按子流统计”。这在普通 TCP 场景通常没问题，但在 MPTCP 场景会失真，因为应用层看到的是 meta-socket 已经重组后的数据。

## 三、为什么最初的验证结论是错的

最初我给 `120-demo` 增加了一个 `--verifyDualLinks=1` 开关，试图用 server 端 `PacketSink/RxWithAddresses` 按源 IP 统计收到的 payload 字节数。

当时的现象是：

- `120` 个 UE（User Equipment，用户设备）短窗口测试时，日志显示 `wifiActive=0/120`
- `12` 个 UE 和 `3` 个 UE 的测试里，也出现了 `wifiActive=0/N`

这个现象看起来像“WiFi 子流完全没有传数据”，但后续证明这个结论不成立。

原因是：

- `PacketSink` 是应用层接收程序。
- MPTCP 会把不同 subflow 上收到的数据先在内核里重组，再交给应用层。
- 因此，`RxWithAddresses` 观察到的地址不适合用来区分“这段 payload 到底是走 NR 子流还是 WiFi 子流来的”。

换句话说，**这个方法可以看总吞吐，不能看每条 MPTCP 子流的承载情况。**

## 四、本次新增的调试开关与作用

为了把排查做成可复现流程，我在 `120-demo.cc` 中加入了若干调试参数。

| 参数 | 类型 | 本次取值 | 作用 | 单位/范围 | 对结果的影响 |
| --- | --- | --- | --- | --- | --- |
| `numClients` | 无符号整数 | `3`、`12`、`120` | client 数量 | 个，正整数 | 越大仿真越重，墙钟时间明显增加 |
| `simTime` | 浮点数 | `4`、`6`、`20` | 仿真停止时间 | 秒，`>0` | 太短可能只够建连，不足以观察稳定传输 |
| `sinkStart` | 浮点数 | `1` | server 端 `PacketSink` 启动时间 | 秒 | 应早于 client 发流 |
| `clientStart` | 浮点数 | `2` | client 应用启动时间 | 秒 | 太早可能遇到路由/关联尚未稳定 |
| `enableMptcpDebug` | 布尔 | `1` 或 `0` | 打开 Linux 内核 MPTCP 调试 | `0/1` | 开启后 `files-*/var/log/messages` 会出现 `mptcp_add_sock` 等日志 |
| `dumpSockets` | 布尔 | `1` 或 `0` | 在仿真末尾通过 DCE 运行 `ss -tin` | `0/1` | 这是本次最终采用的真实子流验证方法 |
| `appSteadyRate` | 速率字符串 | `50.9Kbps`、`20Mbps` | `RateDualApplication` 稳态速率 | bps / Kbps / Mbps | 越大，steady 状态下持续提供给 TCP 的负载越高 |
| `appBurstRate` | 速率字符串 | `92.79Mbps`、`20Mbps` | `RateDualApplication` 突发速率 | 同上 | 越大，burst 状态的瞬时 offered load（提供给 TCP 的应用负载）越高 |
| `appBurstProb` | 双精度浮点 | `0.0005`、`0` | 仅 `legacy-probability` 模式使用：进入 burst 状态的概率 | `[0,1]` | 当前文中主要命令未显式使用；旧接口兼容场景下才需要 |
| `mptcpScheduler` | 字符串 | `default`、`roundrobin`、`blest` | MPTCP 调度器 | 取决于内核模块 | 决定不同 subflow 之间如何分配数据 |

### 这些参数为什么重要

- `simTime` 决定观测窗口。`120` 个 UE 时，哪怕只跑 `4s` 的模拟时间，墙钟也可能达到数分钟。
- `appSteadyRate`、`appBurstRate`，以及所选 traffic model 的持续时间/概率参数，共同决定应用层到底给 TCP 提供多少数据。  
  如果 offered load 很低，即使两条子流都存在，也可能每条只传很少字节。
- `dumpSockets=1` 很关键，因为 `ss -tin` 是直接读内核 socket 状态，比应用层统计更贴近“每条 subflow 是否真的在收发”。

## 五、实验设计

### 1. 控制变量

为了定位问题，我分三类实验：

- **实验 A：错误方法复现**  
  用 `PacketSink/RxWithAddresses` 统计，复现 `wifiActive=0`
- **实验 B：内核级别建流验证**  
  打开 `enableMptcpDebug=1`，看 `files-*/var/log/messages`
- **实验 C：socket 级别传输验证**  
  打开 `dumpSockets=1`，用 `ss -tin` 看 `ESTAB`、`bytes_acked`、`bytes_received`

### 2. 为什么先用 `3` 个 UE 做控制实验

`120` 个 UE 时仿真太重，单次验证的墙钟时间很长。  
而“WiFi 子流是否能建立、是否承载数据”这个问题，本质上是单连接行为问题，不需要先上 `120` 个 UE 才能判断。  
因此我先用 `3` 个 UE 做根因定位，确认方法正确后，再把结论解释回 `120` 个 UE 场景。

## 六、实验步骤与现象

### 实验 A：先复现最初的错误结论

命令：

```bash
./waf --run "120-demo --numClients=3 --simTime=4 --sinkStart=1 --clientStart=2 --verifyDualLinks=1"
```

现象：

```text
[120-demo] Client connect summary: connected=3/3 inProgress=0 totalConnectFailures=0
[120-demo] verifyDualLinks warning: PacketSink/RxWithAddresses observes the MPTCP meta-socket peer, not per-subflow payload. Use --dumpSockets=1 for real subflow verification.
```

说明：

- 现在程序会主动提示这个方法在 MPTCP 下不可靠。
- 这是为了避免后续再次把 `wifiActive=0` 误解为“WiFi 子流没起来”。

### 实验 B：用 MPTCP 内核调试日志确认 WiFi 子流被创建

命令：

```bash
./waf --run "120-demo --numClients=3 --simTime=6 --sinkStart=1 --clientStart=2 --enableMptcpDebug=1 --dumpSockets=1"
```

重点查看的日志文件：

- client 侧：`files-0/var/log/messages`、`files-1/var/log/messages`、`files-2/var/log/messages`
- server 侧：`files-8/var/log/messages`

关键日志片段：

```text
net/mptcp/mptcp_ctrl.c: mptcp_add_sock: token 0x601d1a2e pi 1, src_addr:7.0.0.2:58457 dst_addr:172.16.0.1:5000, cnt_subflows now 1
net/mptcp/mptcp_ctrl.c: mptcp_add_sock: token 0x601d1a2e pi 2, src_addr:0.0.0.0:0 dst_addr:0.0.0.0:0, cnt_subflows now 2
net/mptcp/mptcp_ipv4.c: mptcp_init4_subsockets: token 0x601d1a2e pi 2 src_addr:10.10.0.2:0 dst_addr:172.16.0.1:5000 ifidx: 8
```

server 侧对应地也出现：

```text
net/mptcp/mptcp_ctrl.c: mptcp_add_sock: token 0x609a4db6 pi 2, src_addr:172.16.0.1:5000 dst_addr:10.10.0.2:54511, cnt_subflows now 2
```

解释：

- `pi 1` 可以理解为第一条子流，这里是初始 NR 路径。
- `pi 2` 是后续创建的第二条子流，这里对应 WiFi 路径。
- `src_addr:10.10.0.2` 这一类地址属于 WiFi 子网，所以它已经不是“还没尝试建子流”，而是 **内核已经创建了 WiFi 子流**。

这一步已经足够推翻“WiFi 子流起不来”的猜测。

### 实验 C：用 `ss -tin` 直接看每条 subflow 是否真的有数据统计

同样使用实验 B 的命令，重点看 DCE 中由 `ss -tin` 产生的 stdout。

`files-0`（UE 0）中的输出：

```text
ESTAB 10.10.0.2%sim0:54511 172.16.0.1:5000 bytes_acked:1001
ESTAB 7.0.0.2:58457        172.16.0.1:5000 bytes_acked:1001
```

`files-8`（server）中的输出：

```text
ESTAB 172.16.0.1:5000 10.10.0.2:54511 bytes_received:2000
ESTAB 172.16.0.1:5000 7.0.0.2:58457  bytes_received:1000
```

解释：

- `ESTAB` 表示 TCP 连接已建立（Established）。
- `bytes_acked` 表示本端已发送并且被对端确认（acknowledged）的字节数。
- `bytes_received` 表示本端该 socket 已经收到的字节数。

这说明：

- NR 子流有字节统计。
- WiFi 子流也有字节统计。
- 因而“WiFi 子流不传数据”这个说法不成立。

### 实验 D：提高 offered load，观察两条子流的数据分布

命令：

```bash
./waf --run "120-demo --numClients=3 --simTime=6 --sinkStart=1 --clientStart=2 --enableMptcpDebug=1 --dumpSockets=1 --appSteadyRate=20Mbps --appBurstRate=20Mbps"
```

这样设置的含义：

- `appSteadyRate=20Mbps`：steady 状态固定到 20 Mbps。
- `appBurstRate=20Mbps`：burst 状态也固定到 20 Mbps，因此无论当前处于 steady 还是 burst，应用层看到的目标发流速率都近似恒定。
- 这样做的目的不是“更真实”，而是“更容易观察子流是否被真正使用”。

关键现象：

UE 0：

```text
10.10.0.2%sim0:54511 ... bytes_acked:8868861
7.0.0.2:58457        ... bytes_acked:44141
```

server：

```text
172.16.0.1:5000 10.10.0.2:54511 bytes_received:8884712
172.16.0.1:5000 7.0.0.2:58457  bytes_received:44140
```

解释：

- 这次 WiFi 子流不仅建立了，而且承载了绝大部分数据。
- NR 子流仍然存在，但实际承载量明显小于 WiFi。

这进一步说明：问题并不是 WiFi 子流完全无效，而是 **MPTCP 在不同负载和不同 socket 状态下，会动态决定各条 subflow 的数据分配比例。**

## 七、为什么 `PacketSink` 的源地址统计会误导

这是本次实验里最重要的经验。

`PacketSink` 属于应用层程序。  
而 MPTCP 的核心特性是：多个 subflow 在内核中汇聚成一个对应用透明的 meta-socket。  
因此，应用层看到的是“这个 MPTCP 连接收到了多少数据”，而不是“哪条 subflow 交付了多少数据”。

如果直接拿 `PacketSink/RxWithAddresses` 做 per-subflow 判断，会出现两个问题：

- 可能所有 payload 都被记到初始连接视角上。
- 也可能不同内核实现下表现不一致，导致统计口径不稳定。

所以在 MPTCP 场景中：

- **应用层统计适合看总吞吐**
- **socket/内核统计适合看每条子流**

## 八、对 `120` 个 UE 场景的解释

虽然本次最终根因定位主要依赖 `3` 个 UE 的控制实验，但它足以解释 `120` 个 UE 场景中最初的异常现象：

- 先前出现的 `wifiActive=0/120` 不是“120 个 UE 时 WiFi 都坏了”
- 而是“用于判定 WiFi 是否传输数据的统计口径错误”

更严格地说：

- 先前 `120` UE 结论只能说明：`PacketSink/RxWithAddresses` 没法正确反映 MPTCP 子流分布
- 不能说明：WiFi subflow 没建立
- 也不能说明：WiFi subflow 没传输

如果要对 `120` 个 UE 做真正的多流覆盖率统计，后续应采用以下方式：

- 用 `--dumpSockets=1` 对部分 UE 抽样
- 或进一步在程序里自动解析 `ss -tin` 输出，提取每个 UE 的 NR/WiFi `bytes_acked`

## 九、复现步骤

### 1. 清理旧的 DCE 输出目录

```bash
find . -maxdepth 1 -type d -name 'files-*' -prune -exec rm -rf {} +
```

### 2. 复现内核建流证据

```bash
./waf --run "120-demo --numClients=3 --simTime=6 --sinkStart=1 --clientStart=2 --enableMptcpDebug=1 --dumpSockets=1"
```

### 3. 查看 MPTCP 内核日志

```bash
for f in files-{0,1,2,8}/var/log/messages; do
  echo "== $f =="
  rg -n "mptcp_add_sock|mptcp_init4_subsockets" "$f"
done
```

### 4. 查看 `ss -tin` 输出

```bash
for f in $(find files-* -path '*/cmdline' | sort); do
  if sed -n '1p' "$f" | rg -q '^ss '; then
    echo "== $f =="
    out=${f%cmdline}stdout
    sed -n '1,120p' "$out"
  fi
done
```

### 5. 如果需要提高 offered load

```bash
./waf --run "120-demo --numClients=3 --simTime=6 --sinkStart=1 --clientStart=2 --enableMptcpDebug=1 --dumpSockets=1 --appSteadyRate=20Mbps --appBurstRate=20Mbps"
```

## 十、最终结论

本次排查最终确认：

- WiFi 子流可以正常建立。
- WiFi 子流可以正常传输数据。
- 初始“WiFi 完全没有数据”的结论是由错误的应用层观测方法导致的。

因此，本次问题的真正根因是：

**在 MPTCP 场景下，使用 `PacketSink/RxWithAddresses` 去做 per-subflow 验证是方法错误，不是网络配置错误。**

## 十一、后续建议

- 如果后续要做 `120` 个 UE 的批量多流统计，建议增加一个自动解析 `ss -tin` 的脚本，把每个 UE 的 NR/WiFi `bytes_acked` 归档成表格。
- 如果目标是研究“两个子流分别承担多少业务量”，应继续使用 `ss -tin` 或内核 MPTCP trace，而不是应用层 `PacketSink` 地址统计。
- 如果目标是研究“整体业务吞吐是否提高”，则 `PacketSink totalRxBytes` 仍然是有效指标。
