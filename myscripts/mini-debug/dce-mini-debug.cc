#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-standards.h"
#include "ns3/dce-module.h"
#include "ns3/nr-module.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/antenna-module.h"
#include "ns3/nr-mac-scheduler-ofdma-rr.h"

#include <sstream>
#include <string>

#include "../../model/linux/ipv4-linux.h"
#include "../app-test/rate-dual-helper.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DceMiniDebug");

namespace
{

std::string
ToString (Ipv4Address address)
{
  std::ostringstream oss;
  address.Print (oss);
  return oss.str ();
}

std::string
GetIfName (Ptr<Node> node, Ipv4Address address)
{
  Ptr<Ipv4Linux> ipv4 = node->GetObject<Ipv4Linux> ();
  NS_ABORT_MSG_IF (ipv4 == 0, "Ipv4Linux is not installed");
  const int32_t interfaceIndex = ipv4->GetInterfaceForAddress (address);
  NS_ABORT_MSG_IF (interfaceIndex < 0, "Interface not found for address");
  std::ostringstream oss;
  oss << "sim" << interfaceIndex;
  return oss.str ();
}

void
SetPosition (Ptr<Node> node, const Vector &position)
{
  Ptr<ConstantPositionMobilityModel> mobility =
      node->GetObject<ConstantPositionMobilityModel> ();
  if (mobility == 0)
    {
      mobility = CreateObject<ConstantPositionMobilityModel> ();
      node->AggregateObject (mobility);
    }
  mobility->SetPosition (position);
}

void
RunIpAt (Ptr<Node> node, double whenSeconds, const std::string &command)
{
  LinuxStackHelper::RunIp (node, Seconds (whenSeconds), command);
}

void
ConfigureMptcp (LinuxStackHelper &stack, NodeContainer nodes, bool enableMptcp)
{
  if (!enableMptcp)
    {
      return;
    }

  stack.SysctlSet (nodes, ".net.mptcp.mptcp_enabled", "1");
  stack.SysctlSet (nodes, ".net.mptcp.mptcp_path_manager", "fullmesh");
  stack.SysctlSet (nodes, ".net.ipv4.tcp_congestion_control", "olia");
}

} // namespace

int
main (int argc, char *argv[])
{
  std::string scenario = "p2p";
  std::string appType = "onoff";
  bool enableMptcp = false;
  bool debugRoutes = true;
  double simTime = 8.0;
  double sinkStart = 1.0;
  double clientStart = 2.0;
  uint16_t port = 5000;

  CommandLine cmd;
  cmd.AddValue ("scenario", "p2p, wifi, nr, or dual", scenario);
  cmd.AddValue ("appType", "onoff or rate-dual", appType);
  cmd.AddValue ("enableMptcp", "Enable MPTCP on client/server", enableMptcp);
  cmd.AddValue ("debugRoutes", "Dump route info via DCE ip", debugRoutes);
  cmd.AddValue ("simTime", "Simulation stop time in seconds", simTime);
  cmd.AddValue ("sinkStart", "PacketSink start time", sinkStart);
  cmd.AddValue ("clientStart", "Client app start time", clientStart);
  cmd.AddValue ("port", "TCP port", port);
  cmd.Parse (argc, argv);

  const bool useWifi = (scenario == "wifi" || scenario == "dual");
  const bool useNr = (scenario == "nr" || scenario == "dual");
  const bool useP2p = (scenario == "p2p");

  NS_ABORT_MSG_IF (!(useP2p || useWifi || useNr),
                   "scenario must be p2p, wifi, nr, or dual");
  NS_ABORT_MSG_IF (appType != "onoff" && appType != "rate-dual",
                   "appType must be onoff or rate-dual");

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (999999999));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (999999999));
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (160));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (0)));

  NodeContainer clientNode;
  NodeContainer serverNode;
  NodeContainer apNode;
  NodeContainer gnbNode;

  clientNode.Create (1);
  serverNode.Create (1);
  if (useWifi)
    {
      apNode.Create (1);
    }
  if (useNr)
    {
      gnbNode.Create (1);
    }

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (clientNode);
  mobility.Install (serverNode);
  mobility.Install (apNode);
  mobility.Install (gnbNode);

  SetPosition (clientNode.Get (0), Vector (0.0, 0.0, 0.0));
  SetPosition (serverNode.Get (0), Vector (15.0, 0.0, 0.0));
  if (useWifi)
    {
      SetPosition (apNode.Get (0), Vector (2.0, 0.0, 5.0));
    }
  if (useNr)
    {
      SetPosition (gnbNode.Get (0), Vector (3.0, 0.0, 5.0));
    }

  NodeContainer linuxNodes;
  linuxNodes.Add (clientNode);
  linuxNodes.Add (serverNode);
  linuxNodes.Add (apNode);

  DceManagerHelper dceManager;
  dceManager.SetTaskManagerAttribute ("FiberManagerType",
                                      StringValue ("UcontextFiberManager"));
  dceManager.SetNetworkStack ("ns3::LinuxSocketFdFactory",
                              "Library", StringValue ("liblinux.so"));

  LinuxStackHelper stack;
  stack.Install (linuxNodes);
  dceManager.Install (linuxNodes);

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  p2p.SetChannelAttribute ("Delay", StringValue ("0ms"));
  p2p.SetDeviceAttribute ("Mtu", UintegerValue (2500));

  Ipv4Address serverListenAddress ("0.0.0.0");

  if (useP2p)
    {
      NetDeviceContainer devices = p2p.Install (clientNode.Get (0), serverNode.Get (0));
      Ipv4AddressHelper address;
      address.SetBase ("10.1.0.0", "255.255.255.0");
      Ipv4InterfaceContainer ifaces = address.Assign (devices);

      const std::string clientIf = GetIfName (clientNode.Get (0), ifaces.GetAddress (0));
      const std::string serverIf = GetIfName (serverNode.Get (0), ifaces.GetAddress (1));
      RunIpAt (clientNode.Get (0), 0.2,
               "route add default via " + ToString (ifaces.GetAddress (1)) +
                   " dev " + clientIf);
      RunIpAt (serverNode.Get (0), 0.2,
               "route add default via " + ToString (ifaces.GetAddress (0)) +
                   " dev " + serverIf);
      serverListenAddress = ifaces.GetAddress (1);
    }

  if (useWifi)
    {
      WifiHelper wifi;
      wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
      wifi.SetRemoteStationManager ("ns3::IdealWifiManager");
      WifiMacHelper mac;

      YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
      YansWifiPhyHelper phy;
      phy.SetChannel (channel.Create ());
      phy.Set ("ChannelWidth", UintegerValue (160));
      phy.Set ("Frequency", UintegerValue (5250));
      phy.Set ("TxPowerStart", DoubleValue (23.0));
      phy.Set ("TxPowerEnd", DoubleValue (23.0));

      Ssid ssid ("mini-debug-wifi");

      phy.Set ("Antennas", UintegerValue (4));
      phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
      phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));
      mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
      NetDeviceContainer apWifiDev = wifi.Install (phy, mac, apNode.Get (0));

      phy.Set ("Antennas", UintegerValue (2));
      phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
      phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));
      mac.SetType ("ns3::StaWifiMac",
                   "Ssid", SsidValue (ssid),
                   "ActiveProbing", BooleanValue (false));
      NetDeviceContainer clientWifiDev = wifi.Install (phy, mac, clientNode.Get (0));

      NetDeviceContainer backhaul = p2p.Install (apNode.Get (0), serverNode.Get (0));

      Ipv4AddressHelper wifiAddress;
      wifiAddress.SetBase ("10.10.0.0", "255.255.255.0");
      Ipv4InterfaceContainer apWifiIf = wifiAddress.Assign (apWifiDev);
      Ipv4InterfaceContainer clientWifiIf = wifiAddress.Assign (clientWifiDev);

      Ipv4AddressHelper backhaulAddress;
      backhaulAddress.SetBase ("10.20.0.0", "255.255.255.252");
      Ipv4InterfaceContainer backhaulIf = backhaulAddress.Assign (backhaul);

      const std::string clientWifiIfName =
          GetIfName (clientNode.Get (0), clientWifiIf.GetAddress (0));
      const std::string apWifiIfName =
          GetIfName (apNode.Get (0), apWifiIf.GetAddress (0));
      const std::string apBackhaulIfName =
          GetIfName (apNode.Get (0), backhaulIf.GetAddress (0));
      const std::string serverWifiIfName =
          GetIfName (serverNode.Get (0), backhaulIf.GetAddress (1));

      RunIpAt (clientNode.Get (0), 0.25,
               "route add default via " + ToString (apWifiIf.GetAddress (0)) +
                   " dev " + clientWifiIfName);
      RunIpAt (apNode.Get (0), 0.25,
               "route add default via " + ToString (backhaulIf.GetAddress (1)) +
                   " dev " + apBackhaulIfName);
      RunIpAt (serverNode.Get (0), 0.25,
               "route add 10.10.0.0/24 via " + ToString (backhaulIf.GetAddress (0)) +
                   " dev " + serverWifiIfName);
      RunIpAt (serverNode.Get (0), 0.26,
               "route add default via " + ToString (backhaulIf.GetAddress (0)) +
                   " dev " + serverWifiIfName);
      RunIpAt (apNode.Get (0), 0.26,
               "route add 10.10.0.0/24 dev " + apWifiIfName);

      if (!useNr)
        {
          serverListenAddress = backhaulIf.GetAddress (1);
        }
    }

  if (useNr)
    {
      Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
      Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper> ();
      Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();

      nrHelper->SetEpcHelper (epcHelper);
      nrHelper->SetBeamformingHelper (beamformingHelper);

      CcBwpCreator ccBwpCreator;
      CcBwpCreator::SimpleOperationBandConf bandConf (4.9e9,
                                                      100e6,
                                                      1,
                                                      BandwidthPartInfo::UMi_StreetCanyon_LoS);
      OperationBandInfo band =
          ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
      nrHelper->InitializeOperationBand (&band);
      BandwidthPartInfoPtrVector allBwps =
          CcBwpCreator::GetAllBwps ({band});

      nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));
      nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaRR::GetTypeId ());
      beamformingHelper->SetAttribute (
          "BeamformingMethod",
          TypeIdValue (DirectPathBeamforming::GetTypeId ()));
      epcHelper->SetAttribute ("S1uLinkDelay", TimeValue (MilliSeconds (0)));

      nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
      nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (1));
      nrHelper->SetUeAntennaAttribute (
          "AntennaElement",
          PointerValue (CreateObject<ThreeGppAntennaModel> ()));
      nrHelper->SetGnbAntennaAttribute ("NumRows", UintegerValue (2));
      nrHelper->SetGnbAntennaAttribute ("NumColumns", UintegerValue (2));
      nrHelper->SetGnbAntennaAttribute (
          "AntennaElement",
          PointerValue (CreateObject<ThreeGppAntennaModel> ()));
      nrHelper->SetGnbPhyAttribute ("TxPower", DoubleValue (30.0));
      nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (23.0));

      NetDeviceContainer gnbDev = nrHelper->InstallGnbDevice (gnbNode, allBwps);
      NetDeviceContainer ueDev = nrHelper->InstallUeDevice (clientNode, allBwps);
      nrHelper->GetGnbPhy (gnbDev.Get (0), 0)->SetAttribute ("Numerology",
                                                             UintegerValue (1));
      DynamicCast<NrGnbNetDevice> (gnbDev.Get (0))->UpdateConfig ();
      DynamicCast<NrUeNetDevice> (ueDev.Get (0))->UpdateConfig ();

      Ptr<Node> pgw = epcHelper->GetPgwNode ();
      NetDeviceContainer backhaul = p2p.Install (pgw, serverNode.Get (0));

      Ipv4AddressHelper backhaulAddress;
      backhaulAddress.SetBase ("1.0.0.0", "255.255.255.0");
      Ipv4InterfaceContainer backhaulIf = backhaulAddress.Assign (backhaul);

      Ipv4InterfaceContainer ueIf = epcHelper->AssignUeIpv4Address (ueDev);
      nrHelper->AttachToClosestEnb (ueDev, gnbDev);

      const std::string clientNrIfName =
          GetIfName (clientNode.Get (0), ueIf.GetAddress (0));
      const std::string serverNrIfName =
          GetIfName (serverNode.Get (0), backhaulIf.GetAddress (1));
      const Ipv4Address ueGateway = epcHelper->GetUeDefaultGatewayAddress ();

      RunIpAt (clientNode.Get (0), 0.25,
               "route add default via " + ToString (ueGateway) +
                   " dev " + clientNrIfName);
      RunIpAt (serverNode.Get (0), 0.25,
               "route add 7.0.0.0/8 via " + ToString (backhaulIf.GetAddress (0)) +
                   " dev " + serverNrIfName);
      if (!useWifi)
        {
          RunIpAt (serverNode.Get (0), 0.26,
                   "route add default via " + ToString (backhaulIf.GetAddress (0)) +
                       " dev " + serverNrIfName);
          serverListenAddress = backhaulIf.GetAddress (1);
        }
      else
        {
        const std::string clientWifiIfName = GetIfName (clientNode.Get (0),
                                                        Ipv4Address ("10.10.0.2"));
        RunIpAt (clientNode.Get (0), 0.27,
                 "rule add from " + ToString (ueIf.GetAddress (0)) + " table 1");
        RunIpAt (clientNode.Get (0), 0.28,
                 "route add 7.0.0.0/8 dev " + clientNrIfName + " scope link table 1");
        RunIpAt (clientNode.Get (0), 0.29,
                 "route add default via " + ToString (ueGateway) +
                     " dev " + clientNrIfName + " table 1");
        RunIpAt (clientNode.Get (0), 0.30,
                 "rule add from 10.10.0.2 table 2");
        RunIpAt (clientNode.Get (0), 0.31,
                 "route add 10.10.0.0/24 dev " + clientWifiIfName + " scope link table 2");
        RunIpAt (clientNode.Get (0), 0.32,
                 "route add default via 10.10.0.1 dev " + clientWifiIfName + " table 2");

        const std::string serverWifiIfName = GetIfName (serverNode.Get (0),
                                                        Ipv4Address ("10.20.0.2"));
        RunIpAt (serverNode.Get (0), 0.30,
                 "rule add from " + ToString (backhaulIf.GetAddress (1)) + " table 1");
        RunIpAt (serverNode.Get (0), 0.31,
                 "route add 1.0.0.0/24 dev " + serverNrIfName + " scope link table 1");
        RunIpAt (serverNode.Get (0), 0.32,
                 "route add 7.0.0.0/8 via " + ToString (backhaulIf.GetAddress (0)) +
                     " dev " + serverNrIfName + " table 1");
        RunIpAt (serverNode.Get (0), 0.33,
                 "route add 10.10.0.0/24 via 10.20.0.1 dev " + serverWifiIfName + " table 1");
        RunIpAt (serverNode.Get (0), 0.34,
                 "route add default via " + ToString (backhaulIf.GetAddress (0)) +
                     " dev " + serverNrIfName + " table 1");
        RunIpAt (serverNode.Get (0), 0.35,
                 "rule add from 10.20.0.2 table 2");
        RunIpAt (serverNode.Get (0), 0.36,
                 "route add 10.20.0.0/30 dev " + serverWifiIfName + " scope link table 2");
        RunIpAt (serverNode.Get (0), 0.37,
                 "route add 10.10.0.0/24 via 10.20.0.1 dev " + serverWifiIfName + " table 2");
        RunIpAt (serverNode.Get (0), 0.38,
                 "route add 7.0.0.0/8 via " + ToString (backhaulIf.GetAddress (0)) +
                     " dev " + serverNrIfName + " table 2");
        RunIpAt (serverNode.Get (0), 0.39,
                 "route add default via 10.20.0.1 dev " + serverWifiIfName + " table 2");
        }

      serverListenAddress = backhaulIf.GetAddress (1);
    }

  stack.SysctlSet (linuxNodes, ".net.core.wmem_default", "16777216");
  stack.SysctlSet (linuxNodes, ".net.core.wmem_max", "16777216");
  stack.SysctlSet (linuxNodes, ".net.core.rmem_default", "16777216");
  stack.SysctlSet (linuxNodes, ".net.core.rmem_max", "16777216");
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_rmem", "4096 87380 16777216");
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_wmem", "4096 65536 16777216");
  stack.SysctlSet (linuxNodes, ".net.ipv4.conf.default.rp_filter", "0");
  stack.SysctlSet (linuxNodes, ".net.ipv4.conf.all.rp_filter", "0");
  if (useWifi)
    {
      stack.SysctlSet (apNode, ".net.ipv4.conf.default.forwarding", "1");
    }
  ConfigureMptcp (stack, NodeContainer (clientNode, serverNode), enableMptcp);

  PacketSinkHelper sinkHelper (
      "ns3::LinuxTcpSocketFactory",
      InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sinkHelper.Install (serverNode.Get (0));
  sinkApps.Start (Seconds (sinkStart));
  sinkApps.Stop (Seconds (simTime));

  if (appType == "onoff")
    {
      OnOffHelper onoff ("ns3::LinuxTcpSocketFactory",
                         InetSocketAddress (serverListenAddress, port));
      onoff.SetAttribute ("OnTime",
                          StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
      onoff.SetAttribute ("OffTime",
                          StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      onoff.SetAttribute ("PacketSize", UintegerValue (1448));
      onoff.SetAttribute ("DataRate", StringValue ("20Mbps"));
      ApplicationContainer clientApps = onoff.Install (clientNode.Get (0));
      clientApps.Start (Seconds (clientStart));
      clientApps.Stop (Seconds (simTime));
    }
  else
    {
      RateDualHelper helper (
          "ns3::LinuxTcpSocketFactory",
          InetSocketAddress (serverListenAddress, port));
      ApplicationContainer clientApps = helper.Install (clientNode.Get (0));
      clientApps.Start (Seconds (clientStart));
      clientApps.Stop (Seconds (simTime));
    }

  if (debugRoutes)
    {
      RunIpAt (clientNode.Get (0), clientStart - 0.3, "addr list");
      RunIpAt (clientNode.Get (0), clientStart - 0.2, "route show table all");
      RunIpAt (clientNode.Get (0), clientStart - 0.1,
               "route get " + ToString (serverListenAddress));
      RunIpAt (serverNode.Get (0), clientStart - 0.3, "addr list");
      RunIpAt (serverNode.Get (0), clientStart - 0.2, "route show table all");
      if (useNr)
        {
          RunIpAt (serverNode.Get (0), clientStart - 0.1, "route get 7.0.0.2");
        }
      else if (useWifi)
        {
          RunIpAt (serverNode.Get (0), clientStart - 0.1, "route get 10.10.0.2");
        }
      else
        {
          RunIpAt (serverNode.Get (0), clientStart - 0.1, "route get 10.1.0.1");
        }
    }

  Simulator::Stop (Seconds (simTime + 0.2));
  Simulator::Run ();

  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  std::cout << "[mini-debug] scenario=" << scenario
            << " app=" << appType
            << " mptcp=" << enableMptcp
            << " sink=" << sink->GetTotalRx () << std::endl;

  Simulator::Destroy ();
  return 0;
}
