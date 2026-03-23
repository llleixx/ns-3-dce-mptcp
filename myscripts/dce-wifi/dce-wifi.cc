/*
 * dce-wifi.cc
 * 主流高性能家用WiFi 6路由器配置：
 * 1. WiFi 6 (802.11ax) 5GHz模式
 * 2. 160MHz带宽
 * 3. 不对称MIMO配置: AP为4x4 MIMO, 设备为2x2 MIMO
 * 4. 信道36开始的160MHz配置 (CH36-CH64，中心频率5250MHz)
 * 5. 发射功率: 23dBm - 标准家用路由器配置
 * 6. 使用 ns-3.35 可用的 WiFi 6 配置参数
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-standards.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/dce-module.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DceWifiIperfAxNoSysctl");

static void
TraceStaTxPsdu (WifiConstPsduMap psduMap, WifiTxVector txVector, double txPowerW)
{
  (void) psduMap;
  (void) txPowerW;

  const auto modClass = txVector.GetMode ().GetModulationClass ();
  if ((modClass != WIFI_MOD_CLASS_HT) && (modClass != WIFI_MOD_CLASS_VHT) && (modClass != WIFI_MOD_CLASS_HE))
    {
      return;
    }

  std::ostringstream oss;
  oss << txVector.GetMode ().GetUniqueName ()
      << " width=" << txVector.GetChannelWidth () << "MHz"
      << " nss=" << +txVector.GetNss ()
      << " gi=" << txVector.GetGuardInterval () << "ns";
  const std::string now = oss.str ();

  static std::string last;
  if (now == last)
    {
      return;
    }
  last = now;

  const uint64_t dataRateBps = txVector.GetMode ().GetDataRate (txVector.GetChannelWidth (),
                                                               txVector.GetGuardInterval (),
                                                               txVector.GetNss ());
  std::cout << std::fixed << std::setprecision (6)
            << "[wifi-tx] t=" << Simulator::Now ().GetSeconds () << "s "
            << now << " dataRate=" << (static_cast<double> (dataRateBps) / 1e6) << "Mbps"
            << std::endl;
}

int main (int argc, char *argv[])
{
  bool printTxVector = false;
  uint16_t channelWidthMhz = 160;
  uint16_t frequencyMhz = 5250;
  // 为了更贴近常见 Wi‑Fi 6 终端/路由器在“信号很好、单用户测速”场景下的表现，
  // 这里默认把 HE GI 与聚合相关参数设为更激进的值。
  // 如果需要复现 ns-3.35 的“原始默认行为”，可在命令行显式指定：
  //   --heGuardIntervalNs=3200 --heMpduBufferSize=64 --beMaxAmpduSize=65535 --tcpSockBufMaxBytes=0
  uint16_t heGuardIntervalNs = 800;  // ns-3.35 默认是 3200ns
  uint16_t heMpduBufferSize = 256;   // ns-3.35 默认是 64
  uint32_t beMaxAmpduSize = 6500631; // HE cap（ns-3.35 默认是 65535）
  // 让 iperf -w 8M 在 DCE Linux 中能拿到更接近真实系统的 socket buffer（Linux 会做倍增）
  uint32_t tcpSockBufMaxBytes = 16777216; // 16MiB；设为 0 则保持 DCE Linux 默认
  uint32_t iperfTimeSec = 20;
  uint32_t iperfParallelFlows = 1;

  CommandLine cmd;
  cmd.AddValue ("printTxVector", "Print (MCS,width,NSS,GI) when it changes", printTxVector);
  cmd.AddValue ("channelWidthMhz", "Wi-Fi channel width in MHz", channelWidthMhz);
  cmd.AddValue ("frequencyMhz", "Wi-Fi center frequency in MHz", frequencyMhz);
  cmd.AddValue ("heGuardIntervalNs", "HE guard interval in ns (800/1600/3200)", heGuardIntervalNs);
  cmd.AddValue ("heMpduBufferSize", "HE MPDU buffer size (64-256)", heMpduBufferSize);
  cmd.AddValue ("beMaxAmpduSize", "Max A-MPDU size (bytes) for AC_BE (0 disables A-MPDU)", beMaxAmpduSize);
  cmd.AddValue ("tcpSockBufMaxBytes", "If >0, set DCE Linux rmem/wmem/tcp_{r,w}mem max to this value", tcpSockBufMaxBytes);
  cmd.AddValue ("iperfTimeSec", "iperf client duration in seconds", iperfTimeSec);
  cmd.AddValue ("iperfParallelFlows", "iperf client parallel TCP flows (-P)", iperfParallelFlows);
  cmd.Parse (argc, argv);

  // 1. [DCE 必需] 开启 Checksum
  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));

  // 2. 创建节点
  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (1);
  NodeContainer wifiApNode;
  wifiApNode.Create (1);
  
  NodeContainer allNodes;
  allNodes.Add (wifiApNode);
  allNodes.Add (wifiStaNodes);

  // 3. 配置 Wi-Fi 物理层公共参数
  YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
  YansWifiPhyHelper phy;
  phy.SetChannel (channel.Create ());

  // --- [核心优化] Wi-Fi 6 主流高性能配置 ---
  // 带宽: 160MHz (WiFi 6最大带宽)
  phy.Set ("ChannelWidth", UintegerValue (channelWidthMhz));
  // 频率: 5250MHz - 从信道36开始的160MHz配置
  phy.Set ("Frequency", UintegerValue (frequencyMhz));
  
  // 发射功率: 23dBm (100mW) - 家用路由器标准
  phy.Set ("TxPowerStart", DoubleValue (23));
  phy.Set ("TxPowerEnd", DoubleValue (23));

  // 4. 配置 Wi-Fi MAC 公共参数
  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
  // IdealWifiManager 会根据双方能力自动协商到最高支持的 MCS
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");

  WifiMacHelper mac;
  Ssid ssid = Ssid ("ns-3-wifi6-router");

  // ========================================================
  // 5. [差异化配置] 分别设置 AP 和 STA 的 MIMO 能力
  // ========================================================

  // --- A. 配置并安装 AP 节点 (4x4 MIMO 高配路由器) ---
  phy.Set ("Antennas", UintegerValue (4));
  phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
  phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));
  
  mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevices = wifi.Install (phy, mac, wifiApNode);

  // --- B. 重新配置并安装 STA 节点 (2x2 MIMO 普通手机/电脑) ---
  phy.Set ("Antennas", UintegerValue (2));
  phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
  phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));
  
  mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer staDevices = wifi.Install (phy, mac, wifiStaNodes);

  // ========================================================

  // 5.1 配置 HE 参数 (GI / A-MPDU / 接收缓冲等)
  auto ConfigureWifiDevices = [&] (const NetDeviceContainer& devices) {
    for (uint32_t i = 0; i < devices.GetN (); ++i)
      {
        Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (devices.Get (i));
        if (!dev)
          {
            continue;
          }
        if (Ptr<HeConfiguration> he = dev->GetHeConfiguration ())
          {
            he->SetGuardInterval (NanoSeconds (heGuardIntervalNs));
            he->SetMpduBufferSize (heMpduBufferSize);
          }
        Ptr<WifiMac> wifiMac = dev->GetMac ();
        if (Ptr<RegularWifiMac> rmac = DynamicCast<RegularWifiMac> (wifiMac))
          {
            rmac->SetAttribute ("BE_MaxAmpduSize", UintegerValue (beMaxAmpduSize));
          }
      }
  };
  ConfigureWifiDevices (apDevices);
  ConfigureWifiDevices (staDevices);

  if (printTxVector)
    {
      Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice> (staDevices.Get (0));
      if (staDev)
        {
          staDev->GetPhy ()->TraceConnectWithoutContext ("PhyTxPsduBegin",
                                                        MakeCallback (&TraceStaTxPsdu));
        }
    }

  // 6. 移动性 (距离 1 米)
  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));  // AP
  positionAlloc->Add (Vector (1.0, 0.0, 0.0));  // STA
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (allNodes);

  // 7. 安装 DCE
  DceManagerHelper dceManager;
  // 加载 liblinux.so
  dceManager.SetNetworkStack ("ns3::LinuxSocketFdFactory", "Library", StringValue ("liblinux.so"));
  dceManager.Install (allNodes);

  LinuxStackHelper stack;
  stack.Install (allNodes);

  if (tcpSockBufMaxBytes > 0)
    {
      const std::string buf = std::to_string (tcpSockBufMaxBytes);
      stack.SysctlSet (allNodes, ".net.core.rmem_max", buf);
      stack.SysctlSet (allNodes, ".net.core.wmem_max", buf);
      stack.SysctlSet (allNodes, ".net.ipv4.tcp_rmem", "4096 87380 " + buf);
      stack.SysctlSet (allNodes, ".net.ipv4.tcp_wmem", "4096 87380 " + buf);
    }

  // 8. IP 分配
  Ipv4AddressHelper address;
  address.SetBase ("192.168.1.0", "255.255.255.0");
  address.Assign (apDevices);
  address.Assign (staDevices);

  // 9. 配置 Iperf
  DceApplicationHelper dce;
  dce.SetStackSize (1 << 20);

  // Server (AP) - 启动时间 1.0s
  dce.SetBinary ("iperf");
  dce.ResetArguments ();
  dce.ResetEnvironment ();
  dce.AddArgument ("-s");
  dce.AddArgument ("-i"); dce.AddArgument ("1");
  dce.AddArgument ("-w"); dce.AddArgument ("8M"); 
  ApplicationContainer serverApp = dce.Install (wifiApNode.Get (0));
  serverApp.Start (Seconds (1.0));

  // Client (STA) - 启动时间 2.0s
  dce.SetBinary ("iperf");
  dce.ResetArguments ();
  dce.ResetEnvironment ();
  dce.AddArgument ("-c");
  dce.AddArgument ("192.168.1.1");
  dce.AddArgument ("-i"); dce.AddArgument ("1");
  dce.AddArgument ("-t"); dce.AddArgument (std::to_string (iperfTimeSec));
  dce.AddArgument ("-w"); dce.AddArgument ("8M");
  
  // 建议：如果你想看到极高的吞吐量，可以取消下面这行的注释开启多线程 TCP
  if (iperfParallelFlows > 1)
    {
      dce.AddArgument ("-P"); dce.AddArgument (std::to_string (iperfParallelFlows));
    }

  ApplicationContainer clientApp = dce.Install (wifiStaNodes.Get (0));
  clientApp.Start (Seconds (2.0));

  // 10. 运行
  Simulator::Stop (Seconds (std::max (5.0, static_cast<double> (iperfTimeSec) + 5.0)));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
