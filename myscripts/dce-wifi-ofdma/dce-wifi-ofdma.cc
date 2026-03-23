/*
 * dce-wifi-ofdma.cc
 *
 * Based on myscripts/dce-wifi/dce-wifi.cc, but extended to:
 * 1. support multiple associated STA clients
 * 2. let only a configurable subset run iperf
 * 3. enable both DL and UL OFDMA on 802.11ax
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-standards.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/dce-module.h"
#include "ns3/spectrum-module.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DceWifiIperfAxOfdma");

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

int
main (int argc, char *argv[])
{
  bool printTxVector = false;
  uint16_t channelWidthMhz = 160;
  uint16_t frequencyMhz = 5250;
  uint16_t heGuardIntervalNs = 800;
  uint16_t heMpduBufferSize = 256;
  uint32_t beMaxAmpduSize = 6500631;
  uint32_t tcpSockBufMaxBytes = 16777216;
  uint32_t iperfTimeSec = 20;
  uint32_t iperfParallelFlows = 1;
  uint32_t clientCount = 1;
  uint32_t activeClientCount = 1;
  uint16_t iperfBasePort = 5000;
  double serverStartTimeSec = 1.0;
  double clientStartTimeSec = 3.0;
  double clientStartSpacingSec = 0.1;

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
  cmd.AddValue ("clientCount", "Total number of Wi-Fi STA clients to associate", clientCount);
  cmd.AddValue ("activeClientCount", "Number of STAs that actually run iperf (the others only associate)", activeClientCount);
  cmd.AddValue ("iperfBasePort", "Starting TCP port used by AP-side iperf servers", iperfBasePort);
  cmd.AddValue ("serverStartTimeSec", "AP-side iperf server start time in seconds", serverStartTimeSec);
  cmd.AddValue ("clientStartTimeSec", "First STA iperf client start time in seconds", clientStartTimeSec);
  cmd.AddValue ("clientStartSpacingSec", "Additional delay between consecutive STA client starts in seconds", clientStartSpacingSec);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (clientCount == 0, "clientCount must be at least 1");
  NS_ABORT_MSG_IF (activeClientCount > clientCount,
                   "activeClientCount must be less than or equal to clientCount");
  NS_ABORT_MSG_IF (activeClientCount > 0
                   && static_cast<uint32_t> (iperfBasePort) + activeClientCount - 1 > 65535,
                   "iperfBasePort + activeClientCount - 1 must not exceed 65535");

  const uint8_t muSchedulerStations = static_cast<uint8_t> (std::min<uint32_t> (clientCount, 74));
  if (clientCount > muSchedulerStations)
    {
      std::cout << "[config] clientCount=" << clientCount
                << ", but RrMultiUserScheduler::NStations is capped at "
                << static_cast<uint32_t> (muSchedulerStations) << std::endl;
    }

  std::cout << "[config] totalStaClients=" << clientCount
            << " activeIperfClients=" << activeClientCount
            << " channelWidth=" << channelWidthMhz << "MHz"
            << " frequency=" << frequencyMhz << "MHz"
            << " ulOfdma=on dlOfdma=on" << std::endl;

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold",
                      UintegerValue (4294967295u));

  NodeContainer wifiStaNodes;
  wifiStaNodes.Create (clientCount);
  NodeContainer wifiApNode;
  wifiApNode.Create (1);

  NodeContainer allNodes;
  allNodes.Add (wifiApNode);
  allNodes.Add (wifiStaNodes);

  Ptr<MultiModelSpectrumChannel> spectrumChannel = CreateObject<MultiModelSpectrumChannel> ();
  SpectrumWifiPhyHelper phy;
  phy.SetChannel (spectrumChannel);
  phy.Set ("ChannelWidth", UintegerValue (channelWidthMhz));
  phy.Set ("Frequency", UintegerValue (frequencyMhz));
  phy.Set ("TxPowerStart", DoubleValue (23));
  phy.Set ("TxPowerEnd", DoubleValue (23));

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");

  WifiMacHelper mac;
  Ssid ssid = Ssid ("ns-3-wifi6-router-ofdma");

  phy.Set ("Antennas", UintegerValue (4));
  phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
  phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));

  mac.SetMultiUserScheduler ("ns3::RrMultiUserScheduler",
                             "NStations", UintegerValue (muSchedulerStations),
                             "EnableUlOfdma", BooleanValue (true),
                             "EnableBsrp", BooleanValue (true));
  mac.SetType ("ns3::ApWifiMac",
               "EnableBeaconJitter", BooleanValue (false),
               "Ssid", SsidValue (ssid));
  NetDeviceContainer apDevices = wifi.Install (phy, mac, wifiApNode);

  phy.Set ("Antennas", UintegerValue (2));
  phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
  phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));

  mac.SetType ("ns3::StaWifiMac", "Ssid", SsidValue (ssid));
  NetDeviceContainer staDevices = wifi.Install (phy, mac, wifiStaNodes);

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

  if (printTxVector && staDevices.GetN () > 0)
    {
      Ptr<WifiNetDevice> staDev = DynamicCast<WifiNetDevice> (staDevices.Get (0));
      if (staDev)
        {
          staDev->GetPhy ()->TraceConnectWithoutContext ("PhyTxPsduBegin",
                                                        MakeCallback (&TraceStaTxPsdu));
        }
    }

  MobilityHelper mobility;
  Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
  positionAlloc->Add (Vector (0.0, 0.0, 0.0));
  const double radiusMeters = 1.0;
  const double twoPi = 6.28318530717958647692;
  for (uint32_t i = 0; i < clientCount; ++i)
    {
      const double angle = twoPi * static_cast<double> (i) / static_cast<double> (clientCount);
      positionAlloc->Add (Vector (radiusMeters * std::cos (angle),
                                  radiusMeters * std::sin (angle),
                                  0.0));
    }
  mobility.SetPositionAllocator (positionAlloc);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (allNodes);

  DceManagerHelper dceManager;
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

  Ipv4AddressHelper address;
  address.SetBase ("192.168.1.0", "255.255.255.0");
  Ipv4InterfaceContainer apInterfaces = address.Assign (apDevices);
  Ipv4InterfaceContainer staInterfaces = address.Assign (staDevices);
  std::ostringstream apIpStream;
  apInterfaces.GetAddress (0).Print (apIpStream);
  const std::string apIp = apIpStream.str ();

  DceApplicationHelper dce;
  dce.SetStackSize (1 << 20);

  for (uint32_t i = 0; i < activeClientCount; ++i)
    {
      const uint16_t port = iperfBasePort + i;

      dce.SetBinary ("iperf");
      dce.ResetArguments ();
      dce.ResetEnvironment ();
      dce.AddArgument ("-s");
      dce.AddArgument ("-p");
      dce.AddArgument (std::to_string (port));
      dce.AddArgument ("-i");
      dce.AddArgument ("1");
      dce.AddArgument ("-w");
      dce.AddArgument ("8M");
      ApplicationContainer serverApp = dce.Install (wifiApNode.Get (0));
      serverApp.Start (Seconds (serverStartTimeSec));

      dce.SetBinary ("iperf");
      dce.ResetArguments ();
      dce.ResetEnvironment ();
      dce.AddArgument ("-c");
      dce.AddArgument (apIp);
      dce.AddArgument ("-p");
      dce.AddArgument (std::to_string (port));
      dce.AddArgument ("-i");
      dce.AddArgument ("1");
      dce.AddArgument ("-t");
      dce.AddArgument (std::to_string (iperfTimeSec));
      dce.AddArgument ("-w");
      dce.AddArgument ("8M");

      if (iperfParallelFlows > 1)
        {
          dce.AddArgument ("-P");
          dce.AddArgument (std::to_string (iperfParallelFlows));
        }

      ApplicationContainer clientApp = dce.Install (wifiStaNodes.Get (i));
      clientApp.Start (Seconds (clientStartTimeSec + clientStartSpacingSec * i));
    }

  const double lastClientStartSec = (activeClientCount > 0)
                                      ? (clientStartTimeSec
                                         + clientStartSpacingSec * static_cast<double> (activeClientCount - 1))
                                      : 0.0;
  Simulator::Stop (Seconds (std::max (5.0, lastClientStartSec + static_cast<double> (iperfTimeSec) + 5.0)));
  Simulator::Run ();
  Simulator::Destroy ();

  return 0;
}
