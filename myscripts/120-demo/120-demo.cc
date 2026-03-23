/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-standards.h"

// NR / EPC
#include "ns3/nr-module.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/antenna-module.h"
#include "ns3/nr-mac-scheduler-ofdma-rr.h"
#include "ns3/lte-ue-rrc.h"

// DCE
#include "ns3/dce-module.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#include "../../model/linux/ipv4-linux.h"
#include "../app-test/rate-dual-helper.h"
#include "../app-test/rate-dual-application.h"

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("DceNrWifiMptcp120Demo");

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
PrintUeRrcSummary (NetDeviceContainer ueDevs, const std::string &tag)
{
  uint32_t connectedNormally = 0;
  uint32_t idleOrConnecting = 0;
  uint32_t randomAccess = 0;
  uint32_t other = 0;

  for (uint32_t i = 0; i < ueDevs.GetN (); ++i)
    {
      Ptr<NrUeNetDevice> ue = DynamicCast<NrUeNetDevice> (ueDevs.Get (i));
      if (ue == 0)
        {
          ++other;
          continue;
        }
      Ptr<LteUeRrc> rrc = ue->GetRrc ();
      if (rrc == 0)
        {
          ++other;
          continue;
        }

      const LteUeRrc::State state = rrc->GetState ();
      if (state == LteUeRrc::CONNECTED_NORMALLY)
        {
          ++connectedNormally;
        }
      else if (state == LteUeRrc::IDLE_RANDOM_ACCESS)
        {
          ++randomAccess;
        }
      else if (state == LteUeRrc::IDLE_CONNECTING)
        {
          ++idleOrConnecting;
        }
      else
        {
          ++other;
        }
    }

  std::cout << "[120-demo] UE RRC summary (" << tag << " @"
            << Simulator::Now ().GetSeconds () << "s)"
            << " connected=" << connectedNormally << "/" << ueDevs.GetN ()
            << " randomAccess=" << randomAccess
            << " idleConnecting=" << idleOrConnecting
            << " other=" << other << std::endl;
}

void
ConfigureMptcp (LinuxStackHelper &stack,
                NodeContainer nodes,
                bool enableMptcp,
                bool enableMptcpDebug)
{
  if (!enableMptcp)
    {
      return;
    }

  stack.SysctlSet (nodes, ".net.mptcp.mptcp_enabled", "1");
  stack.SysctlSet (nodes, ".net.mptcp.mptcp_path_manager", "fullmesh");
  stack.SysctlSet (nodes, ".net.ipv4.tcp_congestion_control", "olia");
  if (enableMptcpDebug)
    {
      stack.SysctlSet (nodes, ".net.mptcp.mptcp_debug", "1");
    }
}

struct WifiClientConfig
{
  Ipv4Address addr;
  Ipv4Address gateway;
  std::string subnetCidr;
};

} // namespace

int
main (int argc, char *argv[])
{
  uint32_t numClients = 120;
  double simTime = 20.0;
  double sinkStart = 1.0;
  double clientStart = 2.0;
  double clientStartJitter = 0.002;
  int64_t clientStartJitterStream = 1;
  uint16_t port = 5000;
  bool enableMptcp = true;
  bool enableMptcpDebug = false;
  bool disableIpv6 = true;
  bool debugRoutes = false;

  CommandLine cmd;
  cmd.AddValue ("numClients", "Number of client UEs (default 120)", numClients);
  cmd.AddValue ("simTime", "Simulation stop time (s)", simTime);
  cmd.AddValue ("sinkStart", "PacketSink start time (s)", sinkStart);
  cmd.AddValue ("clientStart", "Client app start time (s)", clientStart);
  cmd.AddValue ("clientStartJitter",
                "Randomly jitter each client start time in [0, jitter] seconds",
                clientStartJitter);
  cmd.AddValue ("clientStartJitterStream",
                "RNG stream index for clientStartJitter (for reproducibility)",
                clientStartJitterStream);
  cmd.AddValue ("port", "TCP port", port);
  cmd.AddValue ("enableMptcp", "Enable MPTCP fullmesh on client/server", enableMptcp);
  cmd.AddValue ("enableMptcpDebug", "Enable MPTCP debug sysctl (very verbose)", enableMptcpDebug);
  cmd.AddValue ("disableIpv6", "Disable IPv6 via sysctl (reduces multicast control traffic)", disableIpv6);
  cmd.AddValue ("debugRoutes", "Dump route info for a few nodes", debugRoutes);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF ((numClients % 3) != 0,
                   "numClients must be a multiple of 3 for the 3-column layout");

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize", UintegerValue (999999999));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize", UintegerValue (999999999));
  // Support many UEs (default 40 is too small for 120 UEs).
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (0)));

  // ---------------- Nodes ----------------
  NodeContainer ueNodes;
  ueNodes.Create (numClients);

  NodeContainer gnbNode;
  gnbNode.Create (1);

  NodeContainer apNodes;
  apNodes.Create (2);

  NodeContainer serverNode;
  serverNode.Create (1);
  Ptr<Node> server = serverNode.Get (0);

  // ---------------- Mobility / placement ----------------
  const uint32_t columns = 3;
  const uint32_t rowsPerColumn = numClients / columns;
  const double colSpacing = 2.8; // meters
  const double rowSpacing = 0.7; // meters
  const double clientZ = 0.0;
  const double infraZ = 5.0;

  const double gridWidth = (columns - 1) * colSpacing;
  const double gridHeight = (rowsPerColumn - 1) * rowSpacing;
  const double centerX = gridWidth / 2.0;
  const double centerY = gridHeight / 2.0;

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (ueNodes);
  mobility.Install (gnbNode);
  mobility.Install (apNodes);
  mobility.Install (serverNode);

  // 120 clients: 3 columns x 40 rows.
  // Layout: x = 0, 2.8, 5.6; y increases with 0.7m step.
  uint32_t idx = 0;
  for (uint32_t row = 0; row < rowsPerColumn; ++row)
    {
      for (uint32_t col = 0; col < columns; ++col)
        {
          SetPosition (ueNodes.Get (idx),
                       Vector (col * colSpacing, row * rowSpacing, clientZ));
          ++idx;
        }
    }

  // Place gNB/APs at 5m above the plane (choose near the grid center).
  SetPosition (gnbNode.Get (0), Vector (centerX, centerY, infraZ));
  SetPosition (apNodes.Get (0), Vector (0.0, centerY, infraZ));
  SetPosition (apNodes.Get (1), Vector (gridWidth, centerY, infraZ));
  SetPosition (server, Vector (centerX, centerY + 30.0, 0.0));

  // ---------------- DCE / Linux stack ----------------
  NodeContainer linuxNodes;
  linuxNodes.Add (ueNodes);
  linuxNodes.Add (apNodes);
  linuxNodes.Add (serverNode);

  DceManagerHelper dceManager;
  dceManager.SetTaskManagerAttribute ("FiberManagerType",
                                      StringValue ("UcontextFiberManager"));
  dceManager.SetNetworkStack ("ns3::LinuxSocketFdFactory",
                              "Library", StringValue ("liblinux.so"));

  LinuxStackHelper stack;
  stack.Install (linuxNodes);
  dceManager.Install (linuxNodes);

  // Common TCP / routing-related sysctls
  stack.SysctlSet (linuxNodes, ".net.core.wmem_default", "16777216");
  stack.SysctlSet (linuxNodes, ".net.core.wmem_max", "16777216");
  stack.SysctlSet (linuxNodes, ".net.core.rmem_default", "16777216");
  stack.SysctlSet (linuxNodes, ".net.core.rmem_max", "16777216");
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_rmem", "4096 87380 16777216");
  stack.SysctlSet (linuxNodes, ".net.ipv4.tcp_wmem", "4096 65536 16777216");
  // Improve many-simultaneous-connect behavior on the server side.
  stack.SysctlSet (serverNode, ".net.core.somaxconn", "4096");
  stack.SysctlSet (serverNode, ".net.ipv4.tcp_max_syn_backlog", "4096");
  stack.SysctlSet (linuxNodes, ".net.ipv4.conf.default.rp_filter", "0");
  stack.SysctlSet (linuxNodes, ".net.ipv4.conf.all.rp_filter", "0");
  if (disableIpv6)
    {
      stack.SysctlSet (linuxNodes, ".net.ipv6.conf.all.disable_ipv6", "1");
      stack.SysctlSet (linuxNodes, ".net.ipv6.conf.default.disable_ipv6", "1");
      stack.SysctlSet (linuxNodes, ".net.ipv6.conf.lo.disable_ipv6", "1");
    }

  // APs need to forward between WiFi and wired backbone.
  stack.SysctlSet (apNodes, ".net.ipv4.ip_forward", "1");
  stack.SysctlSet (apNodes, ".net.ipv4.conf.default.forwarding", "1");

  ConfigureMptcp (stack,
                  NodeContainer (ueNodes, serverNode),
                  enableMptcp,
                  enableMptcpDebug);

  // ---------------- WiFi networks (2 APs) ----------------
  // Split clients into two groups (60/60) to associate with AP1/AP2.
  // Column 0 -> AP1 (40), column 2 -> AP2 (40), column 1 split 20/20 by row.
  NodeContainer wifiSta1;
  NodeContainer wifiSta2;
  for (uint32_t n = 0; n < ueNodes.GetN (); ++n)
    {
      const uint32_t col = n % columns;
      const uint32_t row = n / columns;
      if (col == 0)
        {
          wifiSta1.Add (ueNodes.Get (n));
        }
      else if (col == 2)
        {
          wifiSta2.Add (ueNodes.Get (n));
        }
      else
        {
          if (row < rowsPerColumn / 2)
            {
              wifiSta1.Add (ueNodes.Get (n));
            }
          else
            {
              wifiSta2.Add (ueNodes.Get (n));
            }
        }
    }
  NS_LOG_UNCOND ("[120-demo] WiFi STA split: ap1=" << wifiSta1.GetN ()
                                                   << " ap2=" << wifiSta2.GetN ());

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");

  auto installWifi = [&wifi] (Ptr<Node> ap,
                              NodeContainer stas,
                              const std::string &ssidName,
                              const std::string &subnetBase,
                              Ipv4InterfaceContainer &apIf,
                              Ipv4InterfaceContainer &staIf) {
    WifiMacHelper mac;
    Ssid ssid (ssidName);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default ();
    YansWifiPhyHelper phy;
    phy.SetChannel (channel.Create ());

    // Keep the same WiFi-6 config used by myscripts/dce-wifi/dce-wifi.cc.
    phy.Set ("ChannelWidth", UintegerValue (160));
    phy.Set ("Frequency", UintegerValue (5250));
    phy.Set ("TxPowerStart", DoubleValue (23.0));
    phy.Set ("TxPowerEnd", DoubleValue (23.0));

    // AP: 4x4 MIMO
    phy.Set ("Antennas", UintegerValue (4));
    phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
    phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));
    mac.SetType ("ns3::ApWifiMac", "Ssid", SsidValue (ssid));
    NetDeviceContainer apDev = wifi.Install (phy, mac, ap);

    // STA: 2x2 MIMO
    phy.Set ("Antennas", UintegerValue (2));
    phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
    phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));
    mac.SetType ("ns3::StaWifiMac",
                 "Ssid", SsidValue (ssid),
                 "ActiveProbing", BooleanValue (false));
    NetDeviceContainer staDev = wifi.Install (phy, mac, stas);

    Ipv4AddressHelper wifiAddr;
    wifiAddr.SetBase (subnetBase.c_str (), "255.255.255.0");
    apIf = wifiAddr.Assign (apDev);
    staIf = wifiAddr.Assign (staDev);
  };

  Ipv4InterfaceContainer ap1WifiIf, sta1WifiIf;
  Ipv4InterfaceContainer ap2WifiIf, sta2WifiIf;
  installWifi (apNodes.Get (0), wifiSta1, "wifi-ap-1", "10.10.0.0", ap1WifiIf, sta1WifiIf);
  installWifi (apNodes.Get (1), wifiSta2, "wifi-ap-2", "10.11.0.0", ap2WifiIf, sta2WifiIf);

  // ---------------- NR (5G) + EPC ----------------
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
  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
  nrHelper->InitializeOperationBand (&band);
  BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({band});

  nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));
  nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaRR::GetTypeId ());
  beamformingHelper->SetAttribute ("BeamformingMethod",
                                   TypeIdValue (DirectPathBeamforming::GetTypeId ()));

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

  NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice (gnbNode, allBwps);
  NetDeviceContainer ueDevs = nrHelper->InstallUeDevice (ueNodes, allBwps);

  nrHelper->GetGnbPhy (gnbDevs.Get (0), 0)->SetTxPower (30.0);
  nrHelper->GetGnbPhy (gnbDevs.Get (0), 0)->SetAttribute ("Numerology", UintegerValue (1));
  nrHelper->GetGnbPhy (gnbDevs.Get (0), 0)->SetAttribute ("Pattern", StringValue ("DL|F|UL|UL|UL|"));
  DynamicCast<NrGnbNetDevice> (gnbDevs.Get (0))->UpdateConfig ();
  for (auto it = ueDevs.Begin (); it != ueDevs.End (); ++it)
    {
      DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();
    }

  Ipv4InterfaceContainer ueNrIf = epcHelper->AssignUeIpv4Address (ueDevs);
  const Ipv4Address ueGateway = epcHelper->GetUeDefaultGatewayAddress ();
  nrHelper->AttachToClosestEnb (ueDevs, gnbDevs);

  // RRC state snapshots to help debug large-UE bring-up issues.
  Simulator::Schedule (Seconds (0.9), &PrintUeRrcSummary, ueDevs, "t=0.9");
  Simulator::Schedule (Seconds (clientStart - 0.05),
                       &PrintUeRrcSummary,
                       ueDevs,
                       "pre-clientStart");
  Simulator::Schedule (Seconds (simTime - 0.05),
                       &PrintUeRrcSummary,
                       ueDevs,
                       "pre-stop");

  // ---------------- Wired backbone (Server <-> {PGW, AP1, AP2}) ----------------
  Ptr<Node> pgw = epcHelper->GetPgwNode ();

  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue ("10Gbps"));
  csma.SetChannelAttribute ("Delay", StringValue ("0ms"));

  NodeContainer backboneNodes;
  backboneNodes.Add (server);
  backboneNodes.Add (pgw);
  backboneNodes.Add (apNodes.Get (0));
  backboneNodes.Add (apNodes.Get (1));

  NetDeviceContainer backboneDevs = csma.Install (backboneNodes);
  Ipv4AddressHelper backboneAddr;
  backboneAddr.SetBase ("10.0.0.0", "255.255.255.0");
  Ipv4InterfaceContainer backboneIf = backboneAddr.Assign (backboneDevs);

  const Ipv4Address serverBackboneIp = backboneIf.GetAddress (0);
  const Ipv4Address pgwBackboneIp = backboneIf.GetAddress (1);
  const Ipv4Address ap1BackboneIp = backboneIf.GetAddress (2);
  const Ipv4Address ap2BackboneIp = backboneIf.GetAddress (3);

  const std::string serverBackboneIfName = GetIfName (server, serverBackboneIp);

  // Server routes to UE + both WiFi subnets.
  RunIpAt (server, 0.40,
           "route add 7.0.0.0/8 via " + ToString (pgwBackboneIp) + " dev " +
               serverBackboneIfName);
  RunIpAt (server, 0.41,
           "route add 10.10.0.0/24 via " + ToString (ap1BackboneIp) + " dev " +
               serverBackboneIfName);
  RunIpAt (server, 0.42,
           "route add 10.11.0.0/24 via " + ToString (ap2BackboneIp) + " dev " +
               serverBackboneIfName);

  // APs advertise WiFi subnets as directly connected (helps routing clarity).
  RunIpAt (apNodes.Get (0), 0.40, "route add 10.10.0.0/24 dev " + GetIfName (apNodes.Get (0), ap1WifiIf.GetAddress (0)));
  RunIpAt (apNodes.Get (1), 0.40, "route add 10.11.0.0/24 dev " + GetIfName (apNodes.Get (1), ap2WifiIf.GetAddress (0)));

  // Build per-UE WiFi config maps (address + gateway + subnet) for policy routing.
  std::map<uint32_t, WifiClientConfig> wifiCfgByNodeId;
  for (uint32_t i = 0; i < wifiSta1.GetN (); ++i)
    {
      WifiClientConfig cfg;
      cfg.addr = sta1WifiIf.GetAddress (i);
      cfg.gateway = ap1WifiIf.GetAddress (0);
      cfg.subnetCidr = "10.10.0.0/24";
      wifiCfgByNodeId[wifiSta1.Get (i)->GetId ()] = cfg;
    }
  for (uint32_t i = 0; i < wifiSta2.GetN (); ++i)
    {
      WifiClientConfig cfg;
      cfg.addr = sta2WifiIf.GetAddress (i);
      cfg.gateway = ap2WifiIf.GetAddress (0);
      cfg.subnetCidr = "10.11.0.0/24";
      wifiCfgByNodeId[wifiSta2.Get (i)->GetId ()] = cfg;
    }

  // ---------------- Client routing (policy routing for MPTCP) ----------------
  // Main table: default route via NR gateway (so the first subflow uses NR).
  // Table 2: WiFi source policy (MPTCP fullmesh will create a second subflow
  // from the WiFi source address to the same server address).
  for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
    {
      Ptr<Node> ue = ueNodes.Get (i);
      const Ipv4Address ueNrIp = ueNrIf.GetAddress (i);
      const std::string ueNrIfName = GetIfName (ue, ueNrIp);

      const double base = 0.50 + (i * 0.002); // spread out DCE ip calls a bit

      // Main default route.
      RunIpAt (ue, base,
               "route add default via " + ToString (ueGateway) + " dev " +
                   ueNrIfName);

      if (enableMptcp)
        {
          const auto wifiIt = wifiCfgByNodeId.find (ue->GetId ());
          NS_ABORT_MSG_IF (wifiIt == wifiCfgByNodeId.end (),
                           "Missing WiFi config for UE");
          const WifiClientConfig &wifiCfg = wifiIt->second;
          const std::string ueWifiIfName = GetIfName (ue, wifiCfg.addr);

          // WiFi policy table (2).
          RunIpAt (ue, base + 0.01,
                   "rule add from " + ToString (wifiCfg.addr) + " table 2");
          RunIpAt (ue, base + 0.02,
                   "route add " + wifiCfg.subnetCidr + " dev " + ueWifiIfName +
                       " scope link table 2");
          RunIpAt (ue, base + 0.03,
                   "route add default via " + ToString (wifiCfg.gateway) +
                       " dev " + ueWifiIfName + " table 2");
        }
    }

  // ---------------- Applications ----------------
  PacketSinkHelper sinkHelper ("ns3::LinuxTcpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), port));
  ApplicationContainer sinkApps = sinkHelper.Install (server);
  sinkApps.Start (Seconds (sinkStart));
  sinkApps.Stop (Seconds (simTime));

  Ptr<UniformRandomVariable> startJitterRng = CreateObject<UniformRandomVariable> ();
  startJitterRng->SetStream (clientStartJitterStream);
  double minStartOffset = std::numeric_limits<double>::infinity ();
  double maxStartOffset = 0.0;

  RateDualHelper traffic ("ns3::LinuxTcpSocketFactory",
                          InetSocketAddress (serverBackboneIp, port));
  ApplicationContainer clientApps;
  for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
    {
      ApplicationContainer one = traffic.Install (ueNodes.Get (i));
      const double offset =
          (clientStartJitter > 0.0) ? startJitterRng->GetValue (0.0, clientStartJitter) : 0.0;
      minStartOffset = std::min (minStartOffset, offset);
      maxStartOffset = std::max (maxStartOffset, offset);

      one.Start (Seconds (clientStart + offset));
      one.Stop (Seconds (simTime));
      clientApps.Add (one);
    }
  if (clientStartJitter > 0.0)
    {
      std::cout << std::fixed << std::setprecision (6);
      std::cout << "[120-demo] ClientStart jitter: max=" << clientStartJitter
                << "s minOffset=" << minStartOffset
                << "s maxOffset=" << maxStartOffset << "s" << std::endl;
    }

  if (debugRoutes)
    {
      for (uint32_t i = 0; i < std::min<uint32_t> (3, ueNodes.GetN ()); ++i)
        {
          Ptr<Node> ue = ueNodes.Get (i);
          RunIpAt (ue, clientStart - 0.3, "addr list");
          RunIpAt (ue, clientStart - 0.2, "route show table all");
          RunIpAt (ue, clientStart - 0.1, "route get " + ToString (serverBackboneIp));
        }
      RunIpAt (server, clientStart - 0.3, "addr list");
      RunIpAt (server, clientStart - 0.2, "route show table all");
    }

  Simulator::Stop (Seconds (simTime + 0.5));
  Simulator::Run ();

  // ---------------- PacketSink stats (like myscripts/app-test/app-test1.cc) ----------------
  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  if (sink)
    {
      const uint64_t totalBytes = sink->GetTotalRx ();
      const uint32_t acceptedSockets =
          static_cast<uint32_t> (sink->GetAcceptedSockets ().size ());
      const double activeSeconds = std::max (0.0, simTime - clientStart);
      const double throughputMbps =
          (activeSeconds > 0.0) ? (totalBytes * 8.0 / activeSeconds / 1e6) : 0.0;

      std::cout << std::fixed << std::setprecision (3);
      std::cout << "[120-demo] PacketSink totalRxBytes=" << totalBytes
                << " acceptedSockets=" << acceptedSockets
                << " activeSeconds=" << activeSeconds
                << " throughputMbps=" << throughputMbps << std::endl;
    }
  else
    {
      std::cout << "[120-demo] PacketSink stats unavailable (DynamicCast failed)" << std::endl;
    }

  uint32_t clientConnected = 0;
  uint32_t clientInProgress = 0;
  uint32_t clientConnectFailures = 0;
  for (uint32_t i = 0; i < clientApps.GetN (); ++i)
    {
      Ptr<RateDualModeApplication> app =
          DynamicCast<RateDualModeApplication> (clientApps.Get (i));
      if (!app)
        {
          continue;
        }
      if (app->IsConnected ())
        {
          clientConnected++;
        }
      if (app->IsConnectInProgress ())
        {
          clientInProgress++;
        }
      clientConnectFailures += app->GetConnectFailures ();
    }
  std::cout << "[120-demo] Client connect summary: connected=" << clientConnected
            << "/" << clientApps.GetN ()
            << " inProgress=" << clientInProgress
            << " totalConnectFailures=" << clientConnectFailures << std::endl;

  Simulator::Destroy ();
  return 0;
}
