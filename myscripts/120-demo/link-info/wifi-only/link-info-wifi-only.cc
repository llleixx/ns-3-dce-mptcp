/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/network-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-standards.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE ("LinkInfoWifiOnly");

namespace
{

struct FlowSummary
{
  uint64_t rxBytes = 0;
  uint64_t grossRxBytes = 0;
  uint64_t rxPackets = 0;
  uint64_t lostPackets = 0;
  Time delaySum = Seconds (0);
  Time jitterSum = Seconds (0);
  double netMinFlowThroughputMbps = std::numeric_limits<double>::max ();
  double netMaxFlowThroughputMbps = 0.0;
  double grossMinFlowThroughputMbps = std::numeric_limits<double>::max ();
  double grossMaxFlowThroughputMbps = 0.0;
  uint32_t activeFlows = 0;
};

struct SourceStats
{
  uint64_t rxBytes = 0;
  uint64_t grossRxBytes = 0;
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

    // With SeqTsSizeHeader enabled, PacketSink strips the tracing header before
    // invoking RxWithSeqTsSize. packet->GetSize() is therefore net application
    // payload, while header.GetSize() is the original application packet size.
    stats.rxBytes += packet->GetSize ();
    stats.grossRxBytes += header.GetSize ();
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
        summary.grossRxBytes += stats.grossRxBytes;
        summary.rxPackets += stats.rxPackets;
        summary.lostPackets += stats.inferredLostPackets;
        summary.delaySum += stats.delaySum;
        summary.jitterSum += stats.jitterSum;

        const double netThroughputMbps =
            (static_cast<double> (stats.rxBytes) * 8.0) / activeSeconds / 1e6;
        const double grossThroughputMbps =
            (static_cast<double> (stats.grossRxBytes) * 8.0) / activeSeconds /
            1e6;
        summary.netMinFlowThroughputMbps =
            std::min (summary.netMinFlowThroughputMbps, netThroughputMbps);
        summary.netMaxFlowThroughputMbps =
            std::max (summary.netMaxFlowThroughputMbps, netThroughputMbps);
        summary.grossMinFlowThroughputMbps =
            std::min (summary.grossMinFlowThroughputMbps, grossThroughputMbps);
        summary.grossMaxFlowThroughputMbps =
            std::max (summary.grossMaxFlowThroughputMbps, grossThroughputMbps);
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
              uint32_t numAps,
              double simTime,
              double clientStart,
              const FlowSummary &summary)
{
  const double activeSeconds = std::max (1e-9, simTime - clientStart);
  const double grossThroughputMbps =
      (static_cast<double> (summary.grossRxBytes) * 8.0) / activeSeconds / 1e6;
  const double netPayloadThroughputMbps =
      (static_cast<double> (summary.rxBytes) * 8.0) / activeSeconds / 1e6;
  const double aggregateThroughputMbps =
      netPayloadThroughputMbps;
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
  std::cout << "[link-info][wifi] result clients=" << numClients
            << " aps=" << numAps
            << " grossThroughputMbps=" << grossThroughputMbps
            << " netPayloadThroughputMbps=" << netPayloadThroughputMbps
            << " aggregateThroughputMbps=" << aggregateThroughputMbps
            << " meanDelayMs=" << meanDelayMs
            << " meanJitterMs=" << meanJitterMs
            << " lossRatePct=" << lossRate
            << " activeFlows=" << summary.activeFlows << "/" << numClients;
  if (summary.activeFlows > 0)
    {
      std::cout << " grossMinFlowThroughputMbps="
                << summary.grossMinFlowThroughputMbps
                << " grossMaxFlowThroughputMbps="
                << summary.grossMaxFlowThroughputMbps
                << " netMinFlowThroughputMbps="
                << summary.netMinFlowThroughputMbps
                << " netMaxFlowThroughputMbps="
                << summary.netMaxFlowThroughputMbps;
    }
  std::cout << " grossRxBytes=" << summary.grossRxBytes
            << " netPayloadRxBytes=" << summary.rxBytes << std::endl;
}

} // namespace

int
main (int argc, char *argv[])
{
  uint32_t numClients = 30;
  uint32_t numAps = 1;
  double simTime = 8.0;
  double sinkStart = 1.0;
  double clientStart = 2.0;
  double clientStartStepMs = 10.0;
  double clientStartSkewUs = 100.0;
  double offeredLoadMbpsPerClient = 300.0;
  uint32_t packetSize = 1400;
  uint16_t portBase = 5000;
  uint32_t wifiAssocStaggerMs = 20;
  uint16_t wifiChannelWidthMhz = 160;
  uint16_t wifiFrequencyMhz = 5250;
  uint16_t wifiHeGuardIntervalNs = 800;
  uint16_t wifiHeMpduBufferSize = 256;
  uint32_t wifiBeMaxAmpduSize = 6500631;
  double wifiTxPowerDbm = 23.0;
  std::string wifiPhyModel = "Spectrum";
  double wifiSpectrumMaxLossDb = 110.0;
  bool wifiSpectrumUseFriisLoss = true;
  uint32_t wifiMuSchedulerStations = 0;
  double wifiBackboneDelayMs = 1.0;
  bool wifiEnableOfdma = true;
  bool wifiEnableUlOfdma = true;
  bool wifiEnableBsrp = true;

  CommandLine cmd;
  cmd.AddValue ("numClients", "Number of Wi-Fi clients", numClients);
  cmd.AddValue ("numAps",
                "Number of APs (set to 1 for the single-AP Wi-Fi link benchmark)",
                numAps);
  cmd.AddValue ("simTime", "Simulation stop time in seconds", simTime);
  cmd.AddValue ("sinkStart", "PacketSink start time in seconds", sinkStart);
  cmd.AddValue ("clientStart", "Client start time in seconds", clientStart);
  cmd.AddValue ("clientStartStepMs",
                "Additional start offset per client in ms",
                clientStartStepMs);
  cmd.AddValue ("clientStartSkewUs",
                "Additional deterministic skew per client in us to break exact CBR phase locking",
                clientStartSkewUs);
  cmd.AddValue ("offeredLoadMbpsPerClient",
                "Per-client application sending rate in Mbps, applied to OnOff PacketSize bytes per packet; "
                "includes SeqTsSizeHeader when enabled, excludes UDP/IP/MAC/PHY headers",
                offeredLoadMbpsPerClient);
  cmd.AddValue ("packetSize",
                "Application packet size in bytes at the OnOffApplication layer; "
                "with EnableSeqTsSizeHeader=true this includes the 20-byte SeqTsSizeHeader, "
                "but excludes UDP/IP/MAC/PHY headers",
                packetSize);
  cmd.AddValue ("portBase", "First UDP port used by the sinks", portBase);
  cmd.AddValue ("wifiAssocStaggerMs",
                "Additional WaitBeaconTimeout per STA in ms",
                wifiAssocStaggerMs);
  cmd.AddValue ("wifiChannelWidthMhz",
                "Wi-Fi channel width in MHz",
                wifiChannelWidthMhz);
  cmd.AddValue ("wifiFrequencyMhz",
                "Wi-Fi center frequency in MHz",
                wifiFrequencyMhz);
  cmd.AddValue ("wifiHeGuardIntervalNs",
                "Wi-Fi HE guard interval in ns",
                wifiHeGuardIntervalNs);
  cmd.AddValue ("wifiHeMpduBufferSize",
                "Wi-Fi HE MPDU buffer size",
                wifiHeMpduBufferSize);
  cmd.AddValue ("wifiBeMaxAmpduSize",
                "Wi-Fi AC_BE max A-MPDU size in bytes",
                wifiBeMaxAmpduSize);
  cmd.AddValue ("wifiTxPowerDbm", "Wi-Fi TX power in dBm", wifiTxPowerDbm);
  cmd.AddValue ("wifiPhyModel",
                "Wi-Fi PHY backend: Yans or Spectrum",
                wifiPhyModel);
  cmd.AddValue ("wifiSpectrumMaxLossDb",
                "Spectrum PHY only: skip receptions beyond this path loss",
                wifiSpectrumMaxLossDb);
  cmd.AddValue ("wifiSpectrumUseFriisLoss",
                "Spectrum PHY only: add FriisPropagationLossModel "
                "(disable to mimic the current 120-demo Spectrum channel setup)",
                wifiSpectrumUseFriisLoss);
  cmd.AddValue ("wifiMuSchedulerStations",
                "Maximum stations granted an RU in one DL MU PPDU (0 means auto/all)",
                wifiMuSchedulerStations);
  cmd.AddValue ("wifiBackboneDelayMs",
                "One-way AP-to-server wired delay in ms",
                wifiBackboneDelayMs);
  cmd.AddValue ("wifiEnableOfdma", "Enable AP-side MU scheduler", wifiEnableOfdma);
  cmd.AddValue ("wifiEnableUlOfdma", "Enable UL OFDMA", wifiEnableUlOfdma);
  cmd.AddValue ("wifiEnableBsrp", "Enable BSRP", wifiEnableBsrp);
  cmd.Parse (argc, argv);

  NS_ABORT_MSG_IF (numClients == 0, "numClients must be greater than zero");
  NS_ABORT_MSG_IF (numAps == 0, "numAps must be greater than zero");
  NS_ABORT_MSG_IF (wifiEnableUlOfdma && !wifiEnableOfdma,
                   "wifiEnableUlOfdma requires wifiEnableOfdma");
  NS_ABORT_MSG_IF (wifiEnableBsrp && !wifiEnableOfdma,
                   "wifiEnableBsrp requires wifiEnableOfdma");
  NS_ABORT_MSG_IF (wifiPhyModel != "Yans" && wifiPhyModel != "Spectrum",
                   "wifiPhyModel must be Yans or Spectrum");
  NS_ABORT_MSG_IF (wifiPhyModel == "Yans",
                   "wifiPhyModel=Yans is not supported with this 802.11ax/OFDMA script");

  SeqTsSizeHeader seqTsSizeHeader;
  const uint32_t appHeaderBytes = seqTsSizeHeader.GetSerializedSize ();
  NS_ABORT_MSG_IF (packetSize < appHeaderBytes,
                   "packetSize must be at least SeqTsSizeHeader size");
  const uint32_t netPayloadBytesPerPacket = packetSize - appHeaderBytes;

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold",
                      UintegerValue (4294967295u));

  std::cout << "[link-info][wifi] defaults:"
            << " phy=" << wifiPhyModel << " 802.11ax_5GHz"
            << " width=" << wifiChannelWidthMhz
            << "MHz freq=" << wifiFrequencyMhz
            << "MHz heGi=" << wifiHeGuardIntervalNs
            << "ns heMpduBuffer=" << wifiHeMpduBufferSize
            << " beMaxAmpdu=" << wifiBeMaxAmpduSize
            << " txPower=" << wifiTxPowerDbm
            << "dBm apAnt=4x4 staAnt=2x2 backboneDelay="
            << wifiBackboneDelayMs
            << "ms spectrumMaxLossDb=" << wifiSpectrumMaxLossDb
            << " spectrumLoss="
            << (wifiSpectrumUseFriisLoss ? "friis" : "none")
            << " muStations="
            << (wifiMuSchedulerStations == 0 ? numClients : wifiMuSchedulerStations)
            << " dlOfdma="
            << (wifiEnableOfdma ? "on" : "off")
            << " ulOfdma="
            << ((wifiEnableOfdma && wifiEnableUlOfdma) ? "on" : "off")
            << " bsrp="
            << ((wifiEnableOfdma && wifiEnableBsrp) ? "on" : "off")
            << " clientStep=" << clientStartStepMs
            << "ms+" << clientStartSkewUs
            << "us/client"
            << " packetSize=" << packetSize
            << "B(app)"
            << " appHeader=" << appHeaderBytes
            << "B"
            << " netPayloadPerPacket=" << netPayloadBytesPerPacket
            << "B"
            << " offeredLoadPerClient=" << ToRateString (offeredLoadMbpsPerClient)
            << std::endl;

  NodeContainer staNodes;
  staNodes.Create (numClients);
  NodeContainer apNodes;
  apNodes.Create (numAps);
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
  mobility.Install (staNodes);
  mobility.Install (apNodes);
  mobility.Install (serverNode);

  for (uint32_t index = 0; index < numClients; ++index)
    {
      const uint32_t row = index / columns;
      const uint32_t col = index % columns;
      SetPosition (staNodes.Get (index),
                   Vector (col * colSpacing, row * rowSpacing, clientZ));
    }

  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      const double apY =
          (gridHeight > 0.0)
              ? (((static_cast<double> (apIndex) + 0.5) * gridHeight) /
                 static_cast<double> (numAps))
              : centerY;
      SetPosition (apNodes.Get (apIndex), Vector (centerX, apY, infraZ));
    }
  SetPosition (server, Vector (centerX, centerY + 30.0, 0.0));

  InternetStackHelper internet;
  internet.Install (staNodes);
  internet.Install (apNodes);
  internet.Install (serverNode);

  std::vector<NodeContainer> staGroups (numAps);
  for (uint32_t n = 0; n < numClients; ++n)
    {
      const uint32_t row = n / columns;
      const uint32_t apIndex =
          std::min ((row * numAps) / std::max (1u, rows), numAps - 1);
      staGroups[apIndex].Add (staNodes.Get (n));
    }

  std::ostringstream split;
  split << "[link-info][wifi] STA split:";
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      split << " ap" << (apIndex + 1) << "=" << staGroups[apIndex].GetN ();
    }
  std::cout << split.str () << std::endl;

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");

  std::vector<NetDeviceContainer> apWifiDevs (numAps);
  std::vector<NetDeviceContainer> staWifiDevs (numAps);
  std::vector<Ipv4InterfaceContainer> apWifiIfs (numAps);
  std::vector<Ipv4InterfaceContainer> staWifiIfs (numAps);
  std::vector<Ipv4Address> apWifiGateways;
  std::vector<std::string> wifiSubnetBases;
  apWifiGateways.reserve (numAps);
  wifiSubnetBases.reserve (numAps);

  auto installWifi = [&wifi,
                      wifiAssocStaggerMs,
                      wifiChannelWidthMhz,
                      wifiFrequencyMhz,
                      wifiHeGuardIntervalNs,
                      wifiHeMpduBufferSize,
                      wifiBeMaxAmpduSize,
                      wifiTxPowerDbm,
                      wifiPhyModel,
                      wifiSpectrumMaxLossDb,
                      wifiSpectrumUseFriisLoss,
                      wifiMuSchedulerStations,
                      wifiEnableOfdma,
                      wifiEnableUlOfdma,
                      wifiEnableBsrp] (Ptr<Node> ap,
                                       const NodeContainer &stas,
                                       const std::string &ssidName,
                                       const std::string &subnetBase,
                                       NetDeviceContainer &apDev,
                                       NetDeviceContainer &staDev,
                                       Ipv4InterfaceContainer &apIf,
                                       Ipv4InterfaceContainer &staIf) {
    WifiMacHelper mac;
    const Ssid ssid (ssidName);
    const uint32_t requestedMuStations =
        (wifiMuSchedulerStations == 0) ? stas.GetN () : wifiMuSchedulerStations;
    const uint8_t muSchedulerStations =
        static_cast<uint8_t> (std::max<uint32_t> (
            1u, std::min<uint32_t> (std::min<uint32_t> (stas.GetN (), requestedMuStations), 74)));
    if (wifiEnableOfdma)
      {
        mac.SetMultiUserScheduler ("ns3::RrMultiUserScheduler",
                                   "NStations",
                                   UintegerValue (muSchedulerStations),
                                   "EnableUlOfdma",
                                   BooleanValue (wifiEnableUlOfdma),
                                   "EnableBsrp",
                                   BooleanValue (wifiEnableBsrp));
      }

    if (wifiPhyModel == "Spectrum")
      {
        Ptr<MultiModelSpectrumChannel> spectrumChannel =
            CreateObject<MultiModelSpectrumChannel> ();
        spectrumChannel->SetAttribute ("MaxLossDb",
                                       DoubleValue (wifiSpectrumMaxLossDb));
        if (wifiSpectrumUseFriisLoss)
          {
            Ptr<FriisPropagationLossModel> lossModel =
                CreateObject<FriisPropagationLossModel> ();
            lossModel->SetFrequency (static_cast<double> (wifiFrequencyMhz) * 1e6);
            spectrumChannel->AddPropagationLossModel (lossModel);
          }
        spectrumChannel->SetPropagationDelayModel (
            CreateObject<ConstantSpeedPropagationDelayModel> ());

        SpectrumWifiPhyHelper phy;
        phy.SetChannel (spectrumChannel);
        phy.Set ("ChannelWidth", UintegerValue (wifiChannelWidthMhz));
        phy.Set ("Frequency", UintegerValue (wifiFrequencyMhz));
        phy.Set ("TxPowerStart", DoubleValue (wifiTxPowerDbm));
        phy.Set ("TxPowerEnd", DoubleValue (wifiTxPowerDbm));

        phy.Set ("Antennas", UintegerValue (4));
        phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
        phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));
        mac.SetType ("ns3::ApWifiMac",
                     "EnableBeaconJitter",
                     BooleanValue (false),
                     "Ssid",
                     SsidValue (ssid));
        apDev = wifi.Install (phy, mac, ap);

        phy.Set ("Antennas", UintegerValue (2));
        phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
        phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));
        for (uint32_t i = 0; i < stas.GetN (); ++i)
          {
            NodeContainer singleSta;
            singleSta.Add (stas.Get (i));
            mac.SetType ("ns3::StaWifiMac",
                         "Ssid",
                         SsidValue (ssid),
                         "ActiveProbing",
                         BooleanValue (false),
                         "WaitBeaconTimeout",
                         TimeValue (MilliSeconds (120 + i * wifiAssocStaggerMs)));
            staDev.Add (wifi.Install (phy, mac, singleSta));
          }
      }
    else
      {
        YansWifiChannelHelper channel;
        channel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
        channel.AddPropagationLoss ("ns3::FriisPropagationLossModel",
                                    "Frequency",
                                    DoubleValue (static_cast<double> (wifiFrequencyMhz) * 1e6));

        YansWifiPhyHelper phy;
        phy.SetChannel (channel.Create ());
        phy.Set ("ChannelWidth", UintegerValue (wifiChannelWidthMhz));
        phy.Set ("Frequency", UintegerValue (wifiFrequencyMhz));
        phy.Set ("TxPowerStart", DoubleValue (wifiTxPowerDbm));
        phy.Set ("TxPowerEnd", DoubleValue (wifiTxPowerDbm));

        phy.Set ("Antennas", UintegerValue (4));
        phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
        phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));
        mac.SetType ("ns3::ApWifiMac",
                     "EnableBeaconJitter",
                     BooleanValue (false),
                     "Ssid",
                     SsidValue (ssid));
        apDev = wifi.Install (phy, mac, ap);

        phy.Set ("Antennas", UintegerValue (2));
        phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
        phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));
        for (uint32_t i = 0; i < stas.GetN (); ++i)
          {
            NodeContainer singleSta;
            singleSta.Add (stas.Get (i));
            mac.SetType ("ns3::StaWifiMac",
                         "Ssid",
                         SsidValue (ssid),
                         "ActiveProbing",
                         BooleanValue (false),
                         "WaitBeaconTimeout",
                         TimeValue (MilliSeconds (120 + i * wifiAssocStaggerMs)));
            staDev.Add (wifi.Install (phy, mac, singleSta));
          }
      }

    auto configureWifiDevices = [&] (const NetDeviceContainer &devices) {
      for (uint32_t i = 0; i < devices.GetN (); ++i)
        {
          Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (devices.Get (i));
          if (dev == 0)
            {
              continue;
            }
          Ptr<HeConfiguration> he = dev->GetHeConfiguration ();
          if (he != 0)
            {
              he->SetGuardInterval (NanoSeconds (wifiHeGuardIntervalNs));
              he->SetMpduBufferSize (wifiHeMpduBufferSize);
            }
          Ptr<RegularWifiMac> regularMac =
              DynamicCast<RegularWifiMac> (dev->GetMac ());
          if (regularMac != 0)
            {
              regularMac->SetAttribute ("BE_MaxAmpduSize",
                                        UintegerValue (wifiBeMaxAmpduSize));
            }
        }
    };
    configureWifiDevices (apDev);
    configureWifiDevices (staDev);

    Ipv4AddressHelper wifiAddr;
    wifiAddr.SetBase (subnetBase.c_str (), "255.255.255.0");
    apIf = wifiAddr.Assign (apDev);
    staIf = wifiAddr.Assign (staDev);
  };

  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      const std::string subnetBase =
          "10." + std::to_string (10 + apIndex) + ".0.0";
      wifiSubnetBases.push_back (subnetBase);
      installWifi (apNodes.Get (apIndex),
                   staGroups[apIndex],
                   "wifi-link-info-ap-" + std::to_string (apIndex + 1),
                   subnetBase,
                   apWifiDevs[apIndex],
                   staWifiDevs[apIndex],
                   apWifiIfs[apIndex],
                   staWifiIfs[apIndex]);
      apWifiGateways.push_back (apWifiIfs[apIndex].GetAddress (0));
    }

  PointToPointHelper p2p;
  p2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  p2p.SetChannelAttribute ("Delay",
                           TimeValue (MilliSeconds (wifiBackboneDelayMs)));

  std::vector<Ipv4Address> serverBackboneIps;
  std::vector<Ipv4Address> apBackboneIps;
  std::vector<uint32_t> serverBackboneIfs;
  std::map<uint32_t, Ipv4Address> serverAddrByStaNodeId;
  serverBackboneIps.reserve (numAps);
  apBackboneIps.reserve (numAps);
  serverBackboneIfs.reserve (numAps);

  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      NetDeviceContainer linkDevs = p2p.Install (server, apNodes.Get (apIndex));
      Ipv4AddressHelper backboneAddr;
      const std::string base =
          "172.16." + std::to_string (apIndex + 1) + ".0";
      backboneAddr.SetBase (base.c_str (), "255.255.255.0");
      Ipv4InterfaceContainer linkIf = backboneAddr.Assign (linkDevs);
      serverBackboneIps.push_back (linkIf.GetAddress (0));
      apBackboneIps.push_back (linkIf.GetAddress (1));
      serverBackboneIfs.push_back (
          server->GetObject<Ipv4> ()->GetInterfaceForDevice (linkDevs.Get (0)));

      for (uint32_t i = 0; i < staGroups[apIndex].GetN (); ++i)
        {
          serverAddrByStaNodeId[staGroups[apIndex].Get (i)->GetId ()] =
              linkIf.GetAddress (0);
        }
    }

  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      Ptr<Ipv4> ipv4 = apNodes.Get (apIndex)->GetObject<Ipv4> ();
      for (uint32_t ifIndex = 0; ifIndex < ipv4->GetNInterfaces (); ++ifIndex)
        {
          ipv4->SetForwarding (ifIndex, true);
        }
    }

  Ipv4StaticRoutingHelper routingHelper;
  Ptr<Ipv4StaticRouting> serverRouting =
      routingHelper.GetStaticRouting (server->GetObject<Ipv4> ());

  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      const Ipv4Address subnet (wifiSubnetBases[apIndex].c_str ());
      serverRouting->AddNetworkRouteTo (subnet,
                                        Ipv4Mask ("255.255.255.0"),
                                        apBackboneIps[apIndex],
                                        serverBackboneIfs[apIndex]);

      for (uint32_t i = 0; i < staGroups[apIndex].GetN (); ++i)
        {
          Ptr<Node> sta = staGroups[apIndex].Get (i);
          Ptr<Ipv4StaticRouting> staRouting =
              routingHelper.GetStaticRouting (sta->GetObject<Ipv4> ());
          const uint32_t staIfIndex = sta->GetObject<Ipv4> ()->GetInterfaceForDevice (
              staWifiDevs[apIndex].Get (i));
          staRouting->SetDefaultRoute (apWifiGateways[apIndex], staIfIndex);
        }
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

      const auto serverAddrIt = serverAddrByStaNodeId.find (staNodes.Get (i)->GetId ());
      NS_ABORT_MSG_IF (serverAddrIt == serverAddrByStaNodeId.end (),
                       "Missing server backbone address for STA");

      OnOffHelper client ("ns3::UdpSocketFactory",
                          Address (InetSocketAddress (serverAddrIt->second, port)));
      client.SetAttribute (
          "OnTime",
          StringValue ("ns3::ConstantRandomVariable[Constant=1]"));
      client.SetAttribute (
          "OffTime",
          StringValue ("ns3::ConstantRandomVariable[Constant=0]"));
      client.SetAttribute ("EnableSeqTsSizeHeader", BooleanValue (true));
      client.SetConstantRate (DataRate (offeredLoad), packetSize);
      clientApps.Add (client.Install (staNodes.Get (i)));
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
      // A tiny deterministic skew avoids pathological phase locking when
      // packetSize/dataRate yields an exact CBR interval (e.g. 1400 B at
      // 40 Mbps -> 280 us) under fully deterministic Wi-Fi timing.
      clientApps.Get (i)->SetStartTime (
          Seconds (clientStart +
                   (i * (clientStartStepMs / 1000.0)) +
                   (i * (clientStartSkewUs / 1e6))));
      clientApps.Get (i)->SetStopTime (Seconds (simTime));
    }

  Simulator::Stop (Seconds (simTime));
  Simulator::Run ();

  const double activeSeconds = std::max (1e-9, simTime - clientStart);
  FlowSummary summary = sinkRxTracker.Summarize (activeSeconds);

  PrintSummary (numClients,
                numAps,
                simTime,
                clientStart,
                summary);

  Simulator::Destroy ();
  return 0;
}
