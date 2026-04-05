/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/ipv4-static-routing-helper.h"
#include "ns3/log.h"
#include "ns3/trace-helper.h"

// NR / EPC
#include "ns3/nr-module.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/antenna-module.h"
#include "ns3/nr-mac-scheduler-ofdma-rr.h"
#include "ns3/nr-amc.h"
// DCE
#include "ns3/dce-module.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("NrDceIperfDemo");

void setPos (Ptr<Node> n, int x, int y, int z)
{
  Ptr<ConstantPositionMobilityModel> loc = CreateObject<ConstantPositionMobilityModel> ();
  n->AggregateObject (loc);
  Vector locVec2 (x, y, z);
  loc->SetPosition (locVec2);
}

void
PrintTcpFlags (std::string key, std::string value)
{
  std::cout << key << "=" << value << std::endl;
}

int
main (int argc, char *argv[])
{
  // ---------- 一些配置参数 ----------
  double simTime = 12.0;         // 仿真总时长
  double iperfTime = 10.0;       // iperf 运行时间 (秒)
  double serverStart = 4.5;      // iperf server 启动时间 (秒)
  double clientStart = 5.0;      // iperf client 启动时间 (秒)
  double frequency = 4.9e9;      // 4.9 GHz (China Mobile n79 band)
  double bandwidth = 100e6;      // 100 MHz 带宽
  uint16_t numerology = 1;       // n79 常见 30kHz SCS
  double ueDistance = 5.0;       // UE 与 gNB 距离（米）
  std::string tddPattern = "DL|F|UL|UL|UL|"; // DFUUU，上行优先
  bool useNrEesmT2 = false;      // 使用 NR EESM Table2 (最高 256QAM) 的误码模型/AMC
  bool fixedMcsUl = true;       // 固定 UL MCS（用于“理论上限”测试）
  bool fixedMcsDl = true;       // 固定 DL MCS（用于“理论上限”测试）
  uint16_t startingMcsUl = 27;   // Table2 下最高 MCS=27（0..27）
  uint16_t startingMcsDl = 27;   // Table2 下最高 MCS=27（0..27）
  std::string amcModel = "ErrorModel"; // NrAmc::AmcModel: ErrorModel/ShannonModel
  bool enableNrTraces = false;   // 生成 NR PHY/MAC trace（RxPacketTrace*.txt 等）
  bool useUdp = false;           // 是否用 iperf UDP 模式
  std::string udpRate = "1G";   // 如果用 UDP，对应 -b 速率
  std::string tcpWindow = "1M";  // 旧版 DCE iperf 默认窗口太小，1M 在本场景更稳
  std::string tcpBufferBytes = "16777216"; // 同时放大 DCE Linux socket/TCP buffer 上限
  uint32_t tcpParallel = 1;      // 用多并发流逼近链路上限
  std::string coreDelay = "0ms"; // EPC 到远端主机时延（减少 RTT 限速）

  GlobalValue::Bind("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (0)));

  CommandLine cmd;
  cmd.AddValue ("simTime", "Simulation time in seconds", simTime);
  cmd.AddValue ("iperfTime", "Iperf running time in seconds", iperfTime);
  cmd.AddValue ("serverStart", "Iperf server start time in seconds", serverStart);
  cmd.AddValue ("clientStart", "Iperf client start time in seconds", clientStart);
  cmd.AddValue ("numerology", "NR numerology", numerology);
  cmd.AddValue ("ueDistance", "Distance between UE and gNB in meters", ueDistance);
  cmd.AddValue ("tddPattern", "NR TDD pattern, e.g. DL|UL|UL|UL|", tddPattern);
  cmd.AddValue ("useNrEesmT2", "Use NR EESM error model Table2 (up to 256QAM) for AMC", useNrEesmT2);
  cmd.AddValue ("fixedMcsUl", "Fix UL MCS to startingMcsUl", fixedMcsUl);
  cmd.AddValue ("fixedMcsDl", "Fix DL MCS to startingMcsDl", fixedMcsDl);
  cmd.AddValue ("startingMcsUl", "Starting (and fixed) UL MCS index", startingMcsUl);
  cmd.AddValue ("startingMcsDl", "Starting (and fixed) DL MCS index", startingMcsDl);
  cmd.AddValue ("amcModel", "AMC model: ErrorModel or ShannonModel", amcModel);
  cmd.AddValue ("enableNrTraces", "Enable NR PHY/MAC traces (RxPacketTrace*.txt)", enableNrTraces);
  cmd.AddValue ("useUdp", "Use iperf UDP mode (-u)", useUdp);
  cmd.AddValue ("udpRate", "Iperf -b value if UDP", udpRate);
  cmd.AddValue ("tcpWindow", "Iperf TCP window size (e.g., 4M)", tcpWindow);
  cmd.AddValue ("tcpBufferBytes", "Linux tcp/core rmem/wmem upper bound in bytes", tcpBufferBytes);
  cmd.AddValue ("tcpParallel", "Iperf TCP parallel flow count (-P)", tcpParallel);
  cmd.AddValue ("coreDelay", "PGW-RemoteHost p2p delay", coreDelay);
  cmd.Parse (argc, argv);

  // 确保仿真总时长足够覆盖 iperf 运行窗口，避免应用在 iperf 正常结束前被 Stop() 强行杀掉
  const double minSimTime = clientStart + iperfTime + 1.0;
  if (simTime < minSimTime)
    {
      NS_LOG_UNCOND ("[nr-dce] WARNING: simTime(" << simTime
                                                  << "s) too small; bump to " << minSimTime
                                                  << "s so iperf can finish");
      simTime = minSimTime;
    }

  const uint16_t maxMcsIndex = useNrEesmT2 ? 27 : 28; // EESM Table2: 0..27, LTE MI: 0..28
  if (startingMcsUl > maxMcsIndex)
    {
      NS_LOG_UNCOND ("[nr-dce] WARNING: startingMcsUl(" << startingMcsUl
                                                        << ") > maxMcs(" << maxMcsIndex
                                                        << "); clamp");
      startingMcsUl = maxMcsIndex;
    }
  if (startingMcsDl > maxMcsIndex)
    {
      NS_LOG_UNCOND ("[nr-dce] WARNING: startingMcsDl(" << startingMcsDl
                                                        << ") > maxMcs(" << maxMcsIndex
                                                        << "); clamp");
      startingMcsDl = maxMcsIndex;
    }

  if (clientStart <= serverStart)
    {
      NS_LOG_UNCOND ("[nr-dce] WARNING: clientStart <= serverStart; client may fail to connect");
    }
  if (iperfTime <= 0.0)
    {
      NS_LOG_UNCOND ("[nr-dce] WARNING: iperfTime <= 0; iperf will not run");
    }

  NrAmc::AmcModel amcModelEnum = NrAmc::ErrorModel;
  if (amcModel == "ErrorModel")
    {
      amcModelEnum = NrAmc::ErrorModel;
    }
  else if (amcModel == "ShannonModel")
    {
      amcModelEnum = NrAmc::ShannonModel;
    }
  else
    {
      NS_LOG_UNCOND ("[nr-dce] ERROR: invalid amcModel=\"" << amcModel
                                                           << "\" (use ErrorModel or ShannonModel)");
      return 2;
    }

  // 为了兼容 NR 老版本里用到 LTE RLC 的地方
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (999999999));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (999999999));
  
  // LogComponentEnable("DceKernelSocketFdFactory", LOG_ERROR);
  // LogComponentEnable("Node", LOG_FUNCTION);

  // ---------- 创建节点 ----------
  NodeContainer enbNodes;
  NodeContainer ueNodes;
  enbNodes.Create (1);
  ueNodes.Create (1);

  // RemoteHost：外部一台「服务器」
  NodeContainer remoteHostContainer;
  remoteHostContainer.Create (1);
  Ptr<Node> remoteHost = remoteHostContainer.Get (0);

  // ---------- 放置 gNB / UE ----------
  MobilityHelper mobility;

  Ptr<ListPositionAllocator> enbPos = CreateObject<ListPositionAllocator> ();
  enbPos->Add (Vector (0.0, 0.0, 8.0)); // gNB：高度 8m
  mobility.SetPositionAllocator (enbPos);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (enbNodes);

  Ptr<ListPositionAllocator> uePos = CreateObject<ListPositionAllocator> ();
  uePos->Add (Vector (ueDistance, 0.0, 1.5)); // UE：近距离测速，默认 5m
  mobility.SetPositionAllocator (uePos);
  mobility.Install (ueNodes);

  // ---------- NR / EPC 帮助类 ----------
  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  Ptr<IdealBeamformingHelper> bfHelper = CreateObject<IdealBeamformingHelper> ();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();

  nrHelper->SetEpcHelper (epcHelper);
  nrHelper->SetBeamformingHelper (bfHelper);

  // ---------- 频谱配置：1 个 Band，1 个 CC，1 个 BWP ----------
  CcBwpCreator ccBwpCreator;
  const uint8_t numCcPerBand = 1;
  CcBwpCreator::SimpleOperationBandConf bandConf (frequency,
                                                  bandwidth,
                                                  numCcPerBand,
                                                  BandwidthPartInfo::UMi_StreetCanyon_LoS);
  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
  nrHelper->InitializeOperationBand (&band);
  BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({band});

  // ---------- NR 物理层和调度器等 ----------
  // 关闭 Shadowing，减少随机性
  nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));

  nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaRR::GetTypeId ());
  nrHelper->SetSchedulerAttribute ("FixedMcsUl", BooleanValue (fixedMcsUl));
  nrHelper->SetSchedulerAttribute ("FixedMcsDl", BooleanValue (fixedMcsDl));
  nrHelper->SetSchedulerAttribute ("StartingMcsUl", UintegerValue (static_cast<uint8_t> (startingMcsUl)));
  nrHelper->SetSchedulerAttribute ("StartingMcsDl", UintegerValue (static_cast<uint8_t> (startingMcsDl)));

  nrHelper->SetGnbDlAmcAttribute ("AmcModel", EnumValue (amcModelEnum));
  nrHelper->SetGnbUlAmcAttribute ("AmcModel", EnumValue (amcModelEnum));

  // 波束成形：直达路径
  bfHelper->SetAttribute ("BeamformingMethod",
                          TypeIdValue (DirectPathBeamforming::GetTypeId ()));

  // 参考 contrib/nr 吞吐示例，把 EPC 内部链路时延压到 0，避免单 TCP 流被 RTT 额外拖低。
  epcHelper->SetAttribute ("S1uLinkDelay", TimeValue (MilliSeconds (0)));

  // UE: 手机
  nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
  nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2));
  nrHelper->SetUeAntennaAttribute (
      "AntennaElement",
      PointerValue (CreateObject<ThreeGppAntennaModel> ()));

  // gNB: 4T4R，按 2x2 阵列建模
  nrHelper->SetGnbAntennaAttribute ("NumRows", UintegerValue (2));
  nrHelper->SetGnbAntennaAttribute ("NumColumns", UintegerValue (2));
  nrHelper->SetGnbAntennaAttribute (
      "AntennaElement",
      PointerValue (CreateObject<ThreeGppAntennaModel> ()));

  // 近距离 UL 测试：gNB 下行控制信号保持常规功率，UE 发射功率设成手机常见上限附近
  nrHelper->SetGnbPhyAttribute ("TxPower", DoubleValue (40.0));
  nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (23.0));

  if (useNrEesmT2)
    {
      // NR 的 CQI/MCS Table2（含 256QAM）+ LDPC 的 EESM 误码模型
      // 说明：默认 NrAmc::ErrorModelType 是 NrLteMiErrorModel（最高 64QAM），会显著限制峰值速率。
      // 与 NR 栈当前 HARQ 实现（Chase Combining）保持一致，选用 CcT2。
      Config::SetDefault ("ns3::NrAmc::ErrorModelType", TypeIdValue (TypeId::LookupByName ("ns3::NrEesmCcT2")));
      nrHelper->SetDlErrorModel ("ns3::NrEesmCcT2");
      nrHelper->SetUlErrorModel ("ns3::NrEesmCcT2");
    }

  NS_LOG_UNCOND ("[nr-dce] numerology=" << numerology
                                        << " bw=" << bandwidth / 1e6 << "MHz"
                                        << " freq=" << frequency / 1e9 << "GHz"
                                        << " tddPattern=\"" << tddPattern << "\""
                                        << " ueDistance=" << ueDistance << "m");
  NS_LOG_UNCOND ("[nr-dce] amcModel=" << amcModel
                                        << " errorModelType=" << (useNrEesmT2 ? "NrEesmCcT2(Table2, up to 256QAM)" : "NrLteMiErrorModel(up to 64QAM)")
                                        << " fixedMcsUl=" << fixedMcsUl << " startingMcsUl=" << startingMcsUl
                                        << " fixedMcsDl=" << fixedMcsDl << " startingMcsDl=" << startingMcsDl);

  NetDeviceContainer enbDevs = nrHelper->InstallGnbDevice (enbNodes, allBwps);
  NetDeviceContainer ueDevs = nrHelper->InstallUeDevice (ueNodes, allBwps);

  int64_t randomStream = 1;
  randomStream += nrHelper->AssignStreams (enbDevs, randomStream);
  randomStream += nrHelper->AssignStreams (ueDevs, randomStream);

  // ---------- 安装 NR NetDevice ----------
  // 手动设置 Numerology 和 TDD pattern
  nrHelper->GetGnbPhy (enbDevs.Get (0), 0)->SetTxPower (30.0);
  nrHelper->GetGnbPhy (enbDevs.Get (0), 0)->SetAttribute ("Numerology",
                                                          UintegerValue (numerology));
  nrHelper->GetGnbPhy (enbDevs.Get (0), 0)->SetAttribute ("Pattern",
                                                          StringValue (tddPattern));

  // 更新配置
  for (auto it = enbDevs.Begin (); it != enbDevs.End (); ++it)
    {
      DynamicCast<NrGnbNetDevice> (*it)->UpdateConfig ();
    }
  for (auto it = ueDevs.Begin (); it != ueDevs.End (); ++it)
    {
      DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();
    }

  // ---------- EPC / IP 组网 ----------
  // PGW 节点
  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  setPos (pgw, 15, 0, 5);

  // Linux 栈
  LinuxStackHelper stack;
  stack.Install (ueNodes);
  stack.Install (remoteHostContainer);

  NodeContainer linuxNodes;
  linuxNodes.Add (ueNodes);
  linuxNodes.Add (remoteHostContainer);


  // PGW <-> RemoteHost 的 p2p 链路
  PointToPointHelper p2ph;
  p2ph.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  p2ph.SetChannelAttribute ("Delay", StringValue (coreDelay));
  p2ph.SetDeviceAttribute ("Mtu", UintegerValue (2500));
  NetDeviceContainer internetDevices = p2ph.Install (pgw, remoteHost);

  // 在 p2p 链路上启用 pcap
  // p2ph.EnablePcapAll ("nr-dce", false);

  // p2p 链路 1.0.0.0/8 网段
  Ipv4AddressHelper ipv4h;
  ipv4h.SetBase ("1.0.0.0", "255.0.0.0");
  Ipv4InterfaceContainer internetIpIfaces = ipv4h.Assign (internetDevices);

  Ipv4StaticRoutingHelper ipv4RoutingHelper;

  // nrHelper->EnableTraces();

  // 在 remoteHost / UE 上显式配置路由
  {
    // RemoteHost: 把 7.0.0.0/8 走 PGW
    std::ostringstream rt;
    Ipv4Address pgwAddr = internetIpIfaces.GetAddress (0);
    rt << "route add 7.0.0.0/8 via " << pgwAddr << " dev sim0";
    LinuxStackHelper::RunIp (remoteHost, Seconds (0.1), rt.str ().c_str ());
    // LinuxStackHelper::RunIp (remoteHost, Seconds (0.15), "route show table all");
  }

  // EPC 给 UE 分配 7.x.x.x 地址
  Ipv4InterfaceContainer ueIpIfaces =
      epcHelper->AssignUeIpv4Address (ueDevs);

  {
    // UE: 默认路由到 EPC 网关
    std::ostringstream rt;
    Ipv4Address ueDefaultGateway = epcHelper->GetUeDefaultGatewayAddress ();
    rt << "route add default via " << ueDefaultGateway << " dev sim0";
    LinuxStackHelper::RunIp (ueNodes.Get (0), Seconds (0.1), rt.str ().c_str ());
    // LinuxStackHelper::RunIp (ueNodes.Get (0), Seconds (0.15), "route show table local");
  }

  // 设置MTU和TCP优化参数
  LinuxStackHelper::RunIp (
  ueNodes.Get (0),
  Seconds (0.25),
  "link set dev sim0 mtu 1500"
);

  LinuxStackHelper::RunIp (
  remoteHost,
  Seconds (0.25),
  "link set dev sim0 mtu 1500"
);


  // UE 附着到最近 gNB
  nrHelper->AttachToClosestEnb (ueDevs, enbDevs);

  if (enableNrTraces)
    {
      nrHelper->EnableTraces ();
    }

  // {
  //   LinuxStackHelper::RunIp (ueNodes.Get (0), Seconds (1), "addr show");
  //   LinuxStackHelper::RunIp (ueNodes.Get (0), Seconds (1), "rule show");
  //   LinuxStackHelper::RunIp (ueNodes.Get (0), Seconds (1), "route show table all");
  //   LinuxStackHelper::RunIp (remoteHost, Seconds (1), "addr show");
  //   LinuxStackHelper::RunIp (remoteHost, Seconds (1), "rule show");
  //   LinuxStackHelper::RunIp (remoteHost, Seconds (1), "route show table all");
  // }

  // ---------- 在节点上安装 DCE（只装在需要跑 iperf 的节点） ----------
  DceManagerHelper dceManager;
  dceManager.SetNetworkStack ("ns3::LinuxSocketFdFactory", "Library", StringValue ("liblinux.so"));

  NodeContainer dceNodes;
  dceNodes.Add (remoteHost);
  dceNodes.Add (ueNodes);
  dceManager.Install (dceNodes);

  stack.SysctlSet (linuxNodes, ".net.core.wmem_default", tcpBufferBytes);
  stack.SysctlSet (linuxNodes, ".net.core.wmem_max", tcpBufferBytes);
  stack.SysctlSet (linuxNodes, ".net.core.rmem_default", tcpBufferBytes);
  stack.SysctlSet (linuxNodes, ".net.core.rmem_max", tcpBufferBytes);
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_rmem",
                   "4096 87380 " + tcpBufferBytes);
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_wmem",
                   "4096 65536 " + tcpBufferBytes);
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_window_scaling", "1");

  // ---------- 配置 DCE iperf ----------
  DceApplicationHelper dce;
  dce.SetStackSize (1 << 22); // 1 MB 栈

  ApplicationContainer apps;

  {
    dce.SetBinary ("iperf");
    dce.ResetArguments ();
    dce.ResetEnvironment ();

    dce.AddArgument ("-s"); // server 模式
    dce.AddArgument ("-i");
    dce.AddArgument ("1");     
    if (useUdp)
      {
        dce.AddArgument ("-u");
      }
    else if (tcpWindow != "0")
      {
        dce.AddArgument ("-w");
        dce.AddArgument (tcpWindow);
      }

    // 上行测速：Server 放在 remoteHost
    apps = dce.Install (remoteHost);
    apps.Start (Seconds (serverStart));
    apps.Stop (Seconds (simTime + 1.0));
  }

  {
    dce.SetBinary ("iperf");
    dce.ResetArguments ();
    dce.ResetEnvironment ();

    Ipv4Address serverAddr = internetIpIfaces.GetAddress (1);

    std::ostringstream oss;
    serverAddr.Print (oss);

    dce.AddArgument ("-c");
    dce.AddArgument (oss.str ()); // 目标地址 (RemoteHost IP)
    dce.AddArgument ("-i");
    dce.AddArgument ("1");        // 每秒打印一次
    dce.AddArgument ("--time");
    dce.AddArgument (std::to_string ((int)iperfTime)); // 运行时长

    if (useUdp)
      {
        dce.AddArgument ("-u");
        dce.AddArgument ("-b");
        dce.AddArgument (udpRate); // 比如 1G / 2G / 500M
      }
    else
      {
        if (tcpWindow != "0")
          {
            dce.AddArgument ("-w");
            dce.AddArgument (tcpWindow);
          }
        if (tcpParallel > 1)
          {
            dce.AddArgument ("-P");
            dce.AddArgument (std::to_string (tcpParallel));
          }
      }

    // 上行测速：Client 放在 UE
    apps = dce.Install (ueNodes.Get (0));
    apps.Start (Seconds (clientStart));         // 让 server 先起来
    apps.Stop (Seconds (simTime + 1.0));
  }

  // ---------- 运行仿真 ----------
  Simulator::Stop (Seconds (simTime + 2.0));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
