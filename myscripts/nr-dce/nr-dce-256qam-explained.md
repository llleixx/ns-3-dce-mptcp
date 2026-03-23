# NR DCE 速率（n79 / 100MHz / SISO / TDD `DL|F|UL|UL|UL|`）默认行为与 256QAM（Table2）尝试记录

> 目录定位：本文件位于 `ns-3-dce` 仓库根目录（当前目录）。  
> 对应脚本：`myscripts/nr-dce/nr-dce.cc`（可通过 `./waf --run "nr-dce ..."` 运行）。  
> 本文实验与结论更新时间：2026-03-11。

## 0. 基础知识速读：MCS / AMC / CQI 到底在干什么

这一节是为了理解“为什么默认只有 ~300Mbps”以及“为什么启用 256QAM 后 UDP 能到 ~396Mbps，但 TCP/固定最高 MCS 反而可能变慢”。

### 0.1 一句话版

- **MCS**（Modulation and Coding Scheme）≈ “调制阶数(Qm) + 编码码率(ECR)” 的组合，用一个整数索引表示（例如 MCS=25）。
- **AMC**（Adaptive Modulation and Coding）≈ “根据链路质量（SINR/CQI）自动选一个合适的 MCS”，目标通常是 **TB 误块率 TBLER < 10%**，在可靠性和速率之间折中。
- **CQI**（Channel Quality Indicator）≈ UE/gNB 回报的“信道好坏”指标，调度器/AMC 用它来更新 MCS。
- **TBLER/BLER**（Transport Block / Block Error Rate）≈ “传输块解码失败的概率/比例”。

### 0.1.1 它们在 ns-3 NR 里怎么串起来（按 UL 方向）

以 UL（UE -> gNB -> 核心网 -> remoteHost）为例，关键的链路自适应链条大致是：

1) gNB 侧 `NrSpectrumPhy` 从接收信号估计每个 RB 的 SINR（并维护“感知 SINR”）。
2) gNB 侧 `NrGnbPhy::GenerateDataCqiReport()` 把 UL 的每个 RB 的 SINR 上报给 MAC（见 `../ns-3.35/contrib/nr/model/nr-gnb-phy.cc:1499`）。
3) MAC 的 `NrMacSchedulerCQIManagement::UlSBCQIReported()` 把“被分配 RB 的 SINR”喂给 `NrAmc::CreateCqiFeedbackWbTdma()`，得到 CQI，并更新一个“建议 UL MCS”（见 `../ns-3.35/contrib/nr/model/nr-mac-scheduler-cqi-management.cc:24`）。
4) 调度器在后续 UL 分配里使用这个 MCS（或者你用脚本参数 `fixedMcsUl=1` 强制固定）。

所以当你在脚本里切换 error model 或 AMC 模型，本质是在改变步骤 (3) 里 “CQI/MCS 由什么逻辑算出来”，进而影响吞吐上限与稳定性。

### 0.2 MCS 怎么影响“理论速率”

一个粗略直觉（忽略控制开销/DMRS/协议栈开销）：

- 每个 PRB 每个 OFDM 符号可承载的净比特数 ∝ **Qm * ECR**  
- 所以：
  - 64QAM：`Qm=6`
  - 256QAM：`Qm=8`
  - 仅从调制阶数看，256QAM 相比 64QAM 有 `8/6≈1.33x` 的潜在提升；再叠加 Table2 的 ECR/SE 表更激进，整体上限会抬高。

在 ns-3 NR 模块里，这些“每个 MCS 对应的谱效率(SE)”不是你自己算的，而是由 error model 提供的表：

- 默认 `NrLteMiErrorModel`（最高 64QAM）有 `SpectralEfficiencyForMcs`，最大约 `5.55 b/s/Hz`（见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.cc:293`）。
- NR Table2（`NrEesm...T2`，含 256QAM）有 `SpectralEfficiencyForMcs2`，最大约 `7.41 b/s/Hz`（见 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc:65`）。

### 0.3 “自适应 AMC”和“固定 MCS”的差别（为什么固定最高会翻车）

`NrAmc` 的 ErrorModel 模式里会做这样的事（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:220`）：

1) 从低 MCS 开始尝试；
2) 用 error model 计算该 MCS 下 TB 的 `TBLER`；
3) 找到**最高**的、能让 `TBLER <= 0.1` 的 MCS；
4) 用它发数据。

因此：

- **fixedMcsUl=0（自适应）**：它会“为了不超过 10% TBLER”主动降 MCS，吞吐更稳定。
- **fixedMcsUl=1（固定）**：你强行把 MCS 锁死；如果你锁到过高（例如 Table2 的 27），TBLER 可能非常高，重传/丢包会让应用层吞吐反而更差。

### 0.4 为什么 MCS 合理范围有时是 `0..27`，有时是 `0..28`

这个完全取决于你选用的 error model “内部定义了多少个 MCS”：

- `NrLteMiErrorModel::GetMaxMcs()` 返回 `28`（见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.cc:693`），所以索引范围是 **0..28（29 个值）**。
- `NrEesmErrorModel::GetMaxMcs()` 返回的是 `GetMcsEcrTable()->size()-1`（见 `../ns-3.35/contrib/nr/model/nr-eesm-error-model.cc:412`）。
  - 对 Table2，`McsEcrTable2` 有 28 项（见 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc:36`），所以最大索引是 **27（0..27）**。

这也是为什么脚本里对 `startingMcsUl/Dl` 做了“超范围 clamp”：避免填错索引触发断言/异常。

### 0.5 关键概念：`ErrorModelType` 决定“表/曲线”，而不只是“是否开误码”

ns-3 NR 里有两个地方都要知道“我到底用哪个 error model”：

- **AMC（`NrAmc`）**：把 SINR 映射成 CQI/MCS（当 `AmcModel=ErrorModel` 时，还会直接用 BLER 曲线去找满足 `TBLER<=0.1` 的最高 MCS）。
  - 默认 `NrAmc::ErrorModelType = ns3::NrLteMiErrorModel`（`../ns-3.35/contrib/nr/model/nr-amc.cc:58`）。
- **PHY（`NrSpectrumPhy`）**：把 “TB 的 SINR + MCS” 映射成 `TBLER`，决定 TB 是否 corrupt 以及 HARQ 反馈。
  - 默认 `NrSpectrumPhy::ErrorModelType = ns3::NrLteMiErrorModel`（`../ns-3.35/contrib/nr/model/nr-spectrum-phy.cc:120`）。

`NrHelper::SetUlErrorModel()/SetDlErrorModel()` 会同时设置 AMC 与 SpectrumPhy 使用同一种 error model（见 `../ns-3.35/contrib/nr/helper/nr-helper.cc:1235`）。

## 1. 原来的默认行为（为什么会“卡”在 ~300Mbps）

### 1.1 DCE 输出位置（运行前清理）

- 每次运行 DCE 会生成 `files-x/var/log/<pid>/stdout|stderr|cmdline`。
- **每次运行前建议先删掉旧的 `files-*`**，避免看错输出或混淆实验。

参考查看 stdout：

```bash
for f in files-1/var/log/*/stdout files-2/var/log/*/stdout; do
  echo "== $f =="; sed -n '1,220p' "$f";
done
```

### 1.2 NR 默认 AMC/误码模型的“天花板”

在 `contrib/nr` 这套栈里：

- `NrAmc` 默认 `ErrorModelType = ns3::NrLteMiErrorModel`（见 `../ns-3.35/contrib/nr/model/nr-amc.cc`）。
- `NrLteMiErrorModel` 的 `MCS -> 调制阶数(Qm)` **最高到 64QAM（Qm=6）**（见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.cc:300`）。
- 因此即便带宽 100MHz、近距离、SISO、TDD UL 占空比较高，**吞吐也会被“最高 64QAM + 对应码率/SE 表”限制**。

经验上在本场景（n79=4.9GHz、100MHz、SISO、TDD `DL|F|UL|UL|UL|`）：

- TCP 单连接吞吐常见在 **~300Mbps** 量级；
- UDP 在发送端指定更高 `-b` 时，接收端也会在一个相对固定的上限附近“饱和”（你之前看到的 317Mbps 属于这种表现）。

### 1.3 当前脚本里“未暴露成命令行参数”的默认无线/拓扑配置（读代码汇总）

这些是 `myscripts/nr-dce/nr-dce.cc` 里直接写死的关键默认值（因为它们也会显著影响“理论上限/是否能上 256QAM”）：

- 频段/带宽：`frequency=4.9e9`（n79 近似）、`bandwidth=100e6`
- Numerology：默认 `1`（30kHz SCS）
- TDD：默认 `DL|F|UL|UL|UL|`（UL 占空比更高）
- 场景/信道：`UMi_StreetCanyon_LoS`，并关闭 `ShadowingEnabled`（降低随机性）
- 波束：`DirectPathBeamforming`
- 天线：
  - UE：`NumRows=1, NumColumns=1`（SISO）
  - gNB：`NumRows=2, NumColumns=2`（2x2 阵列；但 UE 只有 1T1R，因此没有空间复用增益）
- 发射功率：gNB `30 dBm`，UE `23 dBm`
- 调度器：`NrMacSchedulerOfdmaRR`
- 核心网链路：`S1uLinkDelay=0ms`，PGW<->RemoteHost `10Gbps`，delay 默认 `0ms`

如果你后续想做“更严格的理论上限推导”，建议先把这些固定默认值一并作为前提写进实验描述里。

## 2. 本次任务中我做了哪些改动（尽量少动不相关参数）

改动范围：仅在 `myscripts/nr-dce/nr-dce.cc` 增加/调整了一些命令行参数与默认值，用于：

1) 明确打印当前 NR/iperf 关键配置；  
2) 可选切换到 **NR EESM Table2（含 256QAM）**；  
3) 可选“固定 MCS”做**理论峰值**实验；  
4) 修正 iperf 时间窗口与 `simTime` 的关系（避免 iperf 还没跑完仿真就 Stop）。

### 2.1 新增/调整的参数清单（含可选项与特点）

下面这些参数都可通过 `./waf --run "nr-dce --参数=值 ..."` 传入。

#### A. 256QAM / MCS / AMC 相关

- `--useNrEesmT2={0|1}`
  - `0`（默认行为 / 原来行为）：沿用原默认 `NrLteMiErrorModel`（最高 64QAM）。
    - 这意味着：即便 SINR 很高，也不会选到 256QAM；MCS 的谱效率上限约 `5.55 b/s/Hz`。
  - `1`（启用 256QAM / Table2 行为）：切到 `ns3::NrEesmCcT2`（TS38.214 Table2，**包含 256QAM（Qm=8）**），并同步设置：
    - `NrHelper::SetUlErrorModel("ns3::NrEesmCcT2")`
    - `NrHelper::SetDlErrorModel("ns3::NrEesmCcT2")`
    - `Config::SetDefault("ns3::NrAmc::ErrorModelType", ns3::NrEesmCcT2)`
  - 特点/注意：
    - 目标是把“可选的谱效率上限”从 ~5.55 抬到 ~7.41（Table2 顶部），从而抬高 UL goodput 上限（尤其是 UDP）。
    - 更高阶调制对 BLER 更敏感：**是否能拿到提升**取决于链路与是否“锁死”了过高的 MCS。

##### A.1 `NrLteMiErrorModel` vs `NrEesm...T2`：差别在哪里？什么时候用？

这两类模型都实现了 `NrErrorModel` 接口（见 `../ns-3.35/contrib/nr/model/nr-error-model.h`），给 AMC/PHY 提供：

- 给定 `cqi` 或 `mcs` 的谱效率表：`GetSpectralEfficiencyForCqi()` / `GetSpectralEfficiencyForMcs()`
- 给定 SINR+MCS 的 `TBLER`：`GetTbDecodificationStats()`
- 给定 MCS+资源块数计算 payload/TB size：`GetPayloadSize()` 等

核心差别（与你的“峰值吞吐”最相关的点）：

- `ns3::NrLteMiErrorModel`
  - **最高 64QAM（Qm=6）**，MCS 最大索引 **28**（`GetMaxMcs()=28`，见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.cc:693`）。
  - 链路到系统映射：基于 **Mutual Information（MIESM 类）** 的 BLER 估计（见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.h` 类注释）。
  - 编码/标准假设：按 LTE（TS 36.212/36.214）假设 **Turbo Coding** + 分段（见同上注释）。
  - HARQ 建模：注释写的是 **IR（Incremental Redundancy）**。
  - 适用场景：
    - 你不关心 256QAM，只想保持默认稳定/与历史结果一致；
    - 你在做 LTE/NR 对标，想要“更接近 LENA/LTE 的默认风格”。

- `ns3::NrEesmCcT2` / `ns3::NrEesmIrT2`（统称 `NrEesm...T2`）
  - 使用 TS 38.214 的 **Table2（含 256QAM，Qm=8）**。
  - Table2 的 MCS 最大索引为 **27（0..27）**（因为表里 28 个条目；见 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc:36` 与 `../ns-3.35/contrib/nr/model/nr-eesm-error-model.cc:412`）。
  - 链路到系统映射：基于 **EESM** 的 BLER 估计（见 `../ns-3.35/contrib/nr/model/nr-eesm-error-model.h` 类注释）。
  - 编码/标准假设：按 NR（TS 38.212/38.214）假设 **LDPC** + 分段（见同上注释）。
  - 适用场景：
    - 你想评估 NR 的 256QAM 对峰值吞吐的影响；
    - 你想使用 NR 的 EESM + 链路级 BLER 曲线（更贴近 NR 的 BLER-vs-SINR 表）。

补充：还有 `NrEesm...T1`（Table1），它仍然最高到 **64QAM**（见 `../ns-3.35/contrib/nr/model/nr-eesm-t1.cc:8177`），但使用 NR 的 EESM/LDPC 逻辑；适合作为“NR 但不启用 256QAM”的选择。

##### A.2 `NrEesmCc*` vs `NrEesmIr*`：CC/IR HARQ 有啥区别？什么时候用？

EESM 系列分两条线（见 `../ns-3.35/contrib/nr/model/nr-eesm-cc.h` 与 `../ns-3.35/contrib/nr/model/nr-eesm-ir.h`）：

- `NrEesmCc*`：HARQ **Chase Combining (CC)**。重传发同一份编码比特，等效码率不变，只是 SINR 叠加后再算等效 SINR。
- `NrEesmIr*`：HARQ **Incremental Redundancy (IR)**。重传发不同冗余比特，等效码率会变化，因此不仅要更新等效 SINR，还要更新等效码率。

什么时候选？

- 只想把 Table2/256QAM 引入并抬高峰值：优先 `NrEesmCcT2`（本脚本就是用它）。
- 明确需要建模 HARQ-IR 的码率累积效应：选 `NrEesmIrT2`。

> 术语澄清：你提到的 `stringMcsUl` 在脚本里实际叫 `startingMcsUl`，是一个整数索引（不是字符串）。

- `--fixedMcsUl={0|1}` / `--startingMcsUl=<int>`（UL 侧）
  - `fixedMcsUl=0`（默认）：UL 由 AMC 自适应选择 MCS（起始值由 `startingMcsUl` 给出，但会很快被 CQI/AMC 刷新）。
  - `fixedMcsUl=1`：UL MCS 固定为 `startingMcsUl`，用于“理论峰值/上限”测试。
  - 可选项（建议范围）：
    - 若 `useNrEesmT2=1`（Table2）：`startingMcsUl` 合理范围 `0..27`。
    - 若 `useNrEesmT2=0`（默认 LTE MI）：`startingMcsUl` 合理范围 `0..28`。
  - 特点/注意：
    - **固定到过高**的 MCS（比如 Table2 的 27）可能导致 TBLER 很高，吞吐反而更差（第 4 节有记录）。
    - 本仓库代码里对 `startingMcsUl` 做了“超范围 clamp”，避免填错导致断言。
  - `startingMcsUl` 在“自适应模式”里到底有什么用？
    - 调度器 `NrMacSchedulerNs3` 提供 `StartingMcsUl` 属性（见 `../ns-3.35/contrib/nr/model/nr-mac-scheduler-ns3.cc:131`）。
    - 当 CQI 过期（timer=0）时，会把 UL MCS 重置为 `StartingMcsUl`（见 `../ns-3.35/contrib/nr/model/nr-mac-scheduler-cqi-management.cc:192`）。
    - 所以它是“初始值/回退值”，不是“上限值”。（上限另有 `MaxDlMcs` 等属性，这里没有特意改。）

- `--fixedMcsDl={0|1}` / `--startingMcsDl=<int>`
  - 含义同上（DL 侧）。本次场景主要测 UL（UE->remoteHost），DL 侧参数一般不用动。

- `--amcModel=ErrorModel|ShannonModel`
  - 作用：控制 `NrAmc::CreateCqiFeedbackWbTdma()` 用哪种方法把 SINR 映射成 CQI/MCS（见 `../ns-3.35/contrib/nr/model/nr-amc.h:97`、`../ns-3.35/contrib/nr/model/nr-amc.cc:165`）。
  - `ErrorModel`（默认，较真实）：
    - 对每个候选 MCS 调用 error model 的 `GetTbDecodificationStats()`，找到满足 `TBLER <= 0.1` 的最高 MCS（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:240`）。
    - 特点：更贴近“可靠传输下的自适应”，但计算更重。
  - `ShannonModel`（更偏理论/近似，较平滑）：
    - 用香农式近似把 SINR 换算成谱效率 `s`（`nr-amc.cc` 里有公式注释），再把 `s` 映射回 CQI/MCS（使用 error model 的 SE 表）。
    - 特点：不使用 BLER 曲线，结果更“连续”、更像容量上限近似；但对真实 BLER/重传不敏感，和真实协议栈 goodput 可能偏离。
  - 注意：
    - 即使使用 `ShannonModel`，也**必须**配置 `ErrorModelType` 与 `NrSpectrumPhy` 一致（`nr-amc.h` 文件头部注释强调了这一点），因为 SE/CQI/MCS 的表仍来自 error model。

#### B. NR 场景/无线配置（本次基本不改，只做展示/可复现）

- `--tddPattern="DL|F|UL|UL|UL|"`  
  - TDD Pattern 字符串，当前默认就是你要求的 **DFUUU**（上行优先）。有些资料会写成 **DSUUU**，其中 `S`/`F` 都是在说“special/flexible slot”的概念；在 ns-3 NR 里用 `F` 表示 flexible。
- `--numerology=<int>`（默认 1）
- `--ueDistance=<double>`（默认 5m）
- `--simTime=<double>`（默认 12s）
  - 注意：代码里会确保 `simTime >= clientStart + iperfTime + 1`，不够会自动“抬高”并打印 WARNING。

#### C. iperf/DCE 相关（用于减少“非无线瓶颈”）

- `--useUdp={0|1}`
  - `0`：TCP（默认）
  - `1`：UDP（更适合做“链路上限/理论峰值”压测）
- `--udpRate="1G"|"500M"|...`
  - 仅 UDP 生效，对应 iperf `-b`；发送端会尽力按该速率发送，接收端显示实际吞吐与丢包。
- `--tcpWindow="1M"|...`
  - 仅 TCP 生效，对应 iperf `-w`（DCE iperf 旧版本窗口偏小时会限速）。
- `--tcpParallel=<int>`
  - 仅 TCP 生效，对应 iperf `-P`（多并发流更容易逼近链路上限）。
- `--tcpBufferBytes=<bytes-as-string>`
  - 会同步增大 DCE Linux 栈 `rmem/wmem` 上限，避免 socket buffer 成瓶颈。
- `--coreDelay="0ms"|...`
  - PGW<->RemoteHost 的 p2p delay；默认 `0ms` 以减少 RTT 对 TCP 的额外限制。
- `--serverStart=<double>` / `--clientStart=<double>`
  - 用于控制 iperf server/client 启动时刻；默认 `4.5/5.0`。
  - 注意：`clientStart` 需大于 `serverStart`，否则 client 可能连不上（代码会打印 WARNING）。

#### D. Trace/调试相关

- `--enableNrTraces={0|1}`
  - `0`（默认）：不生成 NR trace。
  - `1`：调用 `nrHelper->EnableTraces()`，会在当前目录生成 `RxPacketTrace.txt` 等文件。
  - 特点/注意：
    - `RxPacketTrace.txt` 可能很大（包含 DL/UL TB 的 mcs/sinr/tbler/corrupt 等）。
    - 用它可以判断“固定 256QAM 为啥反而更慢”：往往是 `TBler/corrupt` 明显升高造成的。

## 3. 需要注意的坑（本次遇到的现象与处理建议）

1) **固定到最高 256QAM MCS 并不等于更高吞吐**  
   在 `useNrEesmT2=1` 下固定 `startingMcsUl=27`，从 trace 可看到 corrupt/TBLER 显著升高，吞吐会掉到几十 Mbps 甚至更低。

2) **Table2 + 自适应 AMC 在 TCP 下可能“反而更慢”**  
   我在本环境复现到：`useNrEesmT2=1` 且 `fixedMcsUl=0` 时，TCP goodput 可能掉到 ~30Mbps（第 4.2 节有记录）。一个重要原因是：`NrAmc::CreateCqiFeedbackWbTdma()` 在 `ErrorModel` 模式下评估候选 MCS 的 `TBLER` 时，会调用 `CalculateTbSize(mcs, rbMap.size())`（见 `../ns-3.35/contrib/nr/model/nr-amc.cc`），其中 `rbMap.size()` 仅是“频域 RB 数”，不包含“本次实际分配的符号数”；在高阶调制/大 TBS 时这个偏差会被放大，导致选到过激的 MCS，实际 `corrupt/TBLER` 很高，TCP 拥塞窗口被打崩。  
   实操建议：想看“峰值趋势”优先 UDP；想测 TCP 时更推荐固定一个可承受的高 MCS（例如 25），而不是完全依赖 Table2 + 自适应。

3) UDP 更适合“理论峰值”对比，TCP 单连接会受拥塞控制/RTT/窗口等影响  
   所以“256QAM 提升”建议先用 UDP 做上限对比，再考虑 TCP（必要时用 `-P` 多流）。

4) `simTime` 不够会导致 iperf 跑不完、或者统计窗口不完整  
   代码里已经自动兜底：`simTime < clientStart + iperfTime + 1` 时会自动抬高并打印。

5) **个别组合在本仓库版本下可能触发崩溃（SIGSEGV/SIGBUS）**  
   我在尝试“UDP 800M/1G（高注入速率）”等组合时复现到 `SIGBUS` / `SIGSEGV`。建议先用本文第 4 节给出的 `500M~600M` 的稳定组合复现提升，再逐步扩大速率、开 trace 或固定更高 MCS。

## 4. 实验记录与总结（保持距离/频段不变）

> 固定条件：n79=4.9GHz、100MHz、SISO UE、TDD `DL|F|UL|UL|UL|`、UE 距离默认 5m、核心网 delay 默认 0ms。

### 4.1 UDP：最直观的“理论上限”提升

- **基线（默认 64QAM 上限）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=0 --useUdp=1 --udpRate=500M --iperfTime=2 --simTime=8"`
  - 结果：接收端约 **317 Mbps**（基本 0% 丢包）。

- **256QAM（EESM Table2）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=1 --useUdp=1 --udpRate=500M --iperfTime=2 --simTime=8"`
  - 结果：接收端约 **396 Mbps**（丢包约 **0.025%**）。

- **进一步拉高注入速率（验证是否已饱和）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=1 --useUdp=1 --udpRate=600M --iperfTime=2 --simTime=8"`
  - 结果：接收端仍约 **396 Mbps**（说明本配置下 UL 已接近饱和，上不去了）。

结论：在不改距离/频段等前提下，**把“默认 64QAM 天花板”抬到 Table2/256QAM 后，UL 上限从 ~317Mbps 提升到 ~396Mbps**。

### 4.2 TCP：固定一个“可承受”的高 MCS 才更稳

- **基线（默认 64QAM 上限，自适应 AMC）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=0 --iperfTime=3 --simTime=9"`
  - 结果：客户端（UE）侧约 **304 Mbps**。

- **Table2 + 自适应 AMC（不固定 MCS）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=1 --iperfTime=3 --simTime=9"`
  - 结果：客户端侧约 **29 Mbps**（明显变差；与第 3 节第 2 点讨论的现象一致）。

- **固定 MCS=27（Table2 最高）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=1 --fixedMcsUl=1 --startingMcsUl=27 --iperfTime=3 --simTime=9"`
  - 结果：吞吐明显下降（~20Mbps 量级）；trace 显示 UL corrupt/TBLER 明显升高。

- **固定 MCS=25（更现实/更稳的 256QAM 档位）**
  - 命令：`./waf --run "nr-dce --useNrEesmT2=1 --fixedMcsUl=1 --startingMcsUl=25 --iperfTime=3 --simTime=9"`
  - 结果：吞吐可到 **~379Mbps**（本环境复现值）。

结论：TCP 单连接想“接近理论峰值”，更好的做法是**选一个 BLER 可接受的高 MCS（例如 25）**，而不是直接强推最高 MCS。

### 4.3 `ShannonModel` 的实测：并不一定更高

在保持其他配置不变的情况下，我也测试了 `--amcModel=ShannonModel`：

- UDP（Table2 + Shannon）：接收端约 **378 Mbps**
- TCP（Table2 + Shannon）：客户端侧约 **27 Mbps**

因此在本脚本/本配置下，`ShannonModel` 更像是“另一种近似”，不保证更高 goodput。

### 4.4 一个更“贴近理论量级”的上限估算（为什么 ~396Mbps 合理）

在 Table2（256QAM）下，最高谱效率（CQI=15 / MCS=27）在源码表里是 **7.41 b/s/Hz**（见 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc`）。

粗略按“满带宽、满时域、无开销”的理想上限：

- `R_max_ideal ≈ 7.41 (b/s/Hz) × 100e6 (Hz) = 741 Mbps`

但我们是 TDD `DL|F|UL|UL|UL|`，上行并不是 100% 时域可用。用一个保守下界（只算明确的 3 个 UL slot，占比 3/5=0.6）：

- `R_ul_ideal_lower ≈ 741 Mbps × 0.6 = 444 Mbps`

再考虑控制符号、DMRS、调度粒度、HARQ/重传、协议栈开销等，把 ~444Mbps 拉低到 **~396Mbps** 是合理的数量级。

如果后续你希望把“理论峰值”写成更严格的公式上限（考虑 DSUUU 的 slot 配比、控制符号开销、DMRS/参考信号开销、协议栈开销等），建议先约定：

- `F` slot 的具体符号分配（DL/UL 控制与数据占用），以及
- 你希望比较的是 **PHY 层净比特率** 还是 **UDP/TCP goodput**。

## 5. 常见疑问（读代码后补充的 FAQ）

### 5.1 CQI 为什么是 `0..15`？每个值具体代表什么？

`NrAmc` 在 `GetMcsFromCqi()` 里断言 `cqi` 必须在 `0..15`（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:96`），因为它使用的是“标准化的 16 级 CQI”。

在这套实现里，**CQI 的含义不是“某种固定调制方式”**，而是一个“谱效率等级索引”：

- 每个 error model 都提供一个长度为 16 的 `SpectralEfficiencyForCqi` 表；
- CQI=0 表示 out-of-range，其谱效率为 0（一般可理解为“极差/不适合传输”）；
- CQI=1..15 表示逐渐更高的目标谱效率。

为了避免“...省略号看不懂”，这里把两张最常用的 CQI->SE 表完整列出来（直接来自源码常量表）：

- `NrLteMiErrorModel`（默认，最高约 5.55 b/s/Hz；见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.cc`）：

  | CQI | SE (b/s/Hz) |
  |---:|---:|
  | 0 | 0.00 |
  | 1 | 0.15 |
  | 2 | 0.23 |
  | 3 | 0.38 |
  | 4 | 0.60 |
  | 5 | 0.88 |
  | 6 | 1.18 |
  | 7 | 1.48 |
  | 8 | 1.91 |
  | 9 | 2.41 |
  | 10 | 2.73 |
  | 11 | 3.32 |
  | 12 | 3.90 |
  | 13 | 4.52 |
  | 14 | 5.12 |
  | 15 | 5.55 |

- `NrEesm...T2`（Table2，含 256QAM，最高约 7.41 b/s/Hz；见 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc`）：

  | CQI | SE (b/s/Hz) |
  |---:|---:|
  | 0 | 0.00 |
  | 1 | 0.15 |
  | 2 | 0.38 |
  | 3 | 0.88 |
  | 4 | 1.48 |
  | 5 | 1.91 |
  | 6 | 2.41 |
  | 7 | 2.73 |
  | 8 | 3.32 |
  | 9 | 3.90 |
  | 10 | 4.52 |
  | 11 | 5.12 |
  | 12 | 5.55 |
  | 13 | 6.23 |
  | 14 | 6.91 |
  | 15 | 7.41 |

因此同样的 CQI 值，在不同 error model 下对应的 SE 可能不同（尤其 Table2 的顶部更高）。

`GetMcsFromCqi()` 的策略是：

1) 取 `SE = ErrorModel.GetSpectralEfficiencyForCqi(cqi)`；
2) 找到 SE 不超过这个值的“最高 MCS”（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:96` 的 while 循环）。

### 5.2 ShannonModel 到底是什么？它跟 ErrorModel 模式差在哪？

`NrAmc::AmcModel` 有两个取值（见 `../ns-3.35/contrib/nr/model/nr-amc.h:97`）：

- `ErrorModel`：
  - 对每个候选 MCS 用 error model 计算 TBLER；
  - 选出满足 `TBLER<=0.1` 的最高 MCS（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:240`）。
  - 更像真实链路自适应（但依赖 BLER 曲线与 TB size 计算）。

- `ShannonModel`：
  - 先用一个香农式的近似公式从 SINR 得到谱效率 `s`（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:181` 的注释公式）；
  - 再把 `s` 映射到 CQI/MCS（仍然使用 error model 的 SE 表）。
  - 更像“容量上限的平滑近似”，不直接用 BLER 曲线，因此对重传/丢包不敏感。

补充细节（直接对应代码）：

- ShannonModel 用的谱效率近似公式（`sinr_` 为**线性值**）：
  - `s = log2( 1 + sinr_ / ( (-ln(5*BER))/1.5 ) )`
  - 这来自 `../ns-3.35/contrib/nr/model/nr-amc.cc` 中的注释公式（在 `CreateCqiFeedbackWbTdma()` 的 Shannon 分支）。
- 其中 `BER` 不是你配置的，而是 `NrAmc::GetBer()` 根据 error model 类型选一个固定值（见 `../ns-3.35/contrib/nr/model/nr-amc.cc:349`）：
  - 若 error model 是 `NrLteMiErrorModel` 或 `LenaErrorModel`：`BER=5e-5`
  - 否则（NR EESM 系列）：`BER=1e-5`

什么时候用？

- 你想做“比较理想化”的上限估计，或想减少 BLER 曲线带来的离散跳变：可试 `ShannonModel`。
- 你想要“更贴近可靠传输”的行为：用默认 `ErrorModel`。

### 5.3 还有哪些 error model？它们有什么特点，什么时候会选？

`NrErrorModel` 的注释列出了 NR 模块主要支持的类型（见 `../ns-3.35/contrib/nr/model/nr-error-model.h:41`）：

- `NrEesmCcT1`：NR EESM + Table1（最高 64QAM，MCS 0..28）。
- `NrEesmCcT2`：NR EESM + Table2（含 256QAM，MCS 0..27）。
- `NrEesmIrT1`：NR EESM + HARQ-IR + Table1。
- `NrEesmIrT2`：NR EESM + HARQ-IR + Table2（含 256QAM）。
- `NrLteMiErrorModel`：默认/兼容性较好的 LTE MI 风格模型（最高 64QAM，MCS 0..28）。

此外，仓库里还有一个常见的“对标/兼容”模型：

- `LenaErrorModel`（见 `../ns-3.35/contrib/nr/model/lena-error-model.h`）
  - 目的不是提高峰值，而是为了让 TB size 的计算方式更接近 ns-3 传统 LTE LENA 模块（它会对 RB/符号粒度做假设与折算）。
  - 如果你在做 NR 与 LENA/LTE 的对标、或迁移老脚本想保持数值一致性，才更可能用到它。

### 5.4 MCS 索引如何对应到“到底是 QPSK/16QAM/64QAM/256QAM”？

很多时候你看到 “MCS=25” 会本能地问：这是不是 256QAM？答案是：**取决于你选的 error model 表**。

两个最常用的例子（直接来自源码表）：

- `NrLteMiErrorModel`（最高 64QAM）
  - 通过 `ModulationSchemeForMcs[]` 决定每个 MCS 的 `Qm`（见 `../ns-3.35/contrib/nr/model/nr-lte-mi-error-model.cc:300`）。
  - 其分段大致是：
    - MCS `0..9`：QPSK（Qm=2）
    - MCS `10..16`：16QAM（Qm=4）
    - MCS `17..28`：64QAM（Qm=6）

- `NrEesm...T2`（Table2，包含 256QAM）
  - 通过 `McsMTable2` 决定每个 MCS 的 `Qm`（见 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc:51`）。
  - 其分段大致是：
    - MCS `0..4`：QPSK（Qm=2）
    - MCS `5..10`：16QAM（Qm=4）
    - MCS `11..19`：64QAM（Qm=6）
    - MCS `20..27`：256QAM（Qm=8）

为了回答你提到的“除了 QAM 之外还和最大码率有关吗？”——在这套实现里，“码率”主要体现在 `ECR`（effective code rate）与对应的 `SE` 表上。以 Table2 的 256QAM 段（MCS 20..27）为例（数据来自 `../ns-3.35/contrib/nr/model/nr-eesm-t2.cc`）：

| MCS | Qm | ECR | SE (b/s/Hz) |
|---:|---:|---:|---:|
| 20 | 8 | 0.67 | 5.33 |
| 21 | 8 | 0.69 | 5.55 |
| 22 | 8 | 0.74 | 5.89 |
| 23 | 8 | 0.77 | 6.23 |
| 24 | 8 | 0.82 | 6.57 |
| 25 | 8 | 0.86 | 6.91 |
| 26 | 8 | 0.90 | 7.16 |
| 27 | 8 | 0.93 | 7.41 |

这也解释了为什么 “把 startingMcsUl 从 25 调到 27” 有时会让吞吐变差：它们都属于 256QAM 档，但码率更激进、对 SINR/BLER 要求更高。

### 5.5 面向本项目的选型建议：我到底该选哪个 ErrorModel / AmcModel？

如果你的目标是“n79 100MHz SISO、TDD DFUUU 下的理论峰值/上限趋势”：

- 推荐：`useNrEesmT2=1` + `useUdp=1`（UDP goodput 更直接反映链路上限）。

如果你的目标是“在可靠性约束下（TBLER<=10%）更真实的自适应行为”：

- 推荐：`useNrEesmT2=1` + `amcModel=ErrorModel`（默认）。
- 不建议直接 `fixedMcsUl=1` 锁到最高 MCS；更像是“人造上限”实验。

如果你的目标是“更简单、更快、更接近历史默认结果（但不会到 256QAM）”：

- 推荐：`useNrEesmT2=0`（默认 `NrLteMiErrorModel`）。
