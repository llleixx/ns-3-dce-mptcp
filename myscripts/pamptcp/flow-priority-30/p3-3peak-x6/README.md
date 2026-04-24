# p3-3peak-x6

## 设计目标

这个场景把原始业务模板整体放大到 `6x`，但只让 3 台 `prio3` 设备进入峰值模板：

- `prio1`：6 台，全部使用 `72 Kbps`
- `prio2`：11 台，全部使用 `252 Kbps`
- `prio3`：10 台使用 `576 Kbps`，3 台使用 `150 Mbps`

这个设置强调的是：

1. 整体基线负载明显提高，不再是原始轻载 steady。
2. 峰值设备数量保持很少，只测试“少量低优突发”是否足以把高优先级尾部拖坏。
3. 如果 scheduler 仍然有效，它就不应该只在“大量 P3 峰值设备”这类容易解释的场景里表现好。

## 运行命令

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-3peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --mptcpScheduler=default
```

把 `--mptcpScheduler=default` 改成 `pablest` 即可进行对比。
