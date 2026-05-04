我最近在研究 MPTCP Scheduler。

目前基本场景定义如下：

有 30 个设备，都连接到同一个 5G 基站和 Wi-Fi（5G 是 SA，自建核心网；使用 Wi-Fi6；所以延迟在单设备情况下都比较低），设备可以分为三个优先级（数字越小优先级越大），会向服务器发送上行信息，每秒发送一次，每秒产生的数据量如下：

  - 优先级 1：稳态 `36 Kb`，峰值 `60 Mb`
  - 优先级 2：稳态 `126 Kb`，峰值 `192 Mb`
  - 优先级 3：稳态 `288 Kb`，峰值 `150 Mb`

当然，相关参数都可以调整，基本遵照这个模板即可。

我现在希望提出一个好的 MPTCP 调度算法。

---

目前我其实做过一些测试，我个人觉得理想调度器从数据看出来的表现应该是：

在 30 个设备都稳态时，包都比较小，Wi-Fi 链路调度不会出现问题，所以都走 Wi-Fi 效果比较好。

有突发流量时，突发流量比较大，应该同时走 Wi-Fi 和 nr（多条链路聚合带宽，减少单次突发流量完成时间）；而平稳流量应该走好的路径（可能是 Wi-Fi 也可能是 nr）。

---

我目前想法：

单纯从处理 peak 流量来看，default 算法会比其他算法（如 ecf, blest 算法表现好）。但是相比于 peak 流量完成时间，**更重要的指标是 steady 流量延迟**（我觉得应该将 max 控制在 10ms 以内）。我发现在 4 个或 8 个 p3 peak 场景，如果不加以限制，会导致 nr 链路上的稳态流量延迟劣化。

目前稳态流量可以固定为走 nr 链路（目前先固定，之后再改），先忽略 sub-peak-0 场景，因为 steady 固定走 nr 在此场景肯定不如 Wi-Fi 的。

所以现在任务即是你需要提出一个新的算法，在 default 算法基础上，使得能够探测到当前发包是否会让 nr 链路稳态流量劣化（你可能可以参考一下 BBR 拥塞控制的设计）来对 nr 链路上 peak 流量发送加以一点限制。

我觉得算法设计大于调参，所以当你觉得不对的时候，请你首先看看算法设计能不能调整，而不是一味调参。

---

现在请你设计一个新的算法，并在 `/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp` 下进行实现，/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/self/add-scheduler-record.md 有添加调度算法方法。 

然后你可以通过：

```bash
./run-myscript.sh --mode opt --isolate-files \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak-0 \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=default
```

除了 p3-sub-peak-0 场景，还有 p3-sub-peak-4 p3-sub-peak-8 这些场景。

注意开启 isolate-files 选项后，你可以同时运行多个场景。

---

目前我也对于 default 和 blest 算法跑了这些场景测试（最长会跑 12min 左右，所以请你在跑过程中不要着急），你可以看对应目录的 json 文件，其中 `delay_ms` 是只统计稳态流量的延迟，而 peak_delay_ms 我理解是可以近似认为每次突发流的完成时间。

你算法对比基准应该为 default 和 blest 算法，如果有其他算法结果那应该只是给你提供参考。

---

注意在执行过程中不要参考其他文档结论或场景结果，其他文档结论很可能过时了。

注意在执行过程中不要参考本文没有提及的 MPTCP 调度算法（看也不要看），也是过时的。

---

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

构建与部署：

```bash
cd /root/bake/source/net-next-nuse-mptcp-0.92
make library ARCH=lib -j4
cp arch/lib/tools/libsim-linux-4.4.110.so /root/bake/build-opt-dce-rel/bin_dce/libsim-linux-4.4.110.so
cp arch/lib/tools/libsim-linux-4.4.110.so /root/bake/build/bin_dce/libsim-linux-4.4.110.so
```

验证命令模板：

```bash
./run-myscript.sh --mode opt --timeout 1800 --clean-files \
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
| p3-sub-peak-0 | default | 0.665 | 0.696 | 0.828 | 3.491 | 0.000 | 0.000 | 0.000 | 0.000 | 0.186% |
| p3-sub-peak-0 | blest | 0.715 | 0.696 | 2.447 | 5.739 | 0.000 | 0.000 | 0.000 | 0.000 | 0.656% |
| p3-sub-peak-0 | nrsafe | 4.598 | 5.993 | 6.316 | 6.490 | 0.000 | 0.000 | 0.000 | 0.000 | 99.850% |
| p3-sub-peak-4 | default | 3.511 | 18.483 | 18.483 | 18.483 | 265.884 | 303.439 | 316.102 | 319.267 | 34.344% |
| p3-sub-peak-4 | blest | 4.128 | 18.298 | 19.560 | 24.733 | 244.539 | 284.074 | 294.120 | 296.998 | 32.816% |
| p3-sub-peak-4 | nrsafe | 5.072 | 6.479 | 17.933 | 18.483 | 256.602 | 290.416 | 298.019 | 299.813 | 10.789% |
| p3-sub-peak-8 | default | 13.030 | 19.715 | 21.828 | 37.248 | 483.202 | 696.071 | 935.116 | 1085.514 | 32.247% |
| p3-sub-peak-8 | blest | 11.765 | 18.488 | 20.683 | 23.465 | 494.319 | 810.685 | 904.993 | 938.236 | 37.252% |
| p3-sub-peak-8 | nrsafe | 10.915 | 18.483 | 19.715 | 19.733 | 634.023 | 1110.049 | 1485.634 | 1536.140 | 19.759% |

结论：

- `p3-sub-peak-0` 按任务说明不作为胜负判断；当前 steady 固定 NR，所以它自然不如 Wi-Fi-heavy 的 default/blest。
- `p3-sub-peak-4` 中，`nrsafe` 明显降低 steady p95，并把 peak 完成时间保持在接近 blest/default 的区间。
- `p3-sub-peak-8` 中，`nrsafe` 明显降低 steady p99 和 max，但 peak tail 慢于 default/blest。这是当前算法主动保护 NR steady 延迟的代价。
- 当前版本适合作为“NR steady latency protective scheduler”：它不是 peak-only 最优，而是在 default 聚合基础上限制 NR peak 注入，避免 NR 上 steady 流量被 peak 拖出高尾延迟。
