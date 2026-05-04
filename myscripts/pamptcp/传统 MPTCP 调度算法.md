我最近在研究 MPTCP Scheduler。

目前基本场景定义如下：

有 30 个设备，都连接到同一个 5G 基站和 Wi-Fi（5G 是 SA，自建核心网；使用 Wi-Fi6；所以延迟在单设备情况下都比较低），设备可以分为三个优先级（数字越小优先级越大），会向服务器发送上行信息，每秒发送一次，每秒产生的数据量如下：

  - 优先级 1：稳态 `36 Kb`，峰值 `60 Mb`
  - 优先级 2：稳态 `126 Kb`，峰值 `192 Mb`
  - 优先级 3：稳态 `288 Kb`，峰值 `150 Mb`

当然，相关参数都可以调整，基本遵照这个模板即可。

---

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

来使用特定调度器运行特定场景

除了 p3-sub-peak-0 场景，还有 p3-sub-peak-4 p3-sub-peak-8 这些场景。

注意开启 isolate-files 选项后，你可以同时运行多个场景，不用同一时间只跑一个而浪费时间。

---

注意在执行过程中不要参考本文没有提及的 MPTCP 调度算法（看也不要看），也是过时的。

---

现在我是想要调查一下传统 MPTCP 调度算法以及单链路的表现。

需要覆盖的场景有：p3-sub-peak-0, p3-sub-peak-2（目前还没有，你需要再创建一个），p3-sub-peak-4, p3-sub-peak-8

需要覆盖的算法有：default, blest, ecf, round-robin, Wi-Fi 单链路，nr 单链路（目前单链路输出似乎和选定的 MPTCP 调度算法输出到同一文件，所以你可能还需要改变一下输出文件名，当选定单一链路时文件名中不体现调度算法，而是体现选择的单一链路）。

请你先补全做一下实验（在场景文件夹中如果已有类似 `blest-stream1-10s.json` 的文件，代表改算法已经做过了）。