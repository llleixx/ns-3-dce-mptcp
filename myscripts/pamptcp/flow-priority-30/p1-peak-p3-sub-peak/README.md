# p1-peak-p3-sub-peak

## 设计目标

这个场景的目标不是单纯把低优先级流量打满，而是构造一个更容易解释的冲突：

- 优先级 1 中只有 1 台设备进入 `20 Mbps` 峰值，其余 5 台保持 `12 Kbps` 稳态。
- 优先级 2 的 11 台设备全部保持 `42 Kbps` 稳态。
- 优先级 3 中 10 台设备进入 `25 Mbps` 峰值，另外 3 台保持 `96 Kbps` 稳态。

这样可以直接观察：

1. 当一个高优先级设备自己也发生突发时，scheduler 是否还能保护 P1/P2 的时延与抖动。
2. 保护高优先级的代价，是否会明显伤害 P3 的吞吐。

## 运行命令

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p1-peak-p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --mptcpScheduler=default
```

把 `--mptcpScheduler=default` 改成 `pablest` 即可复现对比。

## 当前结果摘要

在 2026-04-15 这次运行中，`default` 与 `pablest` 的对比如下：

- P1 吞吐几乎不变：`20.055 -> 20.055 Mbps`
- P1 `p95 delay`：`4.075 -> 1.470 ms`
- P2 吞吐基本不变：`0.459 -> 0.452 Mbps`
- P2 `p95 delay`：`17.218 -> 2.172 ms`
- P3 吞吐几乎不变：`250.279 -> 250.290 Mbps`

链路占比的差异也很清楚：

- `default` 几乎把所有优先级都压在 NR 上，P1 `nr_share=99.72%`，P2 `nr_share=60.98%`
- `pablest` 把 P1/P2 稳定留在 Wi-Fi，把 P3 留在 NR，形成清晰的“低时延链路承载高优、小体量业务，NR 承载 bulk”分工

因此，这个场景能比 `p3-sub-peak/` 更直接地体现当前 `pablest` 的优势，而且解释成本更低。
