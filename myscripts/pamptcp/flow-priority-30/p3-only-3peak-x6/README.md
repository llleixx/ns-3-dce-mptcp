# p3-only-3peak-x6

## 设计目标

这个场景只保留 3 台 `prio3` 峰值设备，每台固定 `150 Mbps`。

用途不是做业务对比，而是隔离验证：

- 如果只有这 3 台设备走 Wi-Fi，时延尾部会到多少；
- `p3-3peak-x6/testburst` 中看到的 `80 ms` 量级时延，究竟是 Wi-Fi 本身在这个 offered load 下就会出现，还是混合负载/MPTCP 额外放大的结果。
