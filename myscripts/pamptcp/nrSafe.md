
## 本次完成记录：`nrsafe`

新增调度器名称：`nrsafe`。

实现位置：

- `/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/mptcp_nrsafe.c`
- `/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/Kconfig`
- `/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/Makefile`
- `/root/bake/source/net-next-nuse-mptcp-0.92/arch/lib/defconfig`

核心设计：

- 非 P3 peak 流量优先走 NR，符合当前“稳态先固定 NR”的实验前提。
- P3 peak 流量先沿用 default 调度器的路径选择，保留多路径聚合能力。
- 如果 default 选择 Wi-Fi，直接允许发送。
- 如果 default 选择 NR，则进入 NR guard：
  - 记录每条 subflow 的最小 RTT、当前 RTT 和发送队列估计排队时延。
  - NR RTT 膨胀、NR 排队时延、NR cwnd 占用超过阈值时，认为继续发送 peak 会伤害 NR steady 延迟，转回 Wi-Fi。
  - 只有 NR 接近 clean baseline 时才补充 NR peak assist credit。
  - Wi-Fi 排队明显变高时进入 overflow assist，允许更大的 NR credit，但仍受 NR RTT/queue guard 约束。
  - NR peak assist 的发送上限同时受预测延迟预算约束：根据 `srtt + queue_delay + inflight/cwnd` 估计发送后路径延迟，并取绝对预算与 `min_rtt + slack` 两者更小值。这样不是单纯固定限速，而是类似 BBR 的 clean-baseline guard：只有 NR 仍接近最小时延基线时，peak 才能借用 NR。

构建与部署：

```bash
cd /root/bake/source/net-next-nuse-mptcp-0.92
make library ARCH=lib -j4
cp arch/lib/tools/libsim-linux-4.4.110.so /root/bake/build-opt-dce-rel/bin_dce/libsim-linux-4.4.110.so
cp arch/lib/tools/libsim-linux-4.4.110.so /root/bake/build/bin_dce/libsim-linux-4.4.110.so
```

验证命令模板：

```bash
./run-myscript.sh --mode opt --timeout 1800 --isolate-files \
  myscripts/pamptcp/pamptcp.cc -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak-8 \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=nrsafe
```

结果汇总（单位：ms；`delay_ms` 只统计 steady，`peak_delay_ms` 近似表示 peak 完成时间）：

| 场景 | 调度器 | steady mean | steady p95 | steady p99 | steady max | peak mean | peak p95 | peak p99 | peak max | NR 占比 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| p3-sub-peak-0 | default | 0.663 | 0.696 | 0.739 | 3.491 | 0.000 | 0.000 | 0.000 | 0.000 | 0.186% |
| p3-sub-peak-0 | blest | 0.696 | 0.696 | 2.417 | 3.491 | 0.000 | 0.000 | 0.000 | 0.000 | 0.186% |
| p3-sub-peak-0 | nrsafe | 4.578 | 6.004 | 6.306 | 6.490 | 0.000 | 0.000 | 0.000 | 0.000 | 99.850% |
| p3-sub-peak-4 | default | 1.789 | 5.799 | 18.483 | 18.486 | 179.226 | 196.978 | 292.464 | 320.976 | 22.244% |
| p3-sub-peak-4 | blest | 1.969 | 11.604 | 18.483 | 18.483 | 176.138 | 206.298 | 209.896 | 210.757 | 22.932% |
| p3-sub-peak-4 | nrsafe | 4.469 | 5.766 | 6.354 | 6.490 | 188.209 | 218.125 | 228.972 | 232.202 | 0.917% |
| p3-sub-peak-8 | default | 6.025 | 17.337 | 18.405 | 18.483 | 210.771 | 340.423 | 359.960 | 366.968 | 22.480% |
| p3-sub-peak-8 | blest | 5.048 | 18.232 | 18.477 | 19.715 | 281.074 | 504.117 | 1715.934 | 2535.994 | 21.981% |
| p3-sub-peak-8 | nrsafe | 4.343 | 5.763 | 6.413 | 6.490 | 307.075 | 583.443 | 997.672 | 1040.841 | 0.539% |

结论：

- `p3-sub-peak-0` 按任务说明不作为胜负判断；当前 steady 固定 NR，所以它自然不如 Wi-Fi-heavy 的 default/blest。
- `p3-sub-peak-4` 中，`nrsafe` 把 steady max 从 default/blest 的约 18ms 压到 6.49ms，peak max 为 232ms，仍在可接受范围。
- `p3-sub-peak-8` 中，`nrsafe` 同样把 steady max 压到 6.49ms，满足当前“稳态延迟优先、max 尽量 10ms 内”的目标；代价是 peak tail 变慢，peak max 约 1.04s。
- 当前版本适合作为“NR steady latency protective scheduler”：它不是 peak-only 最优，而是在 default 聚合基础上把 NR peak 注入限制为 clean-baseline 下的探测式 assist，避免 NR 上 steady 流量被 peak 拖出高尾延迟。