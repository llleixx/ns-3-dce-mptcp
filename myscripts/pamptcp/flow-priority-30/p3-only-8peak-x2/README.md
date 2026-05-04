# p3-only-8peak-x2

This profile is the same as `p3-only-8peak`, but the peak flow rate is doubled
to 150 Mbps so the offered peak load is heavier.

Run example:

```bash
./run-myscript.sh --mode opt \
  myscripts/pamptcp/pamptcp.cc \
  -- \
  --trafficProfileDir=myscripts/pamptcp/flow-priority-30/p3-only-8peak-x2 \
  --numAps=1 \
  --simTime=10 \
  --statsStart=4 \
  --statsStop=10 \
  --clientStartJitter=1.0 \
  --mptcpScheduler=ecf
```
