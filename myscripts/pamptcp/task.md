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

目前已有的调度器：

- default，MPTCP v0.92 的默认调度器实现
- default_v1，仿造目前内核的 MPTCP v1 版本的默认调度器实现
- default_v1_full，进一步仿造目前内核的 MPTCP v1 版本的默认调度器实现
- pablest，原本是想基于 blest 算法体现优先级的调度器算法，但是现在可以随意魔改

当你进行调度器结果优劣对比的时候，可以先只对比 default 算法。

目前，虽然 pablest 算法在 p3-sub-peak 场景表现较好，但是目前从结果来看，是把某个优先级推到一个链路上的，似乎并不能看出来这个是一个好的算法。你能重新设计一个场景（新创文件夹），然后保证能在新的场景中体现调度器的优势吗？

注意，可能在新的场景中调度器表现不是很好，但是这时，你可以选择：

- 如果你觉得是算法不够好，你可以重新设计 pablest 算法，进行改进。
- 如果你觉得是场景设计不够好，你可以重新设计场景。

场景设计原则：

设置不同实验场景时，最好满足：

- 优先级 1：稳态 `12 Kbps`，峰值 `20 Mbps`
- 优先级 2：稳态 `42 Kbps`，峰值 `64 Mbps`
- 优先级 3：稳态 `96 Kbps`，峰值 `25 Mbps`

保持优先级 1 总设备数量 6 台不变，优先级 2 总设备数量 11 台不变，优先级 3 总设备数量 13 台不变。但是你可以将同一优先级设备划分成多个部分，比如一部分始终稳态，另一部分始终峰值或者有时峰值有时稳态。但是实验场景设置的尽量干净一点（即分类少一点），这样好解释为什么这样设置。