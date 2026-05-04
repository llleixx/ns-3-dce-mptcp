# Link Characteristics Summary

Delay is `priority_metrics.entries[scope=overall].delay_ms`, i.e. application-layer completion time for each timestamped TCP burst.

## nr-only

| clients | bytes/burst | offered Mbps | rx Mbps | delay mean ms | p50 | p95 | p99 | max | connected | notes |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| 1 | 32 | 0 | 0 | 3.25 | 3.25 | 3.25 | 3.25 | 3.25 | 1/1 |  |
| 1 | 64 | 0.001 | 0.001 | 17 | 17 | 17 | 17 | 17 | 1/1 |  |
| 1 | 128 | 0.001 | 0.001 | 3.25 | 3.25 | 3.25 | 3.25 | 3.25 | 1/1 |  |
| 1 | 256 | 0.002 | 0.002 | 3.251 | 3.251 | 3.251 | 3.251 | 3.251 | 1/1 |  |
| 1 | 512 | 0.004 | 0.004 | 3.251 | 3.251 | 3.251 | 3.251 | 3.251 | 1/1 |  |
| 1 | 1024 | 0.008 | 0.008 | 3.253 | 3.253 | 3.253 | 3.253 | 3.253 | 1/1 |  |
| 1 | 2048 | 0.016 | 0.016 | 3.254 | 3.254 | 3.254 | 3.254 | 3.254 | 1/1 |  |

## Files

- CSV: `myscripts/pamptcp-link-characteristics/flow-profiles/nr-small-check/summary.csv`
