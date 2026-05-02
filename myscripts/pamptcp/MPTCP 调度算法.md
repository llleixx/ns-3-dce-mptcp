我最近在研究 MPTCP Scheduler。

目前基本场景定义如下：

有 30 个设备，都连接到同一个 5G 基站和 Wi-Fi（5G 是 SA，自建核心网；使用 Wi-Fi6；所以延迟在单设备情况下都比较低），设备可以分为三个优先级（数字越小优先级越大），会向服务器发送上行信息，每秒发送一次，每秒产生的数据量如下：

  - 优先级 1：稳态 `36 Kb`，峰值 `60 Mb`
  - 优先级 2：稳态 `126 Kb`，峰值 `192 Mb`
  - 优先级 3：稳态 `288 Kb`，峰值 `75 Mb`

当然，相关参数都可以调整，基本遵照这个模板即可。

我现在希望提出一个能区分优先级的 MPTCP 调度算法。

---

目前我其实做过一些测试，我个人觉得理想调度器从数据看出来的表现应该是：

在 30 个设备都稳态时，包都比较小，Wi-Fi 链路调度不会出现问题，所以都走 Wi-Fi 效果比较好。

有突发流量时，突发流量比较大，应该同时走 Wi-Fi 和 nr（多条链路聚合带宽，减少单次突发流量完成时间）；而平稳流量应该走好的路径（可能是 Wi-Fi 也可能是 nr）。

但是随突发流量变多，整体上来看，平稳流量应该会向 nr 迁移，而突发流量多走 Wi-Fi，少走 nr（因为 Wi-Fi 链路带宽更大，而且 Wi-Fi 对多设备调度不稳定；而 nr 虽然带宽小，但是对多设备调度更加稳定）。

然后该算法最好是对 Wi-Fi 链路和 nr 链路是不感知的，也就是不要硬编码某个子网 IP 就对应 nr 或者 Wi-Fi，而是利用链路感知到的特性来调整调度算法。

---

现在请你设计一个新的算法，并在 `/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp` 下进行实现，/root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/self/add-scheduler-record.md 有添加调度算法方法。 

然后你可以通过：

```bash
./run-myscript.sh --mode opt \
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

---

目前我也对于 default 和 blest 算法跑了这些场景测试（最长会跑 10min 左右，所以请你在跑过程中不要着急），你可以看对应目录的 json 文件，其中 `delay_ms` 是只统计稳态流量的延迟，而 peak_delay_ms 我理解是可以近似认为每次突发流的完成时间。

你算法对比基准应该为 default 和 blest 算法，如果有其他算法结果那应该只是给你提供参考。

同时我之前也测试了 testprio 算法，让 p3 走 Wi-Fi，让没有突发的 p1/p2 走 NR，发现效果出奇的好：/root/bake/source/ns-3-dce/myscripts/pamptcp/flow-priority-30/p3-sub-peak-8/testprio-stream1-10s.json

那别人肯定会质疑你为什么不让突发流量只走 Wi-Fi，稳态流量只走 nr。所以我觉得只有你做到保证稳态延迟不会牺牲太多的同时，p3 同时走 Wi-Fi 和 nr（但 nr 肯定是很克制的，不能影响稳态流量），你的算法才能站得住脚。

---

注意在执行过程中不要参考其他文档结论，其他文档结论可能过时了。
