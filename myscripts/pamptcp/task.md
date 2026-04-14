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

对于 p3-sub-peak，仿真 6s 所用时长大约在 9min 左右。

目前我已经跑了一些结果，放在了 `myscripts/pamptcp/flow-priority-30/p3-sub-peak` 文件夹下。

然后我使用的内核构建目录是在 /root/bake/source/net-next-nuse-mptcp-0.92 中，MPTCP 调度算法目录在 /root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp ，编译内核请看 /root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/self/build.md。

目前我设计的 pablest 算法位于 /root/bake/source/net-next-nuse-mptcp-0.92/net/mptcp/mptcp_pablest.c，目前已经被乱改的不成样了，没有多少参考意义。

但从结果显示，表现并不好（目的是尽量降低 p1 和 p2 的延迟及抖动，同时不引入太大的代价）。

你现在可以新增一个 test 算法或者选择改进 pablest 算法。

目前我观察到的结论是（可能不是特别对）： 5G 链路在多设备调度，低峰值流量的情况下时延是比较稳定的。而 Wi-Fi 在多设备流量都比较小的时候时延相比 5G 更小，但是一旦多设备进行低峰值流量时，时延会突增。