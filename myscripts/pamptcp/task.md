运行 p3-peak 场景命令，仿真 6s：

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-peak \
  --numAps=1 \
  --simTime=6 \
  --mptcpScheduler=default
```

对于 p3-peak，仿真 6s 所用时长大约在 11min 左右。

目前我已经跑了一些结果，放在了 `myscripts/pamptcp/flow-priority-30/p3-peak` 文件夹下。

然后我使用的内核构建目录是在 /root/bake/source/net-next-nuse-mptcp-0.92 中，MPTCP 调度算法目录在 /root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp ，编译请看 /root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/self/build.md