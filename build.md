## 1. build ns-3 的命令

```bash
cd /root/bake/source/ns-3.35
python3 ./waf configure --prefix=/root/bake/build --enable-examples --enable-tests
python3 ./waf
python3 ./waf install
```

## 2. build ns-3-dce 的命令

```bash
cd /root/bake/source/ns-3-dce
python3 ./waf configure --prefix=/root/bake/build --with-ns3=/root/bake/build --with-glibc=/root/bake/build/glibc --with-libaspect=/root/bake/build --enable-kernel-stack=/root/bake/source/net-next-nuse-mptcp-0.92/arch
python3 ./waf
python3 ./waf install
```

## 3. Debug 版与 Optimized/O2 版的区别

当前工作区现在有两套可并存的构建：

### 3.1 Debug 版

- `ns-3.35` 安装前缀：`/root/bake/build`
- `ns-3-dce` 安装前缀：`/root/bake/build`
- `ns-3-dce` 构建目录：`/root/bake/source/ns-3-dce/build`
- 典型特征：
  - 链接的是 `libns3.35-*-debug.so`
  - 开启 debug symbols
  - 开启 assert
  - 开启 log
  - 运行明显更慢，但更适合调试

### 3.2 Optimized/O2 版

- `ns-3.35` 安装前缀：`/root/bake/build-opt-rel`
- `ns-3-dce` 安装前缀：`/root/bake/build-opt-dce-rel`
- `ns-3-dce` 构建目录：`/root/bake/source/ns-3-dce/build-opt-dce-rel`
- 典型特征：
  - `ns-3.35` 链接的是 `libns3.35-*-optimized.so`
  - `ns-3-dce` 使用 `--enable-opt`
  - 两边都固定为 `-O2 -g0`
  - `ns-3-dce` 关闭 debug symbols / assert / log
  - 运行明显更快，适合正式跑仿真

## 4. 实际验证通过的 Optimized/O2 构建命令

说明：

- `ns-3-dce --enable-opt` 不是找 `release` 后缀的 ns-3 库，而是找 `*-optimized` 后缀库。
- 因此 `ns-3.35` 这里不能简单用 `release` profile，而要用 `optimized` profile。
- 但为了满足“开启 O2”的要求，这里额外用环境变量把 `CCFLAGS/CXXFLAGS` 固定为 `-O2 -g0`。

### 4.1 构建 ns-3.35 optimized/O2

```bash
cd /root/bake/source/ns-3.35
env CCFLAGS='-O2 -g0' CXXFLAGS='-O2 -g0' \
python3 ./waf configure -d optimized \
  --disable-python \
  --disable-tests \
  --disable-examples \
  --prefix=/root/bake/build-opt-rel \
  -o build-opt-rel

python3 ./waf build install -j4 -o build-opt-rel
```

构建完成后，关键库位于：

```bash
/root/bake/build-opt-rel/lib/libns3.35-core-optimized.so
/root/bake/build-opt-rel/lib/libns3.35-wifi-optimized.so
/root/bake/build-opt-rel/lib/libns3.35-lte-optimized.so
/root/bake/build-opt-rel/lib/libns3.35-nr-optimized.so
/root/bake/build-opt-rel/lib/libns3.35-flow-monitor-optimized.so
/root/bake/build-opt-rel/lib/libns3.35-internet-apps-optimized.so
```

### 4.2 构建 ns-3-dce optimized/O2

```bash
cd /root/bake/source/ns-3-dce
env CCFLAGS='-O2 -g0' CXXFLAGS='-O2 -g0' \
python3 ./waf configure \
  --enable-opt \
  --disable-debug \
  --disable-assert \
  --disable-log \
  --disable-python \
  --with-ns3=/root/bake/build-opt-rel \
  --with-glibc=/root/bake/build/glibc \
  --with-libaspect=/root/bake/build \
  --enable-kernel-stack=/root/bake/source/net-next-nuse-mptcp-0.92/arch \
  --prefix=/root/bake/build-opt-dce-rel \
  -o build-opt-dce-rel

python3 ./waf build install -j4 -o build-opt-dce-rel
```

## 5. 如何使用 Debug 版

推荐直接运行二进制，不依赖 `waf --run`：

```bash
cd /root/bake/source/ns-3-dce
export LD_LIBRARY_PATH=/root/bake/build/lib:$LD_LIBRARY_PATH
export DCE_PATH=/root/bake/build/bin:/root/bake/build/bin_dce:/root/bake/build/sbin:$DCE_PATH
```

示例：

```bash
/root/bake/source/ns-3-dce/build/myscripts/120-demo/bin/120-demo --help
/root/bake/source/ns-3-dce/build/myscripts/120-demo/bin/link-info-wifi-only --help
/root/bake/source/ns-3-dce/build/myscripts/120-demo/bin/link-info-nr-only --help
```

## 6. 如何使用 Optimized/O2 版

同样推荐直接运行二进制：

```bash
cd /root/bake/source/ns-3-dce
export LD_LIBRARY_PATH=/root/bake/build-opt-rel/lib:/root/bake/build-opt-dce-rel/lib:$LD_LIBRARY_PATH
export DCE_PATH=/root/bake/build-opt-dce-rel/bin:/root/bake/build-opt-dce-rel/bin_dce:/root/bake/build/sbin:/root/bake/build/bin_dce
```

注意：

- optimized 的 `ns-3-dce` 运行时仍然需要从 `/root/bake/build/bin_dce` 找到内核包装库 `liblinux.so`
- `ip`、`ss` 这类 DCE 内部会调用的工具在当前环境位于 `/root/bake/build/sbin`
- 因此 `DCE_PATH` 里必须额外包含 `/root/bake/build/bin_dce`
- 以及 `/root/bake/build/sbin`
- 如果漏掉 `/root/bake/build/sbin`，像 `120-demo` 这种依赖 DCE 内部执行 `ip route` / `ss -tin` 的程序会因为找不到可执行文件而无法正确建链；debug 版通常会直接断言，optimized 版则可能表现为“程序跑完但 `connected=0/...`”

示例：

```bash
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo --help
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/link-info-wifi-only --help
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/link-info-nr-only --help
```

当前已经确认存在的 optimized 二进制包括：

```bash
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/120-demo
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/link-info-wifi-only
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/120-demo/bin/link-info-nr-only
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/dce-wifi/bin/dce-wifi
/root/bake/source/ns-3-dce/build-opt-dce-rel/myscripts/nr-dce/bin/nr-dce
```

## 7. 如果仍然想用 waf 运行

也可以保留 `waf` 入口，但需要区分输出目录：

### 7.1 Debug 版

```bash
cd /root/bake/source/ns-3-dce
python3 ./waf --run "120-demo --help"
```

### 7.2 Optimized/O2 版

```bash
cd /root/bake/source/ns-3-dce
python3 ./waf -o build-opt-dce-rel --run "120-demo --help"
```

不过从实际使用角度，直接运行二进制并设置 `LD_LIBRARY_PATH` / `DCE_PATH` 更稳，也更容易明确自己当前到底在用哪套环境。
