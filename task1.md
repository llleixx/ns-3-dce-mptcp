## background

目前该项目是一个 ns-3 dce 的项目，我所做的修改主要在 `myscripts` 文件夹下，然后你可以运行 `./waf --run xx` 来运行某个程序。

DCE 每次运行，都会将 stdout 放在 `files-x/var/log/<pid>/stdout` 下，同理还有 cmdline 和 stderr。你每次运行前记得先删除之前跑出来生成的 `files-x` 文件夹。

参考命令：`for f in files-1/var/log/*/stdout files-2/var/log/*/stdout; do echo "== $f =="; sed -n '1,220p' "$f"; done`

然后在任务执行过程中你可能会遇到 5g nr 模块，请你首先参考 `myscripts/nr-dce/nr-dce.cc` 这个文件。如果不够：对于这部分内容，互联网上资料较少，所以请你尽可能去参考源码实现，源码路径位于 `../ns-3.35/contrib/nr`，特别是其使用示例，即 `../ns-3.35/contrib/nr/examples` 文件夹下的内容。同时，如有必要，请也去阅读 nr 源码。

对于 wifi 模块，你可以参考 `myscripts/dce-wifi/dce-wifi.cc` 这个文件。

对于 dce 下使用 mptcp，你可以参考 `myscripts/app-test/app-test1.cc` 和 `example/dce-cradle-mptcp.cc` 这两个文件。

## task

我现在要搭建一个同时使用 5G 和 WiFi 进行 MPTCP 传输的 demo。

拓扑架构：

120 个 client 节点，1 个 5G 基站，2 个 WiFi AP，5G PGW 和 WiFi AP 都有线连接到一个 Server 节点上。

120 个节点 3 列均匀排布，列与列之间间隔 2.8m，一列上相邻两个节点间隔 0.7m，位于一个水平面，5G 基站和 WiFi AP 都位于距离水平面 5m 高度上（高度已知，水平面上位置你觉得哪里最优就放在哪里）。

client 节点使用 `myscripts/apptest/` 文件夹下定义的 RateDualApplication，`AvgRate` 和 `PeakRate` 以及 `BurstProb` 暂时先使用默认值。

server 节点就用 PacketSink。

5G 设备相关规格参考 `myscripts/nr-dce/nr-dce.cc`，暂时先使用该文件的默认参数。

WiFi 设备相关规格参考 `myscripts/dce-wifi.cc`。

将相关文件放在 `myscripts/120-demo/` 文件夹下。