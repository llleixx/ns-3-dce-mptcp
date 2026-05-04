# pamptcp link characteristics

This directory is an isolated copy for link-characteristic experiments. It does
not modify `myscripts/pamptcp`.

## Batch sweep

Generate and run the completed Wi-Fi/NR single-link grid:

```bash
python3 myscripts/pamptcp-link-characteristics/run-link-characteristics.py \
  --links=wifi-only,nr-only \
  --clients=1,4,8,15,30 \
  --wifi-bytes=64,1024,16384,65536,262144,1048576,2097152,4194304 \
  --nr-bytes=64,1024,16384,65536,262144,524288,1048576,2097152 \
  --profile-root=myscripts/pamptcp-link-characteristics/flow-profiles/final-grid \
  --parallel=4 \
  --timeout=1200 \
  --skip-existing
```

Each generated profile sends one timestamped TCP application burst per UE per
second.  The profile directory name `n030-b1048576` means 30 UEs and 1,048,576
application bytes per second per UE.

Regenerate summary tables:

```bash
python3 myscripts/pamptcp-link-characteristics/summarize-link-characteristics.py \
  myscripts/pamptcp-link-characteristics/flow-profiles/final-grid
```

Outputs:

- `flow-profiles/final-grid/summary.csv`
- `flow-profiles/final-grid/summary.md`

The delay column is `priority_metrics.entries[scope=overall].delay_ms` from the
JSON report, i.e. application-layer completion time for each timestamped TCP
burst.

Initial smoke run:

```bash
./run-myscript.sh --mode opt --isolate-files \
  myscripts/pamptcp-link-characteristics/link-characteristics.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp-link-characteristics/flow-profiles/smoke \
  --numAps=1 \
  --pathMode=wifi-only \
  --simTime=6 \
  --statsStart=3 \
  --statsStop=6 \
  --clientStartJitter=0
```

TCP segment delay matching has been intentionally removed. The available
reports focus on aggregate link characteristics such as sink throughput,
priority-flow metrics, and NR/Wi-Fi payload split checks. Full NR TCP segment
delay would require a UE-side NR stack entry trace, which is outside this
isolated script copy.

Smoke results from the initial validation:

- `wifi-only`, 1 UE, 20 Mbps: script builds and emits JSON with the TCP segment
  delay field omitted.
- `nr-only`, 1 UE, 20 Mbps: script builds and emits JSON with the TCP segment
  delay field omitted.
