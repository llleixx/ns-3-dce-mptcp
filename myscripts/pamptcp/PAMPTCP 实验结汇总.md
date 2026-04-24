# PAMPTCP 实验结论汇总（截至 2026-04-16）

## 0. 先结论

这轮实验最重要的结论有 4 条。

1. 目前最稳定、最可解释的方向，不是“按优先级或业务类型硬绑接入”，而是“让高优业务有轻量偏好，让低优 bulk 保持双链路自适应”。
2. 在当前 `ns-3.35 + DCE + 802.11ax UL OFDMA` 配置下，Wi-Fi 的实际 TCP payload goodput 明显低于“纸面上 1Gbps 左右”的直觉值；`3 x 150 Mbps` 已经足以把 Wi-Fi 压到明显排队区间。
3. 当前 delay 指标不是“空口传播时延”，而是“应用生成包到接收回调”的总时间，包含应用层、Linux TCP、MPTCP、Wi-Fi MAC 队列的等待。
4. 多个实验调度器已经排除了几条看起来直观、但实测不成立的方向：`P1/P2 -> NR, P3 -> Wi-Fi`、`peak -> Wi-Fi`、`peak -> Wi-Fi + 超预算回退`。

本报告把“从一开始到现在”的实验分成两类：

- `历史线索`：用于解释算法演化过程、反映早期尝试为什么会被放弃。
- `当前有效结果`：在修正内核库加载路径之后，能够真实反映当前代码状态的结果。

## 1. 改动范围

### 1.1 全局改动

- `net/mptcp/mptcp_pablest.c`
  - 当前 `pablest` 调度器的实现文件。
  - 同时内嵌了若干实验调度器：`testprio`、`testburst`、`testhybrid`。
  - 参考文件：`/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/mptcp_pablest.c`

- `myscripts/pamptcp/pamptcp.cc`
  - 增加了仅对 `testburst` / `testhybrid` 生效的 DSCP 打标逻辑，用于把“peak-like/steady-like”业务传给内核调度器。
  - 参考文件：`myscripts/pamptcp/pamptcp.cc`

- `ns-3.35` Wi-Fi 侧源码
  - 为了继续定位 Wi-Fi 上行调度问题，临时在 `RrMultiUserScheduler` 中加入了运行摘要。
  - 参考文件：`/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc`

### 1.2 新增实验场景目录

- `myscripts/pamptcp/flow-priority-30/p1-peak-p3-sub-peak/`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/`
- `myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6/`
- `myscripts/pamptcp/flow-priority-30/p3-only-3x140m/`
- `myscripts/pamptcp/flow-priority-30/p3-only-4x150m/`

## 2. 有效性说明

### 2.1 哪些结果是“当前代码有效结果”

本报告把下面这些文件视为当前阶段最可信的结果集合：

- `myscripts/pamptcp/flow-priority-30/p3-peak/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/pablest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testprio-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testburst-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testhybrid-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/pablest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testprio-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testburst-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testhybrid-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6/default-wifi-only-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-only-3x140m/default-wifi-only-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-only-4x150m/default-wifi-only-stream1-6s.json`

### 2.2 为什么要区分“历史线索”和“当前有效结果”

中途排查时发现，`run-myscript.sh` 的 `DCE_PATH` 在 `opt` 模式下会优先搜索：

- `${repo_root}/build-opt-dce-rel/bin_dce`
- `/root/bake/build-opt-dce-rel/bin_dce`

之后才会轮到 `/root/bake/build/bin_dce`。  
参考文件：`run-myscript.sh`

这意味着：如果只替换了 `/root/bake/build/bin_dce/liblinux.so`，而没有替换 `/root/bake/build-opt-dce-rel/bin_dce/liblinux.so`，那么 `opt` 场景仍然会加载旧内核库。

因此：

- 早期一部分 `pablest` 迭代结果，只能当成“历史线索”
- 不能直接拿来代表“当前 `mptcp_pablest.c` 的真实性能”

这不是推脱，而是实验控制变量的一部分。

## 3. 概念说明

### 3.1 `P95 / P99 / P99.9`

- `P95`：95% 的样本不超过这个值。
- `P99`：99% 的样本不超过这个值。
- `P99.9`：99.9% 的样本不超过这个值。

为什么重要：

- 均值（mean）只能看整体趋势。
- 高优先级业务是否“偶尔被拖死”，通常要看 `P99/P99.9`。

### 3.2 `SLA violation`

- `SLA` 是 `Service Level Agreement`，这里可以理解为“时延门限”。
- 本实验里门限按优先级区分：
  - `P1 = 20 ms`
  - `P2 = 40 ms`
  - `P3 = 100 ms`

参考文件：

- `myscripts/pamptcp/pamptcp-profile.cc`

### 3.3 `goodput`

- 这里的吞吐本质上是应用/有效载荷层面的吞吐，不是 PHY 标称速率。
- 因此“160 MHz / 802.11ax / 2x2 MIMO 看起来很大”，不等于 TCP payload 一定能接近纸面上限。

### 3.4 `RU`

- `RU` 是 `Resource Unit`（资源单元），是 802.11ax OFDMA 中给不同用户切分的频域资源。
- 本实验的关键点在于：上行 OFDMA 调度器会给多个 STA 分配等大小 RU，而不是让每个 STA 都独占整个 `160 MHz`。

## 4. 结论清单

### 结论 1：低负载基线下，调度器差异几乎看不出来

**结论**

在 `avg-only` 场景下，`default / blest / pablest` 的结果几乎相同，说明低负载时拓扑本身并没有给调度器制造有效压力。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/avg-only/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/avg-only/blest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/avg-only/pablest-stream1-6s.json`

**推断理由**

- 三者整体吞吐都约为 `1.751 Mbps`
- `P1/P2/P3` 的 `p95 delay`、`p95 jitter`、分流比例都几乎一致
- 这说明：如果 offered load 太低，调度器再复杂也很难体现出差别

### 结论 2：`default_v1` 和 `default_v1_full` 不能直接解决优先级保护问题

**结论**

把调度器改成“更像内核 MPTCP v1 default”的思路，本身并没有自动带来优先级保护。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-sub-peak/default_v1-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-sub-peak/default_v1_full-stream1-6s.json`

**推断理由**

- `default_v1`
  - `P1/P2` 仍然接近 `17 ms` 量级
  - `P3` 还出现了 `0.356%` 的 `SLA violation`
- `default_v1_full`
  - `P3 p95 delay` 达到 `39.407 ms`
  - `P3 SLA violation` 约 `1.978%`

所以，“更先进的默认调度器风格”不等于“更适合优先级业务”。

### 结论 3：历史版本 `pablest` 在中等压力下能显著保护高优先级，但主要靠接入分离

**结论**

在 `p3-sub-peak` 和 `p1-peak-p3-sub-peak` 这类中等压力场景里，早期 `pablest` 的确能显著降低 `P1/P2` 时延，但它的主要手段是把高优业务和低优 bulk 分到不同接入，而不是更细粒度的动态智能。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-sub-peak/default-stream201-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-sub-peak/default-stream301-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-sub-peak/pablest-stream201-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-sub-peak/pablest-stream301-6s.json`
- `myscripts/pamptcp/flow-priority-30/p1-peak-p3-sub-peak/default-stream201-6s.json`
- `myscripts/pamptcp/flow-priority-30/p1-peak-p3-sub-peak/pablest-stream201-6s.json`

**推断理由**

- `p3-sub-peak`
  - `stream201`：`P2 p95 delay 16.804 -> 1.281 ms`
  - `stream301`：`P1 p95 delay 16.468 -> 1.077 ms`
- `p1-peak-p3-sub-peak`
  - `P1 p95 delay 4.007 -> 1.467 ms`
  - `P2 p95 delay 17.236 -> 2.277 ms`

但对应的分流比例显示：

- `P1/P2` 几乎都被压到一侧
- `P3` 几乎都被压到另一侧

这说明它更像“接入隔离”，而不是“跨链路平衡”。

### 结论 4：当前有效结果里，`pablest` 比多个极端实验调度器更合理

**结论**

在 `p3-peak` 与 `p3-3peak-x6` 两个高压场景里，当前 `pablest` 不是完美最优，但比 `testprio / testburst / testhybrid` 这类硬映射实验策略明显更稳。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-peak/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/pablest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testprio-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testburst-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testhybrid-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/pablest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testprio-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testburst-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testhybrid-stream1-6s.json`

**推断理由**

- `testprio`：`P1/P2 -> NR, P3 -> Wi-Fi`
  - `p3-peak`：整体 `SLA violation = 23.671%`
  - `p3-3peak-x6`：整体 `SLA violation = 97.467%`
- `testburst`：`peak -> Wi-Fi, steady -> NR`
  - `p3-peak`：整体 `p95 delay = 343.177 ms`
- `testhybrid`
  - 比 `testburst` 温和，但在 `p3-peak` 上仍有 `3.348%` 的 `SLA violation`

相比之下，当前 `pablest`：

- `p3-peak`：整体 `SLA violation = 0`
- `p3-3peak-x6`：整体 `SLA violation = 0`
- 吞吐也没有明显掉队

所以，极端“按业务类型绑链路”的方向，已经可以视为被排除。

### 结论 5：`P1/P2 -> NR, P3 -> Wi-Fi` 是错误方向

**结论**

`testprio` 证明了：“高优放 NR、低优放 Wi‑Fi”这个看起来直观的策略，在当前拓扑上会把 Wi‑Fi 上的 bulk 完全压坏。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-peak/testprio-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testprio-stream1-6s.json`

**推断理由**

- `p3-peak`
  - `P3 p95 delay = 268.607 ms`
  - `P3 SLA violation = 23.710%`
- `p3-3peak-x6`
  - `P3 p95 delay = 619.958 ms`
  - `P3 SLA violation = 98.255%`

这说明：即便 `P1/P2` 被保护住了，单边把 bulk 压给 Wi‑Fi 仍然会整体失败。

### 结论 6：`peak -> Wi-Fi` 也不是可直接采用的规则

**结论**

无论是强制版 `testburst`，还是带预算回退的 `testhybrid`，都说明“只要是 peak 流量就更适合 Wi‑Fi”这条规则并不成立。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-peak/testburst-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/testhybrid-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testburst-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/testhybrid-stream1-6s.json`

**推断理由**

- `testburst`
  - `p3-peak` 上几乎把 `P3` 全推到 Wi‑Fi：`wifi_share = 96.9%`
  - 结果 `P3 p95 delay = 343.394 ms`
- `testhybrid`
  - 在 `p3-peak` 上也依然很差：`P3 p95 delay = 75.265 ms`

这说明“peak 流量”的标签本身不足以决定接入链路。

### 结论 7：当前 `pablest` 的真实优点是“避免极端错误”，不是“全面压过 default”

**结论**

当前 `pablest` 更像是一个“更稳妥的折中版本”，而不是在所有指标上都明显压过 `default`。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-peak/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-peak/pablest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-3peak-x6/pablest-stream1-6s.json`

**推断理由**

- `p3-peak`
  - `P1/P2 p95` 确实比 `default` 更低
  - 但整体 `p99` 比 `default` 略差
- `p3-3peak-x6`
  - 吞吐略高：`458.927 -> 459.241 Mbps`
  - `P1` 长尾显著改善：`p99 41.715 -> 17.065 ms`
  - 但 `P1/P2 p95` 不是全面更优

所以，更准确的说法是：

- `pablest` 当前已经是“合理版本”
- 但还不是“无条件优于 default 的最终版本”

### 结论 8：当前 delay 指标天然包含发送侧排队时间

**结论**

当前 `delay` 统计不是“空口时延”，而是“应用层创建包到接收回调”的总时间，因此发送侧排队会直接放大 delay。

**证据文件**

- `myscripts/app-test/rate-dual-application.cc`
- `/root/bake/source/ns-3.35/src/applications/model/seq-ts-header.cc`
- `myscripts/pamptcp/pamptcp-report.cc`

**推断理由**

- `SeqTsHeader` 在构造时就写 `Simulator::Now()`  
  参考：`seq-ts-header.cc`
- `RateDualModeApplication` 在 `Send()` 前创建 header  
  参考：`rate-dual-application.cc`
- 接收端直接算 `now - header.GetTs()`  
  参考：`pamptcp-report.cc`

因此：

- 应用层等待
- Linux TCP 发送缓冲等待
- MPTCP 子流等待
- Wi-Fi MAC 队列等待

都会被算进 delay。

### 结论 9：`3 x 150 Mbps / Wi-Fi-only` 已经足够把 Wi‑Fi 打进重排队区间

**结论**

单独 3 台 `150 Mbps` 峰值设备、纯 Wi‑Fi、没有 MPTCP、没有其他业务时，delay 就已经能达到 `100 ms` 量级。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6/default-wifi-only-stream1-6s.json`

**推断理由**

- 吞吐只有 `436.188 Mbps`
- `p95 delay = 105.293 ms`
- `p99 delay = 124.672 ms`

这说明：

- 不是“混合负载 + 调度器”才制造出 `80 ms`
- 就算把问题缩到最小，Wi‑Fi 自身已经会进入重排队区间

### 结论 10：`3 x 140 Mbps` 时延确实显著下降，说明 `150 Mbps` 已经贴近或超过当前 Wi‑Fi 实际承载上限

**结论**

把单流速率从 `150 Mbps` 降到 `140 Mbps`，时延明显下降；因此 `150 Mbps` 附近已经是当前 Wi‑Fi 配置的敏感区间。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6/default-wifi-only-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-only-3x140m/default-wifi-only-stream1-6s.json`

**推断理由**

- `3 x 150 Mbps`
  - `p95 delay = 105.293 ms`
  - 吞吐 `436.188 Mbps`
- `3 x 140 Mbps`
  - `p95 delay = 45.077 ms`
  - 吞吐 `407.198 Mbps`

这说明 `150 Mbps` 并不是“离上限还很远”的配置。

### 结论 11：`4 x 150 Mbps` 比 `3 x 150 Mbps` 更差，说明“3 个 STA 只能服务 2 个”不是唯一主因

**结论**

`4 x 150 Mbps / Wi‑Fi-only` 的 delay 比 `3 x 150 Mbps` 更差，这直接说明：  
之前从 RU 分配代码得到的“3 个 STA 一轮只能排 2 个”的现象，不能单独解释时延问题。

**证据文件**

- `myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6/default-wifi-only-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/p3-only-4x150m/default-wifi-only-stream1-6s.json`
- `/root/bake/source/ns-3.35/src/wifi/model/he/he-ru.cc`
- `/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc`

**推断理由**

- 代码层面：
  - `3 STA` 时，`GetEqualSizedRusForStations(160, 3, ...)` 会把可分配 STA 数缩到 `2`
  - 这是个真实机制
- 但实验层面：
  - `3 x 150 Mbps`: `p95 = 105.293 ms`
  - `4 x 150 Mbps`: `p95 = 141.358 ms`

如果“3 个 STA 只能服务 2 个”是主要原因，那么 `4 个 STA` 应该更容易整分 RU，时延理应下降。  
事实相反，所以 RU 个数问题最多只是次要因素，不是根因。

### 结论 12：更底层的代码原因，是 UL OFDMA 的等尺寸 RU 分配 + Trigger 交换开销，共同压低了实际 payload goodput

**结论**

当前 Wi‑Fi 的真正瓶颈，不是“配置纸面太差”，而是 `ns-3` 的 UL OFDMA 实现机制：

- 上行调度器优先给候选 STA 分配等尺寸 RU
- RU 宽度受 `HeRu::GetEqualSizedRusForStations()` 约束
- 每轮还要付 Trigger / SIFS / ACK 的固定开销

这会把“纸面容量”显著折算成更低的实际 payload goodput。

**证据文件**

- `/root/bake/source/ns-3.35/src/wifi/model/he/rr-multi-user-scheduler.cc`
- `/root/bake/source/ns-3.35/src/wifi/model/he/he-ru.cc`

**推断理由**

- RU 带宽映射：
  - `RU_996_TONE = 80 MHz`
  - `RU_484_TONE = 40 MHz`
  - 参考：`he-ru.cc`
- `TrySendingBasicTf()` 会：
  - 根据候选站数选 RU 大小
  - 再用 Trigger 帧组织上行 MU 交换
  - 并明确扣除 Trigger / SIFS / ACK 的时间
  - 参考：`rr-multi-user-scheduler.cc`

因此，“160MHz 看起来很富余”这个直觉，在当前 UL OFDMA 实现里并不会直接转化成等价的 TCP payload capacity。

### 结论 13：Linux `Send-Q` 的实测已经达到 MB 级，足以单独解释 `80ms` 量级 delay

**结论**

在 `3 x 150 Mbps / Wi‑Fi-only` 的多时刻 `ss -tin` 采样里，某些发送端的 Linux `Send-Q` 明确涨到了 `1.59 MB` 左右，这单独就足够解释 `80ms` 量级 delay。

**证据文件**

- `files-1/var/log/26491/stdout`
- `files-0/var/log/27552/stdout`
- `files-2/var/log/11289/stdout`

**推断理由**

- 例如：
  - `Send-Q = 1,590,096 B`
- 同一场景每流实际吞吐约为 `145 Mbps`
- 换算排队时间约为：
  - `1.59 MB * 8 / 145 Mbps ≈ 87.7 ms`

这与观测到的 `80–100 ms` delay 在同一量级。

### 结论 14：Wi‑Fi MAC queue 默认也允许较深排队，因此 delay 不会只由 Linux `Send-Q` 决定

**结论**

除了 Linux `Send-Q`，`ns-3` 的 `WifiMacQueue` 默认本身也允许较深排队，这会进一步放大应用层 delay。

**证据文件**

- `/root/bake/source/ns-3.35/src/wifi/model/wifi-mac-queue.cc`
- `/root/bake/source/ns-3.35/src/wifi/helper/wifi-helper.cc`

**推断理由**

- 默认 `WifiMacQueue` 参数：
  - `MaxSize = 500p`
  - `MaxDelay = 500 ms`
- `WifiHelper` 会把 `NetDeviceQueueInterface` 的 trace 接到 `WifiMacQueue`

这意味着：

- 发送队列不止一层
- 即使 Linux `Send-Q` 没到极端值，MAC queue 的等待仍会体现在最终 delay 里

## 5. 当前最可靠的总判断

### 5.1 已经被排除的方向

- `P1/P2 -> NR, P3 -> Wi-Fi`
- `peak -> Wi-Fi`
- `peak -> Wi-Fi + 超预算回退`

### 5.2 当前还成立的方向

- 高优先级需要轻量保护
- 低优 bulk 必须继续双链路自适应
- 不能只按“业务类别”把流量钉在单接入上

### 5.3 当前还没有完全解决的问题

当前 `pablest` 已经从“明显错误”进步到了“能用的折中版本”，但还没有达到“所有关键指标都显著优于 default”的程度。

从实验结果看，真正还需要继续优化的点是：

- `P1/P2` 的保护要更稳
- `P3` 的 Wi-Fi/NR 分流要继续保持容量感知
- 不能再走极端硬分流路线

## 6. 复现建议

如果只想复现实验里最有代表性的几组结果，建议优先跑：

1. `p3-peak/default`
2. `p3-peak/pablest`
3. `p3-peak/testprio`
4. `p3-peak/testburst`
5. `p3-3peak-x6/default`
6. `p3-3peak-x6/pablest`
7. `p3-only-3peak-x6/wifi-only`
8. `p3-only-3x140m/wifi-only`
9. `p3-only-4x150m/wifi-only`

这几组已经足够支撑本报告里的主结论。

## 7. 复现命令索引

下面把文中多次引用的关键结果文件，逐一对应到实际运行命令。除特别说明外，默认工作目录都是：

```bash
cd /root/bake/source/ns-3-dce
```

### 7.1 30 设备主场景

#### `avg-only`

对应结果文件：

- `myscripts/pamptcp/flow-priority-30/avg-only/default-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/avg-only/blest-stream1-6s.json`
- `myscripts/pamptcp/flow-priority-30/avg-only/pablest-stream1-6s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/avg-only \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/avg-only \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=blest
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/avg-only \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=pablest
```

#### `p3-sub-peak`

对应结果文件：

- `default-stream201-6s.json`
- `default-stream301-6s.json`
- `pablest-stream201-6s.json`
- `pablest-stream301-6s.json`
- `default_v1-stream1-6s.json`
- `default_v1_full-stream1-6s.json`
- `blest-stream1-6s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=201 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=301 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=201 \
  --mptcpScheduler=pablest
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=301 \
  --mptcpScheduler=pablest
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=default_v1
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=default_v1_full
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=blest
```

#### `p1-peak-p3-sub-peak`

对应结果文件：

- `default-stream201-6s.json`
- `pablest-stream201-6s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p1-peak-p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=201 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p1-peak-p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=201 \
  --mptcpScheduler=pablest
```

#### `p3-peak`

对应结果文件：

- `default-stream1-6s.json`
- `pablest-stream1-6s.json`
- `testprio-stream1-6s.json`
- `testburst-stream1-6s.json`
- `testhybrid-stream1-6s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=pablest
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=testprio
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=testburst
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=testhybrid
```

#### `p3-3peak-x6`

对应结果文件：

- `default-stream1-6s.json`
- `pablest-stream1-6s.json`
- `testprio-stream1-6s.json`
- `testburst-stream1-6s.json`
- `testhybrid-stream1-6s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=pablest
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=testprio
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=testburst
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=testhybrid
```

### 7.2 纯 Wi-Fi 隔离场景

下面这些场景都用 `--pathMode=wifi-only`，目的是把 MPTCP 和 NR 影响拿掉，只看 Wi-Fi 侧行为。

#### `p3-only-3peak-x6`

对应结果文件：

- `default-wifi-only-stream1-6s.json`
- `default-wifi-only-stream1-3s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --pathMode=wifi-only \
  --mptcpScheduler=default
```

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6 \
  --numAps=1 \
  --simTime=3 \
  --clientStartJitterStream=1 \
  --pathMode=wifi-only \
  --mptcpScheduler=default
```

如果要复现多时刻 `ss -tin` 采样，则使用：

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --pathMode=wifi-only \
  --mptcpScheduler=default \
  --dumpSockets=1 \
  --dumpSocketCount=3
```

#### `p3-only-3x140m`

对应结果文件：

- `default-wifi-only-stream1-6s.json`

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-3x140m \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --pathMode=wifi-only \
  --mptcpScheduler=default
```

#### `p3-only-4x150m`

对应结果文件：

- `default-wifi-only-stream1-6s.json`

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

### 7.3 说明

- 上面命令都默认通过 `run-myscript.sh` 进入 `opt` 构建树，结果文件命名与 `clientStartJitterStream`、`simTime` 自动对应。
- `testburst` / `testhybrid` 两个调度器依赖 `pamptcp.cc` 中的额外 DSCP 标记逻辑，因此它们不能简单替换成别的发包程序直接复现。
- 本文未把中途废弃的、只作为“历史线索”的早期 `pablest` 版本命令全部展开，因为这些版本已经不代表当前代码状态。
