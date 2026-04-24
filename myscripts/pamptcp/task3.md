运行 p3-sub-peak 场景命令，仿真 6s：

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-sub-peak \
  --numAps=1 \
  --simTime=6 \
  --mptcpScheduler=default
```

请你看看 myscripts/pamptcp/flow-priority-30/p3-sub-peak 文件下的输出，目前已经跑了一些结果。

可以发现 blest 算法表现是优于 default 的。而且 prio1 和 prio2 在该场景下都是走 nr。

但是在 blest 算法显然是感知不到优先级的，所以如果我想在论文中给这种现象一种解释，我该怎么解释。请你看看 blest 算法是怎么做到这点的。

MPTCP 调度算法目录在 /root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp