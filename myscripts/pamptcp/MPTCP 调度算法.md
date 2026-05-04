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

注意开启 isolate-files 选项后，你可以同时运行多个场景，不用同一时间只跑一个而浪费时间。

---

目前我也对于 default 和 blest 算法跑了这些场景测试（最长会跑 12min 左右，所以请你在跑过程中不要着急），你可以看对应目录的 json 文件，其中 `delay_ms` 是只统计稳态流量的延迟，而 peak_delay_ms 我理解是可以近似认为每次突发流的完成时间。

你算法对比基准应该为 default 和 blest 算法，如果有其他算法结果那应该只是给你提供参考。

---

注意在执行过程中不要参考其他文档结论或场景结果，其他文档结论很可能过时了。

注意在执行过程中不要参考本文没有提及的 MPTCP 调度算法（看也不要看），也是过时的。
