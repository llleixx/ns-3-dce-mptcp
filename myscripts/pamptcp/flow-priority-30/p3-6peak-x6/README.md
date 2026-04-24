# p3-6peak-x6

## 设计目标

这个场景沿用 `p3-3peak-x6` 的 `6x` 业务模板，但把 `P3` 的组成从：

- `10 steady + 3 peak`

改成：

- `7 steady + 6 peak`

固定总设备数仍为 `30`：

- `P1`：6 台，`72 Kbps`
- `P2`：11 台，`252 Kbps`
- `P3`：7 台，`576 Kbps`
- `P3`：6 台，`150 Mbps`

这个改动专门用来测试：当高强度 `P3 peak` 设备数量进一步增加时，scheduler 是否会因为把 `P1/P2` 推向同一条低时延链路而劣化。

## 运行命令

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-6peak-x6 \
  --numAps=1 \
  --simTime=6 \
  --clientStartJitterStream=1 \
  --mptcpScheduler=linksense
```

把 `--mptcpScheduler=linksense` 改成 `pablest` 即可做对比。

## 当前结果摘要

在 2026-04-22 这次运行中，使用相同参数：

- `simTime=6`
- `statsWindow=[4,6]`
- `clientStartJitter=1.0`
- `clientStartJitterStream=1`

得到的结果如下。

### `linksense`

- 文件：`linksense-stream1-6s.json`
- wall-clock：`real=285.84s`
- 窗口总吞吐：`982.236 Mbps`
- `P1`：`mean=4.774 ms`, `p95=15.795 ms`, `nr_share=20.111%`, `wifi_share=79.889%`
- `P2`：`mean=7.711 ms`, `p95=22.302 ms`, `nr_share=39.412%`, `wifi_share=60.588%`
- `P3`：`throughput=979.032 Mbps`, `mean=8.372 ms`, `p95=13.086 ms`, `overall_p95=626.973 ms`

### `pablest`

- 文件：`pablest-stream1-6s.json`
- wall-clock：`real=297.04s`
- 窗口总吞吐：`757.236 Mbps`
- `P1`：`mean=5.146 ms`, `p95=8.928 ms`, `nr_share=100.000%`, `wifi_share=0.000%`
- `P2`：`mean=7.294 ms`, `p95=18.416 ms`, `nr_share=100.000%`, `wifi_share=0.000%`
- `P3`：`throughput=754.032 Mbps`, `mean=7.932 ms`, `p95=14.943 ms`, `overall_p95=431.002 ms`

## 结论

在这个 `P3 peak=6` 的重负载 `x6` 场景中，初版 `linksense` 确实明显劣化，但经过继续调整后，收敛出的当前版本已经不再是那个结果。

当前收敛版与 `pablest` 的对比如下：

- 总吞吐更高：`982.236 > 757.236 Mbps`
- `P3` 吞吐更高：`979.032 > 754.032 Mbps`
- `P1/P2` 仍全部满足 SLA，没有 violation
- `P3 p95 delay` 更低：`13.086 < 14.943 ms`
- 但 `P3 overall_p95` 仍高于 `pablest`：`626.973 > 431.002 ms`
- `P1/P2` 的时延也仍比 `pablest` 差一些

因此，当前 `linksense` 在这个场景下已经从“明显失败”收敛到了“高吞吐、SLA 可接受，但高优和 P3 整体尾时延仍不如 `pablest`”的状态。  
如果目标是追求吞吐上限，这一版已经有优势；如果目标是继续压低高优与 `P3 overall tail`，还需要再引入更强的 guard-lane 稳定机制。
