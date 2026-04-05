/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/antenna-module.h"
#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-module.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/point-to-point-module.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LinkInfoNrOnly");

namespace
{

struct FlowSummary
{
  uint64_t rxBytes = 0;
  uint64_t rxPackets = 0;
  uint64_t lostPackets = 0;
  Time delaySum = Seconds (0);
  Time jitterSum = Seconds (0);
  double minFlowThroughputMbps = std::numeric_limits<double>::max ();
  double maxFlowThroughputMbps = 0.0;
  uint32_t activeFlows = 0;
};

struct SourceStats
{
  uint64_t rxBytes = 0;
  uint64_t rxPackets = 0;
  uint64_t inferredLostPackets = 0;
  Time delaySum = Seconds (0);
  Time jitterSum = Seconds (0);
  Time lastDelay = Seconds (0);
  bool hasLastDelay = false;
  uint32_t nextExpectedSeq = 0;
  bool hasNextExpectedSeq = false;
};

class PacketSinkRxTracker
{
public:
  void
  Rx (Ptr<const Packet> packet,
      const Address &from,
      const Address &to,
      const SeqTsSizeHeader &header)
  {
    (void) to;

    if (!InetSocketAddress::IsMatchingType (from))
      {
        return;
      }

    const Ipv4Address src = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
    SourceStats &stats = m_stats[src];
    const Time delay = Simulator::Now () - header.GetTs ();

    stats.rxBytes += packet->GetSize ();
    stats.rxPackets += 1;
    stats.delaySum += delay;

    if (stats.hasLastDelay)
      {
        const int64_t diffNs =
            (delay - stats.lastDelay).GetNanoSeconds ();
        stats.jitterSum += NanoSeconds (std::llabs (diffNs));
      }
    stats.lastDelay = delay;
    stats.hasLastDelay = true;

    const uint32_t seq = header.GetSeq ();
    if (stats.hasNextExpectedSeq && seq > stats.nextExpectedSeq)
      {
        stats.inferredLostPackets += seq - stats.nextExpectedSeq;
      }
    stats.nextExpectedSeq = seq + 1;
    stats.hasNextExpectedSeq = true;
  }

  FlowSummary
  Summarize (double activeSeconds) const
  {
    FlowSummary summary;
    for (const auto &entry : m_stats)
      {
        const SourceStats &stats = entry.second;
        if (stats.rxPackets == 0)
          {
            continue;
          }

        summary.rxBytes += stats.rxBytes;
        summary.rxPackets += stats.rxPackets;
        summary.lostPackets += stats.inferredLostPackets;
        summary.delaySum += stats.delaySum;
        summary.jitterSum += stats.jitterSum;

        const double throughputMbps =
            (static_cast<double> (stats.rxBytes) * 8.0) / activeSeconds / 1e6;
        summary.minFlowThroughputMbps =
            std::min (summary.minFlowThroughputMbps, throughputMbps);
        summary.maxFlowThroughputMbps =
            std::max (summary.maxFlowThroughputMbps, throughputMbps);
        ++summary.activeFlows;
      }
    return summary;
  }

private:
  std::map<Ipv4Address, SourceStats> m_stats;
};

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

std::string
ToRateString (double rateMbps)
{
  std::ostringstream oss;
  oss << std::fixed << std::setprecision (3) << rateMbps << "Mbps";
  return oss.str ();
}

void
PrintSummary (uint32_t numClients,
              double simTime,
              double clientStart,
              uint64_t sinkTotalRxBytes,
              const FlowSummary &summary)
{
  const double activeSeconds = std::max (1e-9, simTime - clientStart);
  const double aggregateThroughputMbps =
      (static_cast<double> (sinkTotalRxBytes) * 8.0) / activeSeconds / 1e6;
  const double meanDelayMs =
      (summary.rxPackets > 0)
          ? (summary.delaySum.GetSeconds () * 1000.0 / summary.rxPackets)
          : 0.0;
  const double meanJitterMs =
      (summary.rxPackets > 0)
          ? (summary.jitterSum.GetSeconds () * 1000.0 / summary.rxPackets)
          : 0.0;
  const uint64_t totalObservedPackets = summary.rxPackets + summary.lostPackets;
  const double lossRate =
      (totalObservedPackets > 0)
          ? (100.0 * static_cast<double> (summary.lostPackets) /
             static_cast<double> (totalObservedPackets))
          : 0.0;

  std::cout << std::fixed << std::setprecision (3);
  std::cout << "[link-info][nr] result clients=" << numClients
            << " aggregateThroughputMbps=" << aggregateThroughputMbps
            << " meanDelayMs=" << meanDelayMs
            << " meanJitterMs=" << meanJitterMs
            << " lossRatePct=" << lossRate
            << " activeFlows=" << summary.activeFlows << "/" << numClients;
  if (summary.activeFlows > 0)
    {
      std::cout << " minFlowThroughputMbps=" << summary.minFlowThroughputMbps
                << " maxFlowThroughputMbps=" << summary.maxFlowThroughputMbps;
    }
  std::cout << " sinkTotalRxBytes=" << sinkTotalRxBytes << std::endl;
}

} // namespace

int
main (int argc, char *argv[])
{
  uint32_t numClients = 30;
  double simTime = 8.0;
  double sinkStart = 1.0;
  double clientStart = 2.0;
  double clientStartStepMs = 10.0;
  double offeredLoadMbpsPerClient = 300.0;
  uint32_t packetSize = 1400;
  uint16_t portBase = 6000;
  double nrFrequency = 4.9e9;
  double nrBandwidth = 100e6;
  uint16_t nrNumerology = 2;
  std::string nrTddPattern = "DL|F|UL|UL|UL|";
  bool nrUseEesmT2 = true;
  bool nrFixedMcsUl = true;
  bool nrFixedMcsDl = false;
  uint16_t nrStartingMcsUl = 25;
  uint16_t nrStartingMcsDl = 25;
  std::string nrAmcModel = "ErrorModel";
  double nrGnbTxPower = 30.0;
  double nrUeTxPower = 23.0;
  double nrCoreDelayMs = 1.0;
  double nrBackboneDelayMs = 0.5;

  CommandLine cmd;
  cmd.AddValue ("numClients", "Number of NR UEs", numClients);
  cmd.AddValue ("simTime", "Simulation stop time in seconds", simTime);
  cmd.AddValue ("sinkStart", "PacketSink start time in seconds", sinkStart);
  cmd.AddValue ("clientStart", "Client start time in seconds", clientStart);
  cmd.AddValue ("clientStartStepMs",
                "Additional start offset per client in ms",
                clientStartStepMs);
  cmd.AddValue ("offeredLoadMbpsPerClient",
                "Per-client UDP offered load in Mbps",
                offeredLoadMbpsPerClient);
  cmd.AddValue ("packetSize", "UDP payload size in bytes", packetSize);
  cmd.AddValue ("portBase", "First UDP port used by the sinks", portBase);
  cmd.AddValue ("nrFrequency", "NR carrier frequency in Hz", nrFrequency);
  cmd.AddValue ("nrBandwidth", "NR carrier bandwidth in Hz", nrBandwidth);
  cmd.AddValue ("nrNumerology", "NR numerology", nrNumerology);
  cmd.AddValue ("nrTddPattern", "NR TDD pattern", nrTddPattern);
  cmd.AddValue ("nrUseEesmT2", "Use NrEesmCcT2", nrUseEesmT2);
  cmd.AddValue ("nrFixedMcsUl", "Fix UL MCS", nrFixedMcsUl);
  cmd.AddValue ("nrFixedMcsDl", "Fix DL MCS", nrFixedMcsDl);
  cmd.AddValue ("nrStartingMcsUl", "Starting UL MCS", nrStartingMcsUl);
  cmd.AddValue ("nrStartingMcsDl", "Starting DL MCS", nrStartingMcsDl);
  cmd.AddValue ("nrAmcModel", "NR AMC model", nrAmcModel);
  cmd.AddValue ("nrGnbTxPower", "gNB TX power in dBm", nrGnbTxPower);
  cmd.AddValue ("nrUeTxPower", "UE TX power in dBm", nrUeTxPower);
  cmd.AddValue ("nrCoreDelayMs", "EPC S1-U delay in ms", nrCoreDelayMs);
  cmd.AddValue ("nrBackboneDelayMs",
                "One-way PGW-to-server wired delay in ms",
                nrBackboneDelayMs);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (numClients == 0, "numClients must be greater than zero");

  const uint16_t nrMaxMcsIndex = nrUseEesmT2 ? 27 : 28;
  nrStartingMcsUl = std::min<uint16_t> (nrStartingMcsUl, nrMaxMcsIndex);
  nrStartingMcsDl = std::min<uint16_t> (nrStartingMcsDl, nrMaxMcsIndex);

  NrAmc::AmcModel nrAmcModelEnum = NrAmc::ErrorModel;
  if (nrAmcModel == "ErrorModel")
    {
      nrAmcModelEnum = NrAmc::ErrorModel;
    }
  else if (nrAmcModel == "ShannonModel")
    {
      nrAmcModelEnum = NrAmc::ShannonModel;
    }
  else
    {
      NS_ABORT_MSG ("Unsupported nrAmcModel; use ErrorModel or ShannonModel");
    }

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::LteRlcUm::MaxTxBufferSize",
                      UintegerValue (999999999));
  Config::SetDefault ("ns3::LteRlcAm::MaxTxBufferSize",
                      UintegerValue (999999999));
  Config::SetDefault ("ns3::LteEnbRrc::SrsPeriodicity", UintegerValue (320));
  Config::SetDefault ("ns3::ThreeGppChannelModel::UpdatePeriod",
                      TimeValue (MilliSeconds (0)));

  std::cout << "[link-info][nr] defaults:"
            << " freq=" << nrFrequency / 1e9
            << "GHz bw=" << nrBandwidth / 1e6
            << "MHz numerology=" << nrNumerology
            << " pattern=\"" << nrTddPattern << "\""
            << " errorModel="
            << (nrUseEesmT2 ? "NrEesmCcT2(Table2,256QAM)"
                            : "NrLteMiErrorModel(64QAM)")
            << " amcModel=" << nrAmcModel
            << " fixedMcsUl=" << nrFixedMcsUl
            << " startingMcsUl=" << nrStartingMcsUl
            << " fixedMcsDl=" << nrFixedMcsDl
            << " startingMcsDl=" << nrStartingMcsDl
            << " ueTxPower=" << nrUeTxPower
            << "dBm gnbTxPower=" << nrGnbTxPower
            << "dBm ueAnt=1x2 gnbAnt=2x2 coreDelay="
            << nrCoreDelayMs
            << "ms backboneDelay="
            << nrBackboneDelayMs
            << "ms"
            << " offeredLoadPerClient=" << ToRateString (offeredLoadMbpsPerClient)
            << std::endl;

  NodeContainer ueNodes;
  ueNodes.Create (numClients);
  NodeContainer gnbNode;
  gnbNode.Create (1);
  NodeContainer serverNode;
  serverNode.Create (1);
  Ptr<Node> server = serverNode.Get (0);

  const uint32_t columns = std::min<uint32_t> (3u, numClients);
  const uint32_t rows = (numClients + columns - 1) / columns;
  const double colSpacing = 2.8;
  const double rowSpacing = 0.7;
  const double clientZ = 0.0;
  const double infraZ = 5.0;
  const double gridWidth = (columns > 1) ? ((columns - 1) * colSpacing) : 0.0;
  const double gridHeight = (rows > 1) ? ((rows - 1) * rowSpacing) : 0.0;
  const double centerX = gridWidth / 2.0;
  const double centerY = gridHeight / 2.0;

  MobilityHelper mobility;
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");
  mobility.Install (ueNodes);
  mobility.Install (gnbNode);
  mobility.Install (serverNode);

  for (uint32_t index = 0; index < numClients; ++index)
    {
      const uint32_t row = index / columns;
      const uint32_t col = index % columns;
      SetPosition (ueNodes.Get (index),
                   Vector (col * colSpacing, row * rowSpacing, clientZ));
    }
  SetPosition (gnbNode.Get (0), Vector (centerX, centerY, infraZ));
  SetPosition (server, Vector (centerX, centerY + 30.0, 0.0));

  InternetStackHelper internet;
  internet.Install (ueNodes);
  internet.Install (serverNode);

  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  Ptr<IdealBeamformingHelper> beamformingHelper =
      CreateObject<IdealBeamformingHelper> ();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();
  nrHelper->SetEpcHelper (epcHelper);
  nrHelper->SetBeamformingHelper (beamformingHelper);
  epcHelper->SetAttribute ("S1uLinkDelay",
                           TimeValue (MilliSeconds (nrCoreDelayMs)));

  CcBwpCreator ccBwpCreator;
  CcBwpCreator::SimpleOperationBandConf bandConf (
      nrFrequency,
      nrBandwidth,
      1,
      BandwidthPartInfo::UMi_StreetCanyon_LoS);
  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
  nrHelper->InitializeOperationBand (&band);
  BandwidthPartInfoPtrVector allBwps =
      CcBwpCreator::GetAllBwps ({band});

  nrHelper->SetPathlossAttribute ("ShadowingEnabled", BooleanValue (false));
  nrHelper->SetSchedulerTypeId (NrMacSchedulerOfdmaRR::GetTypeId ());
  nrHelper->SetSchedulerAttribute ("FixedMcsUl", BooleanValue (nrFixedMcsUl));
  nrHelper->SetSchedulerAttribute ("FixedMcsDl", BooleanValue (nrFixedMcsDl));
  nrHelper->SetSchedulerAttribute ("StartingMcsUl",
                                   UintegerValue (static_cast<uint8_t> (nrStartingMcsUl)));
  nrHelper->SetSchedulerAttribute ("StartingMcsDl",
                                   UintegerValue (static_cast<uint8_t> (nrStartingMcsDl)));
  nrHelper->SetGnbDlAmcAttribute ("AmcModel", EnumValue (nrAmcModelEnum));
  nrHelper->SetGnbUlAmcAttribute ("AmcModel", EnumValue (nrAmcModelEnum));
  beamformingHelper->SetAttribute ("BeamformingMethod",
                                   TypeIdValue (DirectPathBeamforming::GetTypeId ()));

  if (nrUseEesmT2)
    {
      Config::SetDefault ("ns3::NrAmc::ErrorModelType",
                          TypeIdValue (TypeId::LookupByName ("ns3::NrEesmCcT2")));
      nrHelper->SetDlErrorModel ("ns3::NrEesmCcT2");
      nrHelper->SetUlErrorModel ("ns3::NrEesmCcT2");
    }

  nrHelper->SetUeAntennaAttribute ("NumRows", UintegerValue (1));
  nrHelper->SetUeAntennaAttribute ("NumColumns", UintegerValue (2));
  nrHelper->SetUeAntennaAttribute (
      "AntennaElement",
      PointerValue (CreateObject<ThreeGppAntennaModel> ()));
  nrHelper->SetGnbAntennaAttribute ("NumRows", UintegerValue (2));
  nrHelper->SetGnbAntennaAttribute ("NumColumns", UintegerValue (2));
  nrHelper->SetGnbAntennaAttribute (
      "AntennaElement",
      PointerValue (CreateObject<ThreeGppAntennaModel> ()));
  nrHelper->SetGnbPhyAttribute ("TxPower", DoubleValue (nrGnbTxPower));
  nrHelper->SetUePhyAttribute ("TxPower", DoubleValue (nrUeTxPower));

  NetDeviceContainer gnbDevs = nrHelper->InstallGnbDevice (gnbNode, allBwps);
  NetDeviceContainer ueDevs = nrHelper->InstallUeDevice (ueNodes, allBwps);

  nrHelper->GetGnbPhy (gnbDevs.Get (0), 0)->SetTxPower (nrGnbTxPower);
  nrHelper->GetGnbPhy (gnbDevs.Get (0), 0)->SetAttribute ("Numerology",
                                                          UintegerValue (nrNumerology));
  nrHelper->GetGnbPhy (gnbDevs.Get (0), 0)->SetAttribute ("Pattern",
                                                          StringValue (nrTddPattern));
  DynamicCast<NrGnbNetDevice> (gnbDevs.Get (0))->UpdateConfig ();
  for (auto it = ueDevs.Begin (); it != ueDevs.End (); ++it)
    {
      DynamicCast<NrUeNetDevice> (*it)->UpdateConfig ();
    }

  Ipv4InterfaceContainer ueIf = epcHelper->AssignUeIpv4Address (ueDevs);
  const Ipv4Address ueGateway = epcHelper->GetUeDefaultGatewayAddress ();
  nrHelper->AttachToClosestEnb (ueDevs, gnbDevs);

  Ptr<Node> pgw = epcHelper->GetPgwNode ();
  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  p2p.SetChannelAttribute ("Delay",
                           TimeValue (MilliSeconds (nrBackboneDelayMs)));

  NetDeviceContainer backboneDevs = p2p.Install (server, pgw);
  Ipv4AddressHelper backboneAddr;
  backboneAddr.SetBase ("172.16.0.0", "255.255.255.0");
  Ipv4InterfaceContainer backboneIf = backboneAddr.Assign (backboneDevs);
  const Ipv4Address serverBackboneIp = backboneIf.GetAddress (0);
  const Ipv4Address pgwBackboneIp = backboneIf.GetAddress (1);

  Ipv4StaticRoutingHelper routingHelper;
  Ptr<Ipv4StaticRouting> serverRouting =
      routingHelper.GetStaticRouting (server->GetObject<Ipv4> ());
  const uint32_t serverBackboneIf =
      server->GetObject<Ipv4> ()->GetInterfaceForDevice (backboneDevs.Get (0));
  serverRouting->AddNetworkRouteTo (Ipv4Address ("7.0.0.0"),
                                    Ipv4Mask ("255.0.0.0"),
                                    pgwBackboneIp,
                                    serverBackboneIf);

  for (uint32_t i = 0; i < numClients; ++i)
    {
      Ptr<Ipv4StaticRouting> ueRouting =
          routingHelper.GetStaticRouting (ueNodes.Get (i)->GetObject<Ipv4> ());
      const uint32_t ueIfIndex =
          ueNodes.Get (i)->GetObject<Ipv4> ()->GetInterfaceForDevice (
              ueDevs.Get (i));
      ueRouting->SetDefaultRoute (ueGateway, ueIfIndex);
    }

  const std::string offeredLoad = ToRateString (offeredLoadMbpsPerClient);
  ApplicationContainer sinkApps;
  ApplicationContainer clientApps;
  PacketSinkRxTracker sinkRxTracker;
  for (uint32_t i = 0; i < numClients; ++i)
    {
      const uint16_t port = portBase + static_cast<uint16_t> (i);
      PacketSinkHelper sinkHelper ("ns3::UdpSocketFactory",
                                   InetSocketAddress (Ipv4Address::GetAny (),
                                                      port));
      sinkHelper.SetAttribute ("EnableSeqTsSizeHeader", BooleanValue (true));
      sinkApps.Add (sinkHelper.Install (server));

      OnOffHelper client ("ns3::UdpSocketFactory",
                          Address (InetSocketAddress (serverBackboneIp, port)));
      client.SetAttribute (
          "OnTime",
          StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
      client.SetAttribute (
          "OffTime",
          StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      client.SetAttribute ("EnableSeqTsSizeHeader", BooleanValue (true));
      client.SetConstantRate (DataRate (offeredLoad), packetSize);
      clientApps.Add (client.Install (ueNodes.Get (i)));
    }

  for (uint32_t i = 0; i < sinkApps.GetN (); ++i)
    {
      const bool connected = sinkApps.Get (i)->TraceConnectWithoutContext (
          "RxWithSeqTsSize",
          MakeCallback (&PacketSinkRxTracker::Rx, &sinkRxTracker));
      NS_ABORT_MSG_UNLESS (connected,
                           "Failed to connect PacketSink RxWithSeqTsSize trace");
    }

  sinkApps.Start (Seconds (sinkStart));
  sinkApps.Stop (Seconds (simTime));
  for (uint32_t i = 0; i < clientApps.GetN (); ++i)
    {
      clientApps.Get (i)->SetStartTime (
          Seconds (clientStart +
                   (i * (clientStartStepMs / 1000.0))));
      clientApps.Get (i)->SetStopTime (Seconds (simTime));
    }

  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();

  const double activeSeconds = std::max (1e-9, simTime - clientStart);
  FlowSummary summary = sinkRxTracker.Summarize (activeSeconds);

  PrintSummary (numClients,
                simTime,
                clientStart,
                summary.rxBytes,
                summary);

  Simulator::Destroy ();
  return 0;
}
