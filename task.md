## background

目前该项目是一个 ns-3 dce 的项目，我所做的修改主要在 `myscripts` 文件夹下，然后你可以运行 `./waf --run xx` 来运行某个程序。

DCE 每次运行，都会将 stdout 放在 `files-x/var/log/<pid>/stdout` 下，同理还有 cmdline 和 stderr。你每次运行前记得先删除之前跑出来生成的 `files-x` 文件夹。

参考命令：`for f in files-1/var/log/*/stdout files-2/var/log/*/stdout; do echo "== $f =="; sed -n '1,220p' "$f"; done`

然后在任务执行过程中你可能会遇到 5g nr 模块，对于这部分内容，互联网上资料较少，所以请你尽可能去参考源码实现，源码路径位于 `../ns-3.35/contrib/nr`，特别是其使用示例，即 `../ns-3.35/contrib/nr/examples` 文件夹下的内容。同时，如有必要，请也去阅读 nr 源码。

## nr 模块速率

目前我想看看在 n79 100MHz 频宽，SISO，TDD 采用 DSUUU 模式下的理论速率情况。

在当前 `myscripts/nr-dce/nr-dce.cc` 实现中，TCP 单连接，发送和接受都是 300Mbps 左右。UDP，指定发送方 1000Mbps（实际 821Mbps），接收方 317Mbps。

然后我了解到 5g 速率还与 QAM 调制与最大码率有关，你能看看目前实现的默认这些参数是多少吗？然后调整一下这些参数（不能无脑调，要符合实际，比如一般最多 256QAM），看看能不能提高。

然后评估一下是否符合实际预期（计算一下理论速率，然后比对一下）。