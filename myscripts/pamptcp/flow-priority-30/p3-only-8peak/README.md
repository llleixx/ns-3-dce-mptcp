# p3-only-8peak

This profile isolates the eight priority-3 peak flows from
`p3-sub-peak-8`.  It removes priority-1, priority-2, and priority-3 steady
flows so scheduler tests can focus on whether multipath aggregation reduces
the peak-flow completion time.

Run example:

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-8peak \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=ecf
```
