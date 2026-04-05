我刚刚再次运行了你给我的以下命令，但是还是超过了 280s（我手动 kill 掉了）：

```txt
root@ubuntu:~/bake/source/ns-3-dce# rm -rf files-* && /usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' timeout 299s env LD_LIBRARY_PATH=/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib DCE_PATH=/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce /root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo --numClients=9 --numAps=1 --simTime=3.0 --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 --appSteadyRate=100Mbps --appBurstRate=100Mbps --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false --checkBackboneDualTraffic=1
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=9/9 randomAccess=0 idleConnecting=0 other=0
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=9/9 randomAccess=0 idleConnecting=0 other=0
Command terminated by signal 15
real=280.24 user=279.46 sys=0.72 maxrss=149888 exit=0
```

无论是打开新的终端，还是在当前的和你会话的终端都是如此。

所以你确定你跑的是 9 个 client 并且 100Mbps 吗？如果是，为什么相同的命令你能跑出来？

---

## 回答

这次我可以直接给更明确的回答：

1. 是的，我确定我跑的是 `9 clients`，并且是 `100Mbps`
2. 你现在这个质疑是成立的，我前一条回答少说了一个关键条件
3. 现在我已经把差异定位到一个很强的相关因素：**同一条命令是否挂在 `TTY/PTY` 上**

### 1. 我重新复跑了完全相同的 `9 clients / 100Mbps` 命令

我用的命令和你贴出来的是同一个参数点：

```bash
rm -rf files-* && /usr/bin/time -f 'real=%e user=%U sys=%S maxrss=%M exit=%x' \
timeout 300s \
env \
LD_LIBRARY_PATH=/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib \
DCE_PATH=/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce \
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo \
  --numClients=9 --numAps=1 --simTime=3.0 \
  --sinkStart=0.5 --clientStart=1.0 --clientStartJitter=0.0 \
  --appSteadyRate=100Mbps --appBurstRate=100Mbps \
  --wifiEnableOfdma=false --wifiEnableUlOfdma=false --wifiEnableBsrp=false \
  --checkBackboneDualTraffic=1
```

在我这边，如果**不用 PTY**，这条命令连续 3 次都稳定跑完：

```txt
RUN=1 ... real=46.42
RUN=2 ... real=46.01
RUN=3 ... real=44.45
```

而且三次输出都一致：

```txt
[120-demo] UE RRC summary (pre-stop @2.95s) connected=9/9 ...
[120-demo] PacketSink totalRxBytes=104247036 ...
[120-demo] Backbone dual-traffic summary: nrActive=9/9 wifiActive=6/9 bothActive=6/9 ...
[120-demo] Backbone missing WiFi payload UEs: 3,5,8
```

所以：

- 我之前说“我这里 `9 clients` 能跑出来”这件事本身是真的
- 不是我把 `9 clients` 记成了 `6 clients`

### 2. 但是我现在也能复现你的“跑到很久都不结束”

关键在这里。

我把**同一条命令**换成 **PTY/伪终端** 方式运行，结果立刻变成和你很像：

#### 2.1 直接用 PTY 跑

同一条命令，开启 PTY 后：

```txt
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=9/9 ...
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=9/9 ...
Command exited with non-zero status 124
real=180.00 user=179.20 sys=0.76 maxrss=189400 exit=124
```

#### 2.2 不用交互终端，但用 `script -qec` 人工包一层伪终端

我又做了一个隔离实验：

```bash
timeout 180s script -qec "<同一条 120-demo 命令>" /dev/null
```

结果同样是：

```txt
[120-demo] UE RRC summary (t=0.9 @0.9s) connected=9/9 ...
[120-demo] UE RRC summary (pre-clientStart @0.95s) connected=9/9 ...
Command exited with non-zero status 124
real=180.01 user=179.17 sys=0.77 maxrss=189316 exit=124
```

这说明：

- 差异不是“我有没有真的跑 `9 clients / 100Mbps`”
- 差异也不只是“你终端里恰好有别的负载”
- **同一条命令，只要挂到 `TTY/PTY` 上，就会复现你这种卡在 `0.95s` 之后、墙钟飙到几百秒的行为**

### 3. 所以现在正确的说法应该是什么

正确说法是：

> 我之前能跑出来，不是因为参数和你不一样，而是因为我当时跑的是“非 PTY 模式”的同一条命令。  
> 你在终端里运行时属于“PTY/TTY 模式”，而我现在已经能在这边复现：同一条命令一旦挂在 PTY 上，就会显著变慢，甚至直接超时。

因此，我前一条回复里真正不严谨的地方是：

- 我只说了“我这里复跑成功”
- 但没有说明“我当时那次复跑是在**非 PTY**执行模式下”

这个遗漏会让人误以为：

- “你在普通终端里直接敲同一条命令，也应该是 `44s` 左右”

而现在看，这个推断是不对的。

### 4. 现阶段我能下的最硬结论

截至目前，最硬的结论是：

1. `9 clients / 100Mbps / OFDMA off / checkBackboneDualTraffic=1`
   这个参数点本身没有写错
2. 同一条命令在**非 PTY**模式下，稳定是 `44~46s`
3. 同一条命令在**PTY/TTY**模式下，我这边也能复现超时
4. 你在终端里跑出来的“280s 还没结束”现象，因此不是你操作错了

### 5. 还没完全回答的部分

我现在能把差异定位到“`TTY/PTY` 强相关”，但还不能严谨地说出更底层的最终根因，例如：

- 是 `stdout/stderr` 关联到终端后触发了 DCE/Linux 某条慢路径
- 还是 `PTY` 本身改变了某些进程/会话行为
- 还是和终端会话下的调度/信号/子进程管理有关

也就是说：

- 我已经能解释“为什么相同命令你和我看到的 wall-clock 不一样”
- 但还没把“为什么 `PTY` 会把它拖慢”这件事追到具体代码点

### 6. 对你这个问题的直接回答

所以，直接回答你最后一句：

> 所以你确定你跑的是 9 个 client 并且 100Mbps 吗？

确定。

> 如果是，为什么相同的命令你能跑出来？

因为我之前那次成功复跑，是在**非 PTY**模式下执行的；  
而你在终端里运行属于 **TTY/PTY** 模式。现在我已经验证：**同一条命令一旦挂到 PTY 上，我这边也会复现你这种超时现象。**

换句话说，差异不在命令参数，差异在执行模式。
