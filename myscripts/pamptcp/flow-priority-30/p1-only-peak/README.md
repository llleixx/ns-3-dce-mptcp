# p1-only-peak

保留 `p1-peak-p3-sub-peak` 里的全部 `P1` 设备：

- 1 台 `prio1_peak`：`20 Mbps`
- 5 台 `prio1_steady`：`12 Kbps`

去掉所有 `P2/P3` 设备，用于判断 `P1` 峰值时延是否主要由其它优先级流量竞争导致。
