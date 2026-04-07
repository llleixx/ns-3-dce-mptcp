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
#include "ns3/propagation-module.h"
#include "ns3/spectrum-module.h"

// NR / EPC
#include "ns3/nr-module.h"
#include "ns3/nr-helper.h"
#include "ns3/nr-point-to-point-epc-helper.h"
#include "ns3/antenna-module.h"
#include "ns3/nr-mac-scheduler-ofdma-rr.h"
#include "ns3/nr-amc.h"
#include "ns3/lte-ue-rrc.h"

// DCE
#include "ns3/dce-module.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "../../model/linux/ipv4-linux.h"
#include "../app-test/rate-dual-helper.h"
#include "../app-test/rate-dual-application.h"
#include "pamptcp-profile.h"
#include "pamptcp-report.h"

using namespace ns3;
using namespace ns3::pamptcp;

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
PrintLinuxSysctl (std::string key, std::string value)
{
  std::cout << "[120-demo] sysctl " << key << "=" << value << std::endl;
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
SnapshotPacketSinkAcceptedSockets (Ptr<PacketSink> sink,
                                   uint32_t *acceptedSockets,
                                   bool *snapshotTaken)
{
  if (acceptedSockets == nullptr || snapshotTaken == nullptr)
    {
      return;
    }

  *snapshotTaken = true;
  *acceptedSockets =
      sink ? static_cast<uint32_t> (sink->GetAcceptedSockets ().size ()) : 0;
}

void
ConfigureMptcp (LinuxStackHelper &stack,
                NodeContainer nodes,
                bool enableMptcp,
                bool enableMptcpDebug,
                const std::string &mptcpScheduler)
{
  if (!enableMptcp)
    {
      return;
    }

  stack.SysctlSet (nodes, ".net.mptcp.mptcp_enabled", "1");
  stack.SysctlSet (nodes, ".net.mptcp.mptcp_path_manager", "fullmesh");
  stack.SysctlSet (nodes, ".net.mptcp.mptcp_scheduler", mptcpScheduler);
  stack.SysctlSet (nodes, ".net.ipv4.tcp_congestion_control", "olia");
  if (enableMptcpDebug)
    {
      stack.SysctlSet (nodes, ".net.mptcp.mptcp_debug", "1");
    }
}

} // namespace

int
main (int argc, char *argv[])
{
  uint32_t numClients = 120;
  double simTime = 20.0;
  double sinkStart = 1.0;
  double clientStart = 1.1;
  double clientStartJitter = 0.03;
  int64_t clientStartJitterStream = 1;
  uint16_t port = 5000;
  uint32_t numAps = 4;
  std::string appSteadyRate = "50.9Kbps";
  std::string appBurstRate = "92.79Mbps";
  uint32_t appPacketSize = 1500;
  std::string appTrafficModel = "duration";
  double appBurstProb = 0.0005;
  double appInitialSendDelay = 0;
  std::string appStateInterval =
      "ns3::NormalRandomVariable[Mean=1.0|Variance=0.01|Bound=2.0]";
  std::string appSteadyTime =
      "ns3::ConstantRandomVariable[Constant=1.0]";
  std::string appBurstTime =
      "ns3::ConstantRandomVariable[Constant=0.0001]";
  int64_t appStreamBase = 100;
  uint16_t wifiChannelWidthMhz = 160;
  uint16_t wifiFrequencyMhz = 5250;
  uint16_t wifiHeGuardIntervalNs = 800;
  uint16_t wifiHeMpduBufferSize = 256;
  uint32_t wifiBeMaxAmpduSize = 6500631;
  double wifiTxPowerDbm = 23.0;
  bool wifiEnableOfdma = true;
  bool wifiEnableUlOfdma = true;
  bool wifiEnableBsrp = true;
  std::string mptcpScheduler = "default";
  bool enableMptcp = true;
  bool enableMptcpDebug = false;
  bool disableIpv6 = true;
  bool debugRoutes = false;
  bool verifyDualLinks = false;
  bool checkBackboneDualTraffic = true;
  bool dumpSockets = false;
  uint32_t dumpSocketCount = 3;
  bool dumpMptcpConfig = false;
  std::string trafficProfileDir;
  std::string trafficExperiment;
  bool enablePriorityFlowMetrics = false;
  double statsStart = -1.0;
  double statsStop = -1.0;
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
  double nrGnbTxPower = 40.0;
  double nrUeTxPower = 23.0;
  double nrBackboneDelayMs = 0.5;

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
  cmd.AddValue ("numAps", "Number of WiFi APs", numAps);
  cmd.AddValue ("appSteadyRate", "RateDual steady-state rate", appSteadyRate);
  cmd.AddValue ("appBurstRate", "RateDual burst-state rate", appBurstRate);
  cmd.AddValue ("appPacketSize",
                "Application packet size in bytes",
                appPacketSize);
  cmd.AddValue ("appTrafficModel",
                "RateDual traffic model: duration or legacy-probability",
                appTrafficModel);
  cmd.AddValue ("appBurstProb",
                "RateDual burst probability when appTrafficModel=legacy-probability",
                appBurstProb);
  cmd.AddValue ("appInitialSendDelay",
                "Delay application payload after connect succeeds (s)",
                appInitialSendDelay);
  cmd.AddValue ("appStateInterval",
                "RateDual StateInterval random variable expression when appTrafficModel=legacy-probability",
                appStateInterval);
  cmd.AddValue ("appSteadyTime",
                "RateDual steady-state time random variable expression when appTrafficModel=duration",
                appSteadyTime);
  cmd.AddValue ("appBurstTime",
                "RateDual burst-state time random variable expression when appTrafficModel=duration",
                appBurstTime);
  cmd.AddValue ("appStreamBase",
                "First RNG stream index for RateDual application internals",
                appStreamBase);
  cmd.AddValue ("wifiChannelWidthMhz", "WiFi channel width in MHz", wifiChannelWidthMhz);
  cmd.AddValue ("wifiFrequencyMhz", "WiFi center frequency in MHz", wifiFrequencyMhz);
  cmd.AddValue ("wifiHeGuardIntervalNs",
                "WiFi HE guard interval in ns (800/1600/3200)",
                wifiHeGuardIntervalNs);
  cmd.AddValue ("wifiHeMpduBufferSize",
                "WiFi HE MPDU buffer size (64-256)",
                wifiHeMpduBufferSize);
  cmd.AddValue ("wifiBeMaxAmpduSize",
                "WiFi AC_BE max A-MPDU size in bytes",
                wifiBeMaxAmpduSize);
  cmd.AddValue ("wifiTxPowerDbm", "WiFi AP/STA TX power in dBm", wifiTxPowerDbm);
  cmd.AddValue ("wifiEnableOfdma",
                "Enable AP-side WiFi multi-user scheduler (DL OFDMA path)",
                wifiEnableOfdma);
  cmd.AddValue ("wifiEnableUlOfdma",
                "Enable WiFi UL OFDMA in the AP scheduler",
                wifiEnableUlOfdma);
  cmd.AddValue ("wifiEnableBsrp",
                "Enable WiFi BSRP for UL OFDMA scheduling",
                wifiEnableBsrp);
  cmd.AddValue ("mptcpScheduler", "MPTCP scheduler (e.g. default, roundrobin, redundant, blest, pablest)", mptcpScheduler);
  cmd.AddValue ("enableMptcp", "Enable MPTCP fullmesh on client/server", enableMptcp);
  cmd.AddValue ("enableMptcpDebug", "Enable MPTCP debug sysctl (very verbose)", enableMptcpDebug);
  cmd.AddValue ("disableIpv6", "Disable IPv6 via sysctl (reduces multicast control traffic)", disableIpv6);
  cmd.AddValue ("debugRoutes", "Dump route info for a few nodes", debugRoutes);
  cmd.AddValue ("verifyDualLinks",
                "Enable per-UE dual-link verification helpers and summaries",
                verifyDualLinks);
  cmd.AddValue ("checkBackboneDualTraffic",
                "Count TCP payload on server backbone by source IP to verify per-UE NR/WiFi traffic",
                checkBackboneDualTraffic);
  cmd.AddValue ("dumpSockets",
                "Run `ss -tin` in DCE near the end of the simulation",
                dumpSockets);
  cmd.AddValue ("dumpSocketCount",
                "Number of UEs to run `ss -tin` on when dumpSockets=1",
                dumpSocketCount);
  cmd.AddValue ("dumpMptcpConfig",
                "Print selected MPTCP scheduler/path manager via sysctl",
                dumpMptcpConfig);
  cmd.AddValue ("trafficProfileDir",
                "Directory with templates.csv and groups.csv/clients.csv",
                trafficProfileDir);
  cmd.AddValue ("trafficExperiment",
                "Deprecated. Each trafficProfileDir is now a single scenario directory.",
                trafficExperiment);
  cmd.AddValue ("enablePriorityFlowMetrics",
                "Enable PacketSink SeqTsSizeHeader-based latency/jitter summaries",
                enablePriorityFlowMetrics);
  cmd.AddValue ("statsStart",
                "Stable-window statistics start time in seconds (-1 means auto)",
                statsStart);
  cmd.AddValue ("statsStop",
                "Stable-window statistics stop time in seconds (-1 means simTime)",
                statsStop);
  cmd.AddValue ("nrFrequency", "NR carrier frequency in Hz", nrFrequency);
  cmd.AddValue ("nrBandwidth", "NR carrier bandwidth in Hz", nrBandwidth);
  cmd.AddValue ("nrNumerology", "NR numerology", nrNumerology);
  cmd.AddValue ("nrTddPattern", "NR TDD pattern, e.g. DL|F|UL|UL|UL|", nrTddPattern);
  cmd.AddValue ("nrUseEesmT2",
                "Use NrEesmCcT2 (Table2, up to 256QAM) instead of the default LTE-MI error model",
                nrUseEesmT2);
  cmd.AddValue ("nrFixedMcsUl", "Fix UL MCS to nrStartingMcsUl", nrFixedMcsUl);
  cmd.AddValue ("nrFixedMcsDl", "Fix DL MCS to nrStartingMcsDl", nrFixedMcsDl);
  cmd.AddValue ("nrStartingMcsUl", "Starting (and fixed) UL MCS index", nrStartingMcsUl);
  cmd.AddValue ("nrStartingMcsDl", "Starting (and fixed) DL MCS index", nrStartingMcsDl);
  cmd.AddValue ("nrAmcModel", "NR AMC model: ErrorModel or ShannonModel", nrAmcModel);
  cmd.AddValue ("nrGnbTxPower", "gNB TX power in dBm", nrGnbTxPower);
  cmd.AddValue ("nrUeTxPower", "UE TX power in dBm", nrUeTxPower);
  cmd.AddValue ("nrBackboneDelayMs",
                "One-way PGW-to-server wired delay in ms",
                nrBackboneDelayMs);
  cmd.Parse (argc, argv);

  const TrafficAppConfig defaultTrafficApp =
      BuildDefaultTrafficAppConfig (appSteadyRate,
                                    appBurstRate,
                                    appTrafficModel,
                                    appBurstProb,
                                    appInitialSendDelay,
                                    appStateInterval,
                                    appSteadyTime,
                                    appBurstTime,
                                    appPacketSize);
  std::string scenarioId = "legacy-cli";
  std::vector<ClientTrafficConfig> clientTrafficConfigs;
  NS_ABORT_MSG_IF (!trafficExperiment.empty (),
                   "--trafficExperiment is deprecated; each trafficProfileDir is one scenario");
  if (!trafficProfileDir.empty ())
    {
      const TrafficProfileConfig trafficProfile =
          LoadTrafficProfileConfig (trafficProfileDir, defaultTrafficApp);
      clientTrafficConfigs = trafficProfile.clients;
      scenarioId = trafficProfile.scenarioId;
      numClients = clientTrafficConfigs.size ();
      enablePriorityFlowMetrics = true;
    }
  else
    {
      clientTrafficConfigs.reserve (numClients);
      for (uint32_t i = 0; i < numClients; ++i)
        {
          clientTrafficConfigs.push_back (
              MakeClientTrafficConfig (i, "legacy-cli", defaultTrafficApp));
        }
    }

  NS_ABORT_MSG_IF (numAps == 0, "numAps must be greater than zero");
  NS_ABORT_MSG_IF (wifiEnableUlOfdma && !wifiEnableOfdma,
                   "wifiEnableUlOfdma requires wifiEnableOfdma");
  NS_ABORT_MSG_IF (wifiEnableBsrp && !wifiEnableOfdma,
                   "wifiEnableBsrp requires wifiEnableOfdma");
  NS_ABORT_MSG_IF (numClients == 0, "numClients must be greater than zero");

  if (statsStart < 0.0)
    {
      statsStart = std::max (clientStart, simTime * 0.5);
    }
  if (statsStop < 0.0)
    {
      statsStop = simTime;
    }
  statsStart = std::max (0.0, statsStart);
  statsStop = std::min (simTime, statsStop);
  NS_ABORT_MSG_IF (statsStop <= statsStart,
                   "statsStop must be greater than statsStart");

  std::ostream *summaryReportStream = nullptr;
  std::string summaryReportPath;
  if (!trafficProfileDir.empty ())
    {
      summaryReportPath =
          BuildSummaryReportPath (trafficProfileDir,
                                  mptcpScheduler,
                                  clientStartJitterStream);
      EmitReportLine ("[120-demo] Summary report file: " + summaryReportPath,
                      summaryReportStream);
    }

  SimulationJsonReport jsonReport;
  jsonReport.scenarioId = scenarioId;
  jsonReport.trafficProfileDir = trafficProfileDir;
  jsonReport.reportPath = summaryReportPath;
  jsonReport.mptcpScheduler = mptcpScheduler;
  jsonReport.simTimeSeconds = simTime;
  jsonReport.sinkStartSeconds = sinkStart;
  jsonReport.clientStartSeconds = clientStart;
  jsonReport.statsStartSeconds = statsStart;
  jsonReport.statsStopSeconds = statsStop;
  jsonReport.clientStartJitterStream = clientStartJitterStream;

  const uint16_t nrMaxMcsIndex = nrUseEesmT2 ? 27 : 28;
  if (nrStartingMcsUl > nrMaxMcsIndex)
    {
      NS_LOG_UNCOND ("[120-demo] WARNING: nrStartingMcsUl(" << nrStartingMcsUl
                                                            << ") > maxMcs(" << nrMaxMcsIndex
                                                            << "); clamp");
      nrStartingMcsUl = nrMaxMcsIndex;
    }
  if (nrStartingMcsDl > nrMaxMcsIndex)
    {
      NS_LOG_UNCOND ("[120-demo] WARNING: nrStartingMcsDl(" << nrStartingMcsDl
                                                            << ") > maxMcs(" << nrMaxMcsIndex
                                                            << "); clamp");
      nrStartingMcsDl = nrMaxMcsIndex;
    }

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
      NS_ABORT_MSG ("Unsupported nrAmcModel=\"" << nrAmcModel
                                                << "\"; use ErrorModel or ShannonModel");
    }

  NS_LOG_UNCOND ("[120-demo] NR defaults: freq=" << nrFrequency / 1e9
                                                 << "GHz bw=" << nrBandwidth / 1e6
                                                 << "MHz numerology=" << nrNumerology
                                                 << " pattern=\"" << nrTddPattern << "\""
                                                 << " errorModel="
                                                 << (nrUseEesmT2
                                                         ? "NrEesmCcT2(Table2,256QAM)"
                                                         : "NrLteMiErrorModel(64QAM)")
                                                 << " amcModel=" << nrAmcModel
                                                 << " fixedMcsUl=" << nrFixedMcsUl
                                                 << " startingMcsUl=" << nrStartingMcsUl
                                                 << " fixedMcsDl=" << nrFixedMcsDl
                                                 << " startingMcsDl=" << nrStartingMcsDl
                                                 << " ueTxPower=" << nrUeTxPower
                                                 << "dBm gnbTxPower=" << nrGnbTxPower
                                                 << "dBm ueAnt=1x2 gnbAnt=2x2"
                                                 << " backboneDelay=" << nrBackboneDelayMs
                                                 << "ms");
  NS_LOG_UNCOND ("[120-demo] WiFi defaults: phy=Spectrum 802.11ax_5GHz"
                 << " width=" << wifiChannelWidthMhz
                 << "MHz freq=" << wifiFrequencyMhz
                 << "MHz heGi=" << wifiHeGuardIntervalNs
                 << "ns heMpduBuffer=" << wifiHeMpduBufferSize
                 << " beMaxAmpdu=" << wifiBeMaxAmpduSize
                 << " txPower=" << wifiTxPowerDbm
                 << "dBm apAnt=4x4 staAnt=2x2 dlOfdma="
                 << (wifiEnableOfdma ? "on" : "off")
                 << " ulOfdma="
                 << ((wifiEnableOfdma && wifiEnableUlOfdma) ? "on" : "off")
                 << " bsrp="
                 << ((wifiEnableOfdma && wifiEnableBsrp) ? "on" : "off"));
  if (!trafficProfileDir.empty ())
    {
      std::map<uint32_t, uint32_t> clientsPerPriority;
      for (const ClientTrafficConfig &client : clientTrafficConfigs)
        {
          clientsPerPriority[client.app.priority] += 1;
        }

      std::ostringstream oss;
      oss << "[120-demo] Traffic profile: dir=" << trafficProfileDir
          << " scenario=" << scenarioId
          << " statsWindow=[" << statsStart << "," << statsStop << "]s";
      for (const auto &entry : clientsPerPriority)
        {
          oss << " p" << entry.first << "=" << entry.second;
        }
      EmitReportLine (oss.str (), summaryReportStream);
    }
  else if (enablePriorityFlowMetrics)
    {
      std::ostringstream oss;
      oss << "[120-demo] Priority metrics enabled in legacy CLI mode"
          << " statsWindow=[" << statsStart << "," << statsStop << "]s";
      EmitReportLine (oss.str (), summaryReportStream);
    }

  {
    std::map<uint32_t, uint32_t> clientsPerPriority;
    for (const ClientTrafficConfig &client : clientTrafficConfigs)
      {
        clientsPerPriority[client.app.priority] += 1;
      }
    for (const auto &entry : clientsPerPriority)
      {
        ClientPriorityCount count;
        count.priority = entry.first;
        count.count = entry.second;
        jsonReport.clientCountsByPriority.push_back (count);
      }
  }

  GlobalValue::Bind ("ChecksumEnabled", BooleanValue (true));
  Config::SetDefault ("ns3::WifiRemoteStationManager::RtsCtsThreshold",
                      UintegerValue (4294967295u));
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
  apNodes.Create (numAps);

  NodeContainer serverNode;
  serverNode.Create (1);
  Ptr<Node> server = serverNode.Get (0);

  // ---------------- Mobility / placement ----------------
  const uint32_t columns = 3;
  const uint32_t rowsPerColumn = (numClients + columns - 1) / columns;
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

  // Default layout: 3 columns, filled row by row.
  uint32_t idx = 0;
  for (uint32_t row = 0; row < rowsPerColumn; ++row)
    {
      for (uint32_t col = 0; col < columns; ++col)
        {
          if (idx >= numClients)
            {
              break;
            }
          SetPosition (ueNodes.Get (idx),
                       Vector (col * colSpacing, row * rowSpacing, clientZ));
          ++idx;
        }
    }

  // Place the gNB at the grid center and spread APs along the long side.
  SetPosition (gnbNode.Get (0), Vector (centerX, centerY, infraZ));
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      const double apY = ((static_cast<double> (apIndex) + 0.5) * gridHeight) /
                         static_cast<double> (numAps);
      SetPosition (apNodes.Get (apIndex), Vector (centerX, apY, infraZ));
    }
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
                  enableMptcpDebug,
                  mptcpScheduler);
  if (enableMptcp && dumpMptcpConfig && ueNodes.GetN () > 0)
    {
      LinuxStackHelper::SysctlGet (ueNodes.Get (0),
                                   Seconds (0.2),
                                   ".net.mptcp.mptcp_scheduler",
                                   &PrintLinuxSysctl);
      LinuxStackHelper::SysctlGet (ueNodes.Get (0),
                                   Seconds (0.2),
                                   ".net.mptcp.mptcp_path_manager",
                                   &PrintLinuxSysctl);
    }

  // ---------------- WiFi networks ----------------
  // Split the 3xN client grid into row bands so each AP serves one band.
  std::vector<NodeContainer> wifiStaGroups (numAps);
  for (uint32_t n = 0; n < ueNodes.GetN (); ++n)
    {
      const uint32_t row = n / columns;
      const uint32_t apIndex =
          std::min ((row * numAps) / rowsPerColumn, numAps - 1);
      wifiStaGroups[apIndex].Add (ueNodes.Get (n));
    }
  std::ostringstream wifiSplit;
  wifiSplit << "[120-demo] WiFi STA split:";
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      wifiSplit << " ap" << (apIndex + 1) << "=" << wifiStaGroups[apIndex].GetN ();
    }
  NS_LOG_UNCOND (wifiSplit.str ());

  WifiHelper wifi;
  wifi.SetStandard (WIFI_STANDARD_80211ax_5GHZ);
  wifi.SetRemoteStationManager ("ns3::IdealWifiManager");

  auto installWifi = [&wifi,
                      wifiChannelWidthMhz,
                      wifiFrequencyMhz,
                      wifiHeGuardIntervalNs,
                      wifiHeMpduBufferSize,
                      wifiBeMaxAmpduSize,
                      wifiTxPowerDbm,
                      wifiEnableOfdma,
                      wifiEnableUlOfdma,
                      wifiEnableBsrp] (Ptr<Node> ap,
                                       NodeContainer stas,
                                       const std::string &ssidName,
                                       const std::string &subnetBase,
                                       Ipv4InterfaceContainer &apIf,
                                       Ipv4InterfaceContainer &staIf) {
    WifiMacHelper mac;
    Ssid ssid (ssidName);

    Ptr<MultiModelSpectrumChannel> spectrumChannel =
        CreateObject<MultiModelSpectrumChannel> ();
    spectrumChannel->SetAttribute ("MaxLossDb", DoubleValue (110.0));
    Ptr<FriisPropagationLossModel> wifiLossModel =
        CreateObject<FriisPropagationLossModel> ();
    wifiLossModel->SetFrequency (static_cast<double> (wifiFrequencyMhz) * 1e6);
    spectrumChannel->AddPropagationLossModel (wifiLossModel);
    spectrumChannel->SetPropagationDelayModel (
        CreateObject<ConstantSpeedPropagationDelayModel> ());
    SpectrumWifiPhyHelper phy;
    phy.SetChannel (spectrumChannel);

    // Align the default WiFi-6 capability with myscripts/dce-wifi-ofdma.
    phy.Set ("ChannelWidth", UintegerValue (wifiChannelWidthMhz));
    phy.Set ("Frequency", UintegerValue (wifiFrequencyMhz));
    phy.Set ("TxPowerStart", DoubleValue (wifiTxPowerDbm));
    phy.Set ("TxPowerEnd", DoubleValue (wifiTxPowerDbm));

    // AP: 4x4 MIMO
    phy.Set ("Antennas", UintegerValue (4));
    phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (4));
    phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (4));
    if (wifiEnableOfdma)
      {
        const uint8_t muSchedulerStations =
            static_cast<uint8_t> (std::max<uint32_t> (
                1u, std::min<uint32_t> (stas.GetN (), 74)));
        mac.SetMultiUserScheduler ("ns3::RrMultiUserScheduler",
                                   "NStations",
                                   UintegerValue (muSchedulerStations),
                                   "EnableUlOfdma",
                                   BooleanValue (wifiEnableUlOfdma),
                                   "EnableBsrp",
                                   BooleanValue (wifiEnableBsrp));
      }
    mac.SetType ("ns3::ApWifiMac",
                 "EnableBeaconJitter",
                 BooleanValue (false),
                 "Ssid",
                 SsidValue (ssid));
    NetDeviceContainer apDev = wifi.Install (phy, mac, ap);

    // STA: 2x2 MIMO
    phy.Set ("Antennas", UintegerValue (2));
    phy.Set ("MaxSupportedTxSpatialStreams", UintegerValue (2));
    phy.Set ("MaxSupportedRxSpatialStreams", UintegerValue (2));
    NetDeviceContainer staDev;
    for (uint32_t i = 0; i < stas.GetN (); ++i)
      {
        NodeContainer singleSta;
        singleSta.Add (stas.Get (i));
        mac.SetType ("ns3::StaWifiMac",
                     "Ssid", SsidValue (ssid),
                     "ActiveProbing", BooleanValue (false));
        staDev.Add (wifi.Install (phy, mac, singleSta));
      }

    auto ConfigureWifiDevices = [&] (const NetDeviceContainer &devices) {
      for (uint32_t i = 0; i < devices.GetN (); ++i)
        {
          Ptr<WifiNetDevice> dev = DynamicCast<WifiNetDevice> (devices.Get (i));
          if (!dev)
            {
              continue;
            }

          if (Ptr<HeConfiguration> he = dev->GetHeConfiguration ())
            {
              he->SetGuardInterval (NanoSeconds (wifiHeGuardIntervalNs));
              he->SetMpduBufferSize (wifiHeMpduBufferSize);
            }

          Ptr<WifiMac> wifiMac = dev->GetMac ();
          if (Ptr<RegularWifiMac> rmac = DynamicCast<RegularWifiMac> (wifiMac))
            {
              rmac->SetAttribute ("BE_MaxAmpduSize",
                                  UintegerValue (wifiBeMaxAmpduSize));
            }
        }
    };
    ConfigureWifiDevices (apDev);
    ConfigureWifiDevices (staDev);

    Ipv4AddressHelper wifiAddr;
    wifiAddr.SetBase (subnetBase.c_str (), "255.255.255.0");
    apIf = wifiAddr.Assign (apDev);
    staIf = wifiAddr.Assign (staDev);
  };

  std::vector<Ipv4InterfaceContainer> apWifiIfs (numAps);
  std::vector<Ipv4InterfaceContainer> staWifiIfs (numAps);
  std::vector<std::string> wifiSubnetCidrs;
  wifiSubnetCidrs.reserve (numAps);
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      const std::string subnetBase =
          "10." + std::to_string (10 + apIndex) + ".0.0";
      wifiSubnetCidrs.push_back (subnetBase + "/24");
      installWifi (apNodes.Get (apIndex),
                   wifiStaGroups[apIndex],
                   "wifi-ap-" + std::to_string (apIndex + 1),
                   subnetBase,
                   apWifiIfs[apIndex],
                   staWifiIfs[apIndex]);
    }

  // ---------------- NR (5G) + EPC ----------------
  Ptr<NrPointToPointEpcHelper> epcHelper = CreateObject<NrPointToPointEpcHelper> ();
  Ptr<IdealBeamformingHelper> beamformingHelper = CreateObject<IdealBeamformingHelper> ();
  Ptr<NrHelper> nrHelper = CreateObject<NrHelper> ();
  nrHelper->SetEpcHelper (epcHelper);
  nrHelper->SetBeamformingHelper (beamformingHelper);
  epcHelper->SetAttribute ("S1uLinkDelay", TimeValue (MilliSeconds (0)));

  CcBwpCreator ccBwpCreator;
  CcBwpCreator::SimpleOperationBandConf bandConf (nrFrequency,
                                                  nrBandwidth,
                                                  1,
                                                  BandwidthPartInfo::UMi_StreetCanyon_LoS);
  OperationBandInfo band = ccBwpCreator.CreateOperationBandContiguousCc (bandConf);
  nrHelper->InitializeOperationBand (&band);
  BandwidthPartInfoPtrVector allBwps = CcBwpCreator::GetAllBwps ({band});

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

  // ---------------- Wired backbone (Server <-> {PGW, APs}) ----------------
  Ptr<Node> pgw = epcHelper->GetPgwNode ();

  PointToPointHelper backboneP2p;
  backboneP2p.SetDeviceAttribute ("DataRate", StringValue ("10Gbps"));
  backboneP2p.SetChannelAttribute ("Delay",
                                   TimeValue (MilliSeconds (nrBackboneDelayMs)));

  Ipv4AddressHelper backboneAddr;
  NetDeviceContainer serverPgwDevs = backboneP2p.Install (server, pgw);
  backboneAddr.SetBase ("172.16.0.0", "255.255.255.252");
  Ipv4InterfaceContainer serverPgwIf = backboneAddr.Assign (serverPgwDevs);

  const Ipv4Address serverBackboneIp = serverPgwIf.GetAddress (0);
  const Ipv4Address pgwBackboneIp = serverPgwIf.GetAddress (1);
  const std::string serverPgwIfName = GetIfName (server, serverBackboneIp);

  std::vector<Ipv4Address> serverApBackboneIps;
  serverApBackboneIps.reserve (numAps);
  std::vector<Ipv4Address> apBackboneIps;
  apBackboneIps.reserve (numAps);
  std::vector<std::string> serverApIfNames;
  serverApIfNames.reserve (numAps);
  std::vector<std::string> apBackboneIfNames;
  apBackboneIfNames.reserve (numAps);
  std::vector<Ptr<NetDevice>> serverBackboneTraceDevs;
  serverBackboneTraceDevs.push_back (serverPgwDevs.Get (0));
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      NetDeviceContainer serverApDevs =
          backboneP2p.Install (server, apNodes.Get (apIndex));
      const std::string subnetBase =
          "172.16." + std::to_string (apIndex + 1) + ".0";
      backboneAddr.SetBase (subnetBase.c_str (), "255.255.255.252");
      Ipv4InterfaceContainer serverApIf = backboneAddr.Assign (serverApDevs);
      serverApBackboneIps.push_back (serverApIf.GetAddress (0));
      apBackboneIps.push_back (serverApIf.GetAddress (1));
      serverApIfNames.push_back (GetIfName (server, serverApIf.GetAddress (0)));
      apBackboneIfNames.push_back (
          GetIfName (apNodes.Get (apIndex), serverApIf.GetAddress (1)));
      serverBackboneTraceDevs.push_back (serverApDevs.Get (0));
    }
  BackboneTcpPayloadTracker backboneTcpTracker (Seconds (statsStart),
                                               Seconds (statsStop));
  if (checkBackboneDualTraffic || enablePriorityFlowMetrics)
    {
      for (const Ptr<NetDevice> &serverDev : serverBackboneTraceDevs)
        {
          const bool connected =
              serverDev->TraceConnectWithoutContext (
                  "MacRx",
                  MakeCallback (&BackboneTcpPayloadTracker::Rx,
                                &backboneTcpTracker));
          NS_ABORT_MSG_UNLESS (connected,
                               "Failed to connect server backbone MacRx trace");
        }
    }

  // Server routes to UE + all WiFi subnets.
  RunIpAt (server, 0.40,
           "route add 7.0.0.0/8 via " + ToString (pgwBackboneIp) + " dev " +
               serverPgwIfName);
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      RunIpAt (server,
               0.41 + (apIndex * 0.01),
               "route add " + wifiSubnetCidrs[apIndex] + " via " +
                   ToString (apBackboneIps[apIndex]) + " dev " +
                   serverApIfNames[apIndex]);
    }

  // APs advertise WiFi subnets as directly connected and route server traffic
  // over their dedicated backbone P2P links.
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      RunIpAt (apNodes.Get (apIndex),
               0.40,
               "route add " + wifiSubnetCidrs[apIndex] + " dev " +
                   GetIfName (apNodes.Get (apIndex),
                              apWifiIfs[apIndex].GetAddress (0)));
      RunIpAt (apNodes.Get (apIndex),
               0.41,
               "route add " + ToString (serverBackboneIp) + "/32 via " +
                   ToString (serverApBackboneIps[apIndex]) + " dev " +
                   apBackboneIfNames[apIndex]);
    }

  // Build per-UE WiFi config maps (address + gateway + subnet) for policy routing.
  std::map<uint32_t, WifiClientConfig> wifiCfgByNodeId;
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      for (uint32_t i = 0; i < wifiStaGroups[apIndex].GetN (); ++i)
        {
          WifiClientConfig cfg;
          cfg.addr = staWifiIfs[apIndex].GetAddress (i);
          cfg.gateway = apWifiIfs[apIndex].GetAddress (0);
          cfg.subnetCidr = wifiSubnetCidrs[apIndex];
          wifiCfgByNodeId[wifiStaGroups[apIndex].Get (i)->GetId ()] = cfg;
        }
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
  PriorityPacketSinkTracker prioritySinkTracker (Seconds (statsStart),
                                                 Seconds (statsStop));
  for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
    {
      const ClientTrafficConfig &client = clientTrafficConfigs[i];
      prioritySinkTracker.RegisterSource (ueNrIf.GetAddress (i),
                                          client.clientIndex,
                                          client.app.priority,
                                          client.templateId);

      const auto wifiIt = wifiCfgByNodeId.find (ueNodes.Get (i)->GetId ());
      if (wifiIt != wifiCfgByNodeId.end ())
        {
          prioritySinkTracker.RegisterSource (wifiIt->second.addr,
                                              client.clientIndex,
                                              client.app.priority,
                                              client.templateId);
        }
    }

  PacketSinkHelper sinkHelper ("ns3::LinuxTcpSocketFactory",
                               InetSocketAddress (Ipv4Address::GetAny (), port));
  if (enablePriorityFlowMetrics)
    {
      sinkHelper.SetAttribute ("EnableSeqTsSizeHeader", BooleanValue (true));
    }
  ApplicationContainer sinkApps = sinkHelper.Install (server);
  sinkApps.Start (Seconds (sinkStart));
  sinkApps.Stop (Seconds (simTime));
  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  uint32_t acceptedSocketsSnapshot = 0;
  bool acceptedSocketsSnapshotTaken = false;
  if (sink)
    {
      // PacketSink clears m_socketList in StopApplication(), so capture the
      // accepted socket count just before the app stops.
      Time acceptedSocketsSnapshotTime = Seconds (simTime);
      if (acceptedSocketsSnapshotTime > NanoSeconds (1))
        {
          acceptedSocketsSnapshotTime -= NanoSeconds (1);
        }
      Simulator::Schedule (acceptedSocketsSnapshotTime,
                           &SnapshotPacketSinkAcceptedSockets,
                           sink,
                           &acceptedSocketsSnapshot,
                           &acceptedSocketsSnapshotTaken);
    }
  PacketSinkRxTracker sinkRxTracker;
  if (enablePriorityFlowMetrics)
    {
      const bool connected =
          sinkApps.Get (0)->TraceConnectWithoutContext (
              "RxWithSeqTsSize",
              MakeCallback (&PriorityPacketSinkTracker::Rx, &prioritySinkTracker));
      NS_ABORT_MSG_UNLESS (connected,
                           "Failed to connect PacketSink RxWithSeqTsSize trace");
    }
  if (verifyDualLinks && !enableMptcp)
    {
      const bool connected =
          sinkApps.Get (0)->TraceConnectWithoutContext ("RxWithAddresses",
                                                        MakeCallback (&PacketSinkRxTracker::Rx,
                                                                      &sinkRxTracker));
      NS_ABORT_MSG_UNLESS (connected, "Failed to connect PacketSink RxWithAddresses trace");
    }

  Ptr<UniformRandomVariable> startJitterRng = CreateObject<UniformRandomVariable> ();
  startJitterRng->SetStream (clientStartJitterStream);
  double minStartOffset = std::numeric_limits<double>::infinity ();
  double maxStartOffset = 0.0;

  ApplicationContainer clientApps;
  int64_t nextAppStream = appStreamBase;
  for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
    {
      const ClientTrafficConfig &client = clientTrafficConfigs[i];
      const uint8_t ipTos = DscpToIpTos (PriorityToDscp (client.app.priority));
      RateDualHelper traffic ("ns3::LinuxTcpSocketFactory",
                              InetSocketAddress (serverBackboneIp, port));
      traffic.SetAttribute ("PacketSize", UintegerValue (client.app.appPacketSize));
      traffic.SetAttribute ("IpTos", UintegerValue (ipTos));
      traffic.SetAttribute ("SteadyRate",
                            DataRateValue (client.app.appSteadyRate));
      traffic.SetAttribute ("BurstRate",
                            DataRateValue (client.app.appBurstRate));
      traffic.SetAttribute ("InitialSendDelay",
                            TimeValue (Seconds (client.app.appInitialSendDelaySeconds)));
      traffic.SetAttribute ("EnableSeqTsSizeHeader",
                            BooleanValue (enablePriorityFlowMetrics));
      if (client.app.appTrafficModel == "duration")
        {
          traffic.SetAttribute ("TrafficModel",
                                EnumValue (RateDualModeApplication::TRAFFIC_MODEL_DURATION));
          traffic.SetAttribute ("SteadyTime",
                                StringValue (client.app.appSteadyTime));
          traffic.SetAttribute ("BurstTime",
                                StringValue (client.app.appBurstTime));
        }
      else if (client.app.appTrafficModel == "legacy-probability")
        {
          traffic.SetAttribute (
              "TrafficModel",
              EnumValue (RateDualModeApplication::TRAFFIC_MODEL_LEGACY_PROBABILITY));
          traffic.SetAttribute ("BurstProb", DoubleValue (client.app.appBurstProb));
          traffic.SetAttribute ("StateInterval",
                                StringValue (client.app.appStateInterval));
        }
      else
        {
          NS_ABORT_MSG ("Unsupported appTrafficModel=\""
                        << client.app.appTrafficModel
                                                         << "\"; use duration or legacy-probability");
        }

      ApplicationContainer one = traffic.Install (ueNodes.Get (i));
      const double offset =
          (clientStartJitter > 0.0) ? startJitterRng->GetValue (0.0, clientStartJitter) : 0.0;
      minStartOffset = std::min (minStartOffset, offset);
      maxStartOffset = std::max (maxStartOffset, offset);

      one.Start (Seconds (clientStart + offset));
      one.Stop (Seconds (simTime));
      clientApps.Add (one);
      nextAppStream += traffic.AssignStreams (one, nextAppStream);
    }
  if (clientStartJitter > 0.0)
    {
      jsonReport.clientStartJitter.enabled = true;
      jsonReport.clientStartJitter.configuredMaxSeconds = clientStartJitter;
      jsonReport.clientStartJitter.minOffsetSeconds = minStartOffset;
      jsonReport.clientStartJitter.maxOffsetSeconds = maxStartOffset;
      jsonReport.clientStartJitter.stream = clientStartJitterStream;

      std::ostringstream oss;
      oss << std::fixed << std::setprecision (6)
          << "[120-demo] ClientStart jitter: max=" << clientStartJitter
          << "s minOffset=" << minStartOffset
          << "s maxOffset=" << maxStartOffset << "s";
      EmitReportLine (oss.str (), summaryReportStream);
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

  if (dumpSockets)
    {
      DceApplicationHelper dce;
      dce.SetStackSize (1 << 20);
      dce.SetBinary ("ss");
      dce.ResetArguments ();
      dce.ResetEnvironment ();
      dce.AddArgument ("-tin");

      const double dumpTime = std::max (clientStart + 1.0, simTime - 0.4);
      ApplicationContainer serverSs = dce.Install (server);
      serverSs.Start (Seconds (dumpTime));
      serverSs.Stop (Seconds (simTime + 0.1));

      for (uint32_t i = 0; i < std::min (dumpSocketCount, ueNodes.GetN ()); ++i)
        {
          ApplicationContainer ueSs = dce.Install (ueNodes.Get (i));
          ueSs.Start (Seconds (dumpTime));
          ueSs.Stop (Seconds (simTime + 0.1));
        }
    }

  Simulator::Stop (Seconds (simTime + 0.5));
  Simulator::Run ();

  Time firstPayloadTxTime = Time::Max ();
  bool hasPayloadStart = false;
  for (uint32_t i = 0; i < clientApps.GetN (); ++i)
    {
      Ptr<RateDualModeApplication> app =
          DynamicCast<RateDualModeApplication> (clientApps.Get (i));
      if (!app || !app->HasSentPayload ())
        {
          continue;
        }
      firstPayloadTxTime = std::min (firstPayloadTxTime, app->GetFirstPayloadTxTime ());
      hasPayloadStart = true;
    }
  const double payloadStartSeconds =
      hasPayloadStart ? firstPayloadTxTime.GetSeconds () : simTime;
  const double payloadActiveSeconds = std::max (0.0, simTime - payloadStartSeconds);

  // ---------------- PacketSink stats (like myscripts/app-test/app-test1.cc) ----------------
  if (sink)
    {
      const uint64_t totalBytes = sink->GetTotalRx ();
      const uint32_t acceptedSockets =
          acceptedSocketsSnapshotTaken
              ? acceptedSocketsSnapshot
              : static_cast<uint32_t> (sink->GetAcceptedSockets ().size ());
      const double throughputMbps =
          (payloadActiveSeconds > 0.0)
              ? (totalBytes * 8.0 / payloadActiveSeconds / 1e6)
              : 0.0;

      jsonReport.packetSink.available = true;
      jsonReport.packetSink.totalRxBytes = totalBytes;
      jsonReport.packetSink.acceptedSockets = acceptedSockets;
      jsonReport.packetSink.payloadStartSeconds = payloadStartSeconds;
      jsonReport.packetSink.activeSeconds = payloadActiveSeconds;
      jsonReport.packetSink.throughputMbps = throughputMbps;

      std::ostringstream oss;
      oss << std::fixed << std::setprecision (3)
          << "[120-demo] PacketSink totalRxBytes=" << totalBytes
          << " acceptedSockets=" << acceptedSockets
          << " payloadStartSeconds=" << payloadStartSeconds
          << " activeSeconds=" << payloadActiveSeconds
          << " throughputMbps=" << throughputMbps;
      EmitReportLine (oss.str (), summaryReportStream);
    }
  else
    {
      EmitReportLine ("[120-demo] PacketSink stats unavailable (DynamicCast failed)",
                      summaryReportStream);
    }

  if (enablePriorityFlowMetrics)
    {
      const double priorityWindowSeconds = std::max (1e-9, statsStop - statsStart);
      jsonReport.priorityMetrics =
          prioritySinkTracker.BuildSummary (clientTrafficConfigs,
                                            scenarioId,
                                            priorityWindowSeconds,
                                            mptcpScheduler);
      ApplyPriorityPathSplitMetrics (jsonReport.priorityMetrics,
                                     clientTrafficConfigs,
                                     ueNodes,
                                     ueNrIf,
                                     wifiCfgByNodeId,
                                     backboneTcpTracker);
      PrintPriorityMetricsReport (jsonReport.priorityMetrics, summaryReportStream);
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
      if (app->HasConnected ())
        {
          clientConnected++;
        }
      if (app->IsConnectInProgress ())
        {
          clientInProgress++;
        }
      clientConnectFailures += app->GetConnectFailures ();
    }
  {
    std::ostringstream oss;
    oss << "[120-demo] Client connect summary: connected=" << clientConnected
        << "/" << clientApps.GetN ()
        << " inProgress=" << clientInProgress
        << " totalConnectFailures=" << clientConnectFailures;
    EmitReportLine (oss.str (), summaryReportStream);
  }
  jsonReport.clientConnect.connected = clientConnected;
  jsonReport.clientConnect.total = clientApps.GetN ();
  jsonReport.clientConnect.inProgress = clientInProgress;
  jsonReport.clientConnect.totalConnectFailures = clientConnectFailures;

  if (verifyDualLinks && enableMptcp)
    {
      const std::string warning =
          "[120-demo] verifyDualLinks warning: PacketSink/RxWithAddresses"
          " observes the MPTCP meta-socket peer, not per-subflow payload."
          " Use --dumpSockets=1 for real subflow verification.";
      EmitReportLine (warning, summaryReportStream);
      jsonReport.warnings.push_back (warning);
    }
  if (checkBackboneDualTraffic)
    {
      uint32_t nrPayloadActive = 0;
      uint32_t wifiPayloadActive = 0;
      uint32_t bothPayloadActive = 0;
      uint64_t totalNrBytes = 0;
      uint64_t totalWifiBytes = 0;
      std::ostringstream missingNr;
      std::ostringstream missingWifi;
      bool hasMissingNr = false;
      bool hasMissingWifi = false;

      for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
        {
          const Ptr<Node> ue = ueNodes.Get (i);
          const uint32_t nodeId = ue->GetId ();
          const auto wifiIt = wifiCfgByNodeId.find (nodeId);
          NS_ABORT_MSG_IF (wifiIt == wifiCfgByNodeId.end (),
                           "Missing WiFi config for UE while checking backbone traffic");

          const uint64_t nrBytes =
              backboneTcpTracker.GetPayloadBytes (ueNrIf.GetAddress (i));
          const uint64_t wifiBytes =
              backboneTcpTracker.GetPayloadBytes (wifiIt->second.addr);
          totalNrBytes += nrBytes;
          totalWifiBytes += wifiBytes;

          if (nrBytes > 0)
            {
              ++nrPayloadActive;
            }
          else
            {
              if (hasMissingNr)
                {
                  missingNr << ",";
                }
              missingNr << nodeId;
              hasMissingNr = true;
            }

          if (wifiBytes > 0)
            {
              ++wifiPayloadActive;
            }
          else
            {
              if (hasMissingWifi)
                {
                  missingWifi << ",";
                }
              missingWifi << nodeId;
              hasMissingWifi = true;
            }

          if (nrBytes > 0 && wifiBytes > 0)
            {
              ++bothPayloadActive;
            }
        }

      const double nrThroughputMbps =
          (statsStop > statsStart)
              ? (totalNrBytes * 8.0 / (statsStop - statsStart) / 1e6)
              : 0.0;
      const double wifiThroughputMbps =
          (statsStop > statsStart)
              ? (totalWifiBytes * 8.0 / (statsStop - statsStart) / 1e6)
              : 0.0;

      jsonReport.backboneDualTraffic.enabled = true;
      jsonReport.backboneDualTraffic.nrActive = nrPayloadActive;
      jsonReport.backboneDualTraffic.wifiActive = wifiPayloadActive;
      jsonReport.backboneDualTraffic.bothActive = bothPayloadActive;
      jsonReport.backboneDualTraffic.totalFlows = ueNodes.GetN ();
      jsonReport.backboneDualTraffic.nrPayloadBytes = totalNrBytes;
      jsonReport.backboneDualTraffic.wifiPayloadBytes = totalWifiBytes;
      jsonReport.backboneDualTraffic.nrThroughputMbps = nrThroughputMbps;
      jsonReport.backboneDualTraffic.wifiThroughputMbps = wifiThroughputMbps;

      {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision (3)
            << "[120-demo] Backbone dual-traffic summary: nrActive="
            << nrPayloadActive << "/" << ueNodes.GetN ()
            << " wifiActive=" << wifiPayloadActive << "/" << ueNodes.GetN ()
            << " bothActive=" << bothPayloadActive << "/" << ueNodes.GetN ()
            << " nrPayloadBytes=" << totalNrBytes
            << " wifiPayloadBytes=" << totalWifiBytes
            << " nrThroughputMbps=" << nrThroughputMbps
            << " wifiThroughputMbps=" << wifiThroughputMbps;
        EmitReportLine (oss.str (), summaryReportStream);
      }

      if (hasMissingNr)
        {
          jsonReport.backboneDualTraffic.missingNrNodeIds =
              ParseNodeIdList (missingNr.str ());
          EmitReportLine ("[120-demo] Backbone missing NR payload UEs: " +
                              missingNr.str (),
                          summaryReportStream);
        }
      if (hasMissingWifi)
        {
          jsonReport.backboneDualTraffic.missingWifiNodeIds =
              ParseNodeIdList (missingWifi.str ());
          EmitReportLine ("[120-demo] Backbone missing WiFi payload UEs: " +
                              missingWifi.str (),
                          summaryReportStream);
        }
    }
  else if (verifyDualLinks)
    {
      uint32_t nrPayloadActive = 0;
      uint32_t wifiPayloadActive = 0;
      uint32_t bothPayloadActive = 0;
      uint64_t minNrBytes = std::numeric_limits<uint64_t>::max ();
      uint64_t minWifiBytes = std::numeric_limits<uint64_t>::max ();
      std::ostringstream missingNr;
      std::ostringstream missingWifi;
      bool hasMissingNr = false;
      bool hasMissingWifi = false;

      for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
        {
          const Ptr<Node> ue = ueNodes.Get (i);
          const uint32_t nodeId = ue->GetId ();
          const auto wifiIt = wifiCfgByNodeId.find (nodeId);
          NS_ABORT_MSG_IF (wifiIt == wifiCfgByNodeId.end (),
                           "Missing WiFi config for UE while verifying dual links");

          const uint64_t nrBytes = sinkRxTracker.GetBytes (ueNrIf.GetAddress (i));
          const uint64_t wifiBytes = sinkRxTracker.GetBytes (wifiIt->second.addr);

          if (nrBytes > 0)
            {
              ++nrPayloadActive;
              minNrBytes = std::min (minNrBytes, nrBytes);
            }
          else
            {
              if (hasMissingNr)
                {
                  missingNr << ",";
                }
              missingNr << nodeId;
              hasMissingNr = true;
            }

          if (wifiBytes > 0)
            {
              ++wifiPayloadActive;
              minWifiBytes = std::min (minWifiBytes, wifiBytes);
            }
          else
            {
              if (hasMissingWifi)
                {
                  missingWifi << ",";
                }
              missingWifi << nodeId;
              hasMissingWifi = true;
            }

          if (nrBytes > 0 && wifiBytes > 0)
            {
              ++bothPayloadActive;
            }
        }

      jsonReport.dualLinkPayload.enabled = true;
      jsonReport.dualLinkPayload.nrActive = nrPayloadActive;
      jsonReport.dualLinkPayload.wifiActive = wifiPayloadActive;
      jsonReport.dualLinkPayload.bothActive = bothPayloadActive;
      jsonReport.dualLinkPayload.totalFlows = ueNodes.GetN ();
      if (nrPayloadActive > 0)
        {
          jsonReport.dualLinkPayload.hasMinNrBytes = true;
          jsonReport.dualLinkPayload.minNrBytes = minNrBytes;
        }
      if (wifiPayloadActive > 0)
        {
          jsonReport.dualLinkPayload.hasMinWifiBytes = true;
          jsonReport.dualLinkPayload.minWifiBytes = minWifiBytes;
        }

      std::ostringstream dualLinkOss;
      dualLinkOss << "[120-demo] Dual-link payload summary: nrActive="
                  << nrPayloadActive << "/" << ueNodes.GetN ()
                  << " wifiActive=" << wifiPayloadActive << "/"
                  << ueNodes.GetN ()
                  << " bothActive=" << bothPayloadActive << "/"
                  << ueNodes.GetN ();
      if (nrPayloadActive > 0)
        {
          dualLinkOss << " minNrBytes=" << minNrBytes;
        }
      if (wifiPayloadActive > 0)
        {
          dualLinkOss << " minWifiBytes=" << minWifiBytes;
        }
      EmitReportLine (dualLinkOss.str (), summaryReportStream);

      if (hasMissingNr)
        {
          jsonReport.dualLinkPayload.missingNrNodeIds =
              ParseNodeIdList (missingNr.str ());
          EmitReportLine ("[120-demo] Missing NR payload UEs: " +
                              missingNr.str (),
                          summaryReportStream);
        }
      if (hasMissingWifi)
        {
          jsonReport.dualLinkPayload.missingWifiNodeIds =
              ParseNodeIdList (missingWifi.str ());
          EmitReportLine ("[120-demo] Missing WiFi payload UEs: " +
                              missingWifi.str (),
                          summaryReportStream);
        }

    }

  if (!summaryReportPath.empty ())
    {
      WriteSimulationJsonReport (jsonReport);
    }

  Simulator::Destroy ();
  return 0;
}
