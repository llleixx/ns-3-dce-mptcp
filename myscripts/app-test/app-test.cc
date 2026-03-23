#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
// 引入你刚编写的 Helper
#include "rate-dual-helper.h" 

using namespace ns3;

int main (int argc, char *argv[])
{
  Time::SetResolution (Time::NS);
  
  // 1. 创建极其简单的拓扑：Node 0 (LLRF 发送端) ---- Node 1 (Archiver 接收端)
  NodeContainer nodes;
  nodes.Create (2);

  PointToPointHelper pointToPoint;
  pointToPoint.SetDeviceAttribute ("DataRate", StringValue ("1Gbps"));
  pointToPoint.SetChannelAttribute ("Delay", StringValue ("2ms"));
  NetDeviceContainer devices = pointToPoint.Install (nodes);

  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer interfaces = address.Assign (devices);

  uint16_t port = 9;

  // 2. 接收端设置 (使用标准的 PacketSink)
  Address sinkLocalAddress (InetSocketAddress (Ipv4Address::GetAny (), port));
  PacketSinkHelper packetSinkHelper ("ns3::TcpSocketFactory", sinkLocalAddress);
  ApplicationContainer sinkApps = packetSinkHelper.Install (nodes.Get (1));
  sinkApps.Start (Seconds (0.0));
  sinkApps.Stop (Seconds (20.0));

  // 3. 发送端设置：使用你编写的 RateDualHelper
  Address destAddress (InetSocketAddress (interfaces.GetAddress (1), port));
  RateDualHelper llrfTraffic ("ns3::TcpSocketFactory", destAddress);

  // 设置属性（完全匹配 LLRF 的调研文档数据）
  llrfTraffic.SetAttribute ("AvgRate", DataRateValue (DataRate ("3.9Mbps")));
  llrfTraffic.SetAttribute ("PeakRate", DataRateValue (DataRate ("92.79Mbps")));
  llrfTraffic.SetAttribute ("BurstProb", DoubleValue (0.5));
  llrfTraffic.SetAttribute ("PacketSize", UintegerValue (1460)); // Ethernet MTU payload

  // 批量安装到源节点
  ApplicationContainer clientApps = llrfTraffic.Install (nodes.Get (0));
  clientApps.Start (Seconds (1.0));
  clientApps.Stop (Seconds (20.0));

  // 运行仿真
  Simulator::Stop (Seconds (20.0));
  Simulator::Run ();

  Ptr<PacketSink> sinkPtr = DynamicCast<PacketSink> (sinkApps.Get (0));
  uint32_t totalBytes = sinkPtr->GetTotalRx ();
  // 传输时间：20.0s (Stop) - 5.0s (Client Start) = 15.0s
  double throughput = (totalBytes * 8.0) / (19.0) / 1000000.0;

  std::cout << throughput << std::endl;

  Simulator::Destroy ();

  return 0;
}
