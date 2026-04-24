# p3-only-3x140m

## 设计目标

这个场景只保留 3 台 `prio3` 设备，每台固定 `140 Mbps`，并用 `wifi-only` 方式运行。

用途是和 `p3-only-3peak-x6` 的 `3 x 150 Mbps` 纯 Wi-Fi 对照：

- 如果 `140 Mbps` 时延没有明显下降，说明问题不只是“略微超过容量上限”；
- 如果 `140 Mbps` 时延明显下降，说明当前时延主要由 offered load 贴近 Wi-Fi 实际 goodput 上限导致。
