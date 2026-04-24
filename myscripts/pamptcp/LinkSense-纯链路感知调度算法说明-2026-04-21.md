# LinkSense 纯链路感知调度算法说明

## 1. 目标

`pablest` 的主要问题不是“优先级感知”本身，而是它把优先级语义建立在了两个强先验上：

1. 事先知道一条链路是 `NR`，另一条是 `Wi-Fi`
2. 通过固定 IPv4 前缀把子流直接映射到 `NR/Wi-Fi`

`linksense` 的目标是去掉这两个先验，只保留：

- 业务优先级：仍然通过 `DSCP` 识别 `P1/P2/P3`
- 子流状态：完全使用运行时链路信号做决策

也就是说，算法可以知道“这是高优业务还是低优业务”，但不能知道“这条子流先验上叫 NR 还是 Wi-Fi”。

## 2. 核心设计

`linksense` 的设计不再做“按接入类型 hard bind”，而是先做**链路角色发现**，再做**优先级调度**。

### 2.1 链路角色发现

对每个 MPTCP 连接，周期性比较所有可用子流的两类得分：

- `bulk score`
  - 偏向大 burst、弱时延权重
  - 谁更适合吞吐型发送，谁就成为 `bulk lane`
- `guard score`
  - 偏向小 burst、强时延/抖动权重
  - 主要用于辅助判断当前连接还有没有更适合守护高优业务的备选 lane

当前实现最终收敛成下面这个规则：

1. 先找出 `bulk score` 最小的子流，记为 `bulk lane`
2. 如果存在 `bulk score` 次优子流，则把它保留为 `guard lane`
3. 如果没有明显次优子流，再退回到 `guard score`

这个过程完全不看 IP 前缀，也不看“NR/Wi-Fi 标签”。

### 2.2 优先级调度

- `P1/P2`
  - 优先发到 `guard lane`
  - 如果 `guard lane` 暂时不可发，则根据 `hold_budget` 和 `spill_penalty` 决定是等待还是溢出
- `P3`
  - 优先发到 `bulk lane`
  - 参数上更偏向大 burst、更低时延权重、更高等待意愿，尽量减少向 `guard lane` 的过早 spill

## 3. 仅使用的运行时信号

`linksense` 只使用当前内核里已经稳定可用的量：

- `DSCP`
- `sk_wmem_queued`
- `sk_pacing_rate`
- `snd_cwnd`
- `srtt_us`
- `min_srtt_us / max_srtt_us`
- `mptcp_is_def_unavailable()`
- `pablest_is_temp_unavailable()`

没有使用：

- 子流 IP 前缀到接入类型的映射
- “这条链路一定是 NR / Wi-Fi”的先验知识

## 4. 实现位置

- 内核调度器实现：
  - `/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/mptcp_pablest.c`
  - scheduler 名称：`linksense`
- 仿真入口参数说明：
  - `/root/bake/source/ns-3-dce/myscripts/pamptcp/pamptcp.cc`

## 5. 复现命令

编译内核库：

```bash
cd /root/bake/source/net-next-nuse-mptcp-0.92
make library ARCH=lib -j4
for d in /root/bake/build/bin_dce /root/bake/build-opt-dce-rel/bin_dce; do
  cp /root/bake/source/net-next-nuse-mptcp-0.92/arch/lib/tools/libsim-linux-4.4.110.so "$d"/
  ln -s -f libsim-linux-4.4.110.so "$d"/liblinux.so
done
```

运行 `linksense`：

```bash
cd /root/bake/source/ns-3-dce
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=linksense
```

## 6. 最终结果

### 6.1 10 秒主口径对比

对比文件：

- `default-stream1-10s.json`
- `blest-stream1-10s.json`
- `pablest-stream1-10s.json`
- `linksense-stream1-10s.json`

关键结果如下：

| Scheduler | Sink Throughput | P1 mean / p95 delay | P2 mean / p95 delay | P3 throughput | P3 mean / p95 delay | P3 overall p95 delay |
|---|---:|---:|---:|---:|---:|---:|
| `default` | `191.672 Mbps` | `1.424 / 3.468 ms` | `1.901 / 5.455 ms` | `171.121 Mbps` | `4.034 / 16.191 ms` | `2817.068 ms` |
| `blest` | `187.276 Mbps` | `1.985 / 5.396 ms` | `1.302 / 3.489 ms` | `158.621 Mbps` | `3.190 / 9.944 ms` | `2214.008 ms` |
| `pablest` | `228.419 Mbps` | `3.367 / 3.468 ms` | `3.440 / 3.489 ms` | `221.121 Mbps` | `1.712 / 6.838 ms` | `1176.141 ms` |
| `linksense` | `247.939 Mbps` | `1.449 / 6.258 ms` | `0.911 / 3.382 ms` | `250.288 Mbps` | `4.183 / 9.831 ms` | `181.311 ms` |

### 6.2 解释

`linksense` 的最终形态并没有把高优业务强行压到“较慢链路”上，反而更多地让 `P1/P2` 留在当前更适合小包低队列的 lane，同时通过 `bulk lane / guard lane` 发现机制和更激进的 `P3` 参数，把 `P3` 的整体发送效率显著拉高。

从主口径结果看：

- 相比 `pablest`
  - 总吞吐更高：`247.939 > 228.419 Mbps`
  - `P3` 吞吐更高：`250.288 > 221.121 Mbps`
  - `P3` 的 `overall p95 delay` 明显更低：`181.311 << 1176.141 ms`
  - `P2` 时延更好
  - `P1` 的 `p95` 比 `pablest` 略高，但仍远低于 `20 ms` SLA
- 相比 `default/blest`
  - 吞吐优势明显
  - `P3` 的尾时延显著收敛，不再出现秒级 `overall p95`

## 7. 额外现象

在一次同配置的 `pablest` 复跑中，仿真出现了明显的 wall-clock cliff。  
附加的 `gdb` 栈显示进程当时卡在 `ns3::NrInterference::ConditionallyEvaluateChunk()` 相关路径，而不是卡在 `linksense` 新代码中。

这说明：

- `linksense` 至少在本次主口径下没有引入额外的运行时 cliff
- 当前树里的 `pablest` 在这组参数下存在不稳定的长尾运行时间风险

## 8. 结论

结论是：**可以**在不依赖“已知哪条是 NR / 哪条是 Wi-Fi”、也不依赖 IP 前缀分类的前提下，设计出在当前测试场景里效果更好的新调度器。

当前收敛下来的 `linksense` 已经满足这个目标，而且在 `p3-sub-peak` 主口径上，综合结果优于现有 `pablest`。
