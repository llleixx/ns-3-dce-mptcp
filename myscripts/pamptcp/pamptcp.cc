/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/csma-module.h"
#include "ns3/applications-module.h"
#include "ns3/csv-reader.h"
#include "ns3/wifi-module.h"
#include "ns3/wifi-standards.h"
#include "ns3/spectrum-module.h"
#include "ns3/ethernet-header.h"
#include "ns3/ethernet-trailer.h"
#include "ns3/llc-snap-header.h"
#include "ns3/tcp-header.h"

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
#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

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

struct WifiClientConfig
{
  Ipv4Address addr;
  Ipv4Address gateway;
  std::string subnetCidr;
};

std::string
ToLowerCopy (std::string text)
{
  std::transform (text.begin (),
                  text.end (),
                  text.begin (),
                  [] (unsigned char c) { return std::tolower (c); });
  return text;
}

uint8_t
PriorityToDscp (uint32_t priority)
{
  if (priority == 1)
    {
      return 46; // EF
    }
  if (priority == 2)
    {
      return 26; // AF31
    }
  return 0; // Best effort
}

uint8_t
DscpToIpTos (uint8_t dscp)
{
  return static_cast<uint8_t> (dscp << 2);
}

double
GetDelaySlaThresholdMs (uint32_t priority)
{
  if (priority == 1)
    {
      return 20.0;
    }
  if (priority == 2)
    {
      return 40.0;
    }
  return 100.0;
}

bool
FileExists (const std::string &path)
{
  std::ifstream stream (path.c_str (), std::ios::in);
  return stream.good ();
}

std::string
JoinPath (const std::string &directory, const std::string &filename)
{
  if (directory.empty () || directory.back () == '/')
    {
      return directory + filename;
    }
  return directory + "/" + filename;
}

struct CsvNamedRow
{
  uint32_t lineNumber = 0;
  std::map<std::string, std::string> values;
};

std::vector<CsvNamedRow>
ReadNamedCsv (const std::string &path)
{
  NS_ABORT_MSG_IF (!FileExists (path), "CSV file not found: " << path);

  CsvReader csv (path);
  std::vector<std::string> header;
  std::vector<CsvNamedRow> rows;
  uint32_t lineNumber = 0;

  while (csv.FetchNextRow ())
    {
      ++lineNumber;
      if (csv.IsBlankRow ())
        {
          continue;
        }

      if (header.empty ())
        {
          header.resize (csv.ColumnCount ());
          for (uint32_t i = 0; i < header.size (); ++i)
            {
              std::string value;
              const bool ok = csv.GetValue (i, value);
              NS_ABORT_MSG_IF (!ok || value.empty (),
                               "Invalid or empty CSV header at " << path
                                                                 << ":" << lineNumber);
              header[i] = value;
            }
          continue;
        }

      CsvNamedRow row;
      row.lineNumber = lineNumber;
      for (uint32_t i = 0; i < header.size (); ++i)
        {
          std::string value;
          if (i < csv.ColumnCount ())
            {
              csv.GetValue (i, value);
            }
          row.values[header[i]] = value;
        }
      rows.push_back (row);
    }

  NS_ABORT_MSG_IF (header.empty (), "CSV header missing in " << path);
  return rows;
}

std::string
GetCsvValueOrEmpty (const CsvNamedRow &row, const std::string &column)
{
  const auto it = row.values.find (column);
  return (it == row.values.end ()) ? "" : it->second;
}

std::string
GetCsvValue (const CsvNamedRow &row, const std::string &column, const std::string &path)
{
  const std::string value = GetCsvValueOrEmpty (row, column);
  NS_ABORT_MSG_IF (value.empty (),
                   "Missing required column \"" << column << "\" at "
                                                << path << ":" << row.lineNumber);
  return value;
}

uint32_t
ParseUint32 (const std::string &text,
             const std::string &what,
             const std::string &path,
             uint32_t lineNumber)
{
  try
    {
      size_t pos = 0;
      const unsigned long value = std::stoul (text, &pos, 10);
      NS_ABORT_MSG_IF (pos != text.size (),
                       "Invalid unsigned integer for " << what << " at "
                                                       << path << ":" << lineNumber
                                                       << ": \"" << text << "\"");
      return static_cast<uint32_t> (value);
    }
  catch (const std::exception &)
    {
      NS_ABORT_MSG ("Invalid unsigned integer for " << what << " at "
                                                    << path << ":" << lineNumber
                                                    << ": \"" << text << "\"");
    }
  return 0;
}

double
ParseDouble (const std::string &text,
             const std::string &what,
             const std::string &path,
             uint32_t lineNumber)
{
  try
    {
      size_t pos = 0;
      const double value = std::stod (text, &pos);
      NS_ABORT_MSG_IF (pos != text.size (),
                       "Invalid double for " << what << " at " << path << ":"
                                             << lineNumber << ": \"" << text
                                             << "\"");
      return value;
    }
  catch (const std::exception &)
    {
      NS_ABORT_MSG ("Invalid double for " << what << " at " << path << ":"
                                          << lineNumber << ": \"" << text
                                          << "\"");
    }
  return 0.0;
}

uint32_t
GetOptionalUint32 (const CsvNamedRow &row,
                   const std::string &column,
                   const std::string &path,
                   uint32_t defaultValue)
{
  const std::string value = GetCsvValueOrEmpty (row, column);
  if (value.empty ())
    {
      return defaultValue;
    }
  return ParseUint32 (value, column, path, row.lineNumber);
}

double
GetOptionalDouble (const CsvNamedRow &row,
                   const std::string &column,
                   const std::string &path,
                   double defaultValue)
{
  const std::string value = GetCsvValueOrEmpty (row, column);
  if (value.empty ())
    {
      return defaultValue;
    }
  return ParseDouble (value, column, path, row.lineNumber);
}

std::string
GetAnyCsvValueOrEmpty (const CsvNamedRow &row,
                       std::initializer_list<std::string> columns)
{
  for (const std::string &column : columns)
    {
      const std::string value = GetCsvValueOrEmpty (row, column);
      if (!value.empty ())
        {
          return value;
        }
    }
  return "";
}

std::string
GetAnyRequiredCsvValue (const CsvNamedRow &row,
                        const std::string &path,
                        std::initializer_list<std::string> columns)
{
  const std::string value = GetAnyCsvValueOrEmpty (row, columns);
  if (!value.empty ())
    {
      return value;
    }

  std::ostringstream oss;
  bool first = true;
  for (const std::string &column : columns)
    {
      if (!first)
        {
          oss << "/";
        }
      oss << column;
      first = false;
    }
  NS_ABORT_MSG ("Missing required column \"" << oss.str () << "\" at "
                                             << path << ":" << row.lineNumber);
}

std::string
GetBaseName (const std::string &path)
{
  if (path.empty ())
    {
      return "";
    }

  std::string normalized = path;
  while (normalized.size () > 1 && normalized.back () == '/')
    {
      normalized.pop_back ();
    }

  const size_t slash = normalized.find_last_of ('/');
  return (slash == std::string::npos) ? normalized : normalized.substr (slash + 1);
}

std::vector<uint32_t>
ParseNodeIdList (const std::string &csvList)
{
  std::vector<uint32_t> nodeIds;
  std::istringstream iss (csvList);
  std::string token;
  while (std::getline (iss, token, ','))
    {
      if (!token.empty ())
        {
          nodeIds.push_back (static_cast<uint32_t> (std::stoul (token)));
        }
    }
  return nodeIds;
}

std::string
SanitizeFilenameComponent (std::string text)
{
  for (char &c : text)
    {
      const unsigned char uc = static_cast<unsigned char> (c);
      if (!std::isalnum (uc) && c != '-' && c != '_')
        {
          c = '-';
        }
    }
  return text;
}

std::string
BuildSummaryReportPath (const std::string &directory,
                        const std::string &mptcpScheduler,
                        int64_t clientStartJitterStream)
{
  std::ostringstream oss;
  oss << "pamptcp-summary-"
      << SanitizeFilenameComponent (mptcpScheduler)
      << "-stream" << clientStartJitterStream
      << ".json";
  return JoinPath (directory, oss.str ());
}

void
EmitReportLine (const std::string &line, std::ostream *reportStream)
{
  std::cout << line << std::endl;
  if (reportStream)
    {
      (*reportStream) << line << std::endl;
      reportStream->flush ();
    }
}

struct TrafficAppConfig
{
  uint32_t priority = 0;
  std::string appSteadyRateText;
  DataRate appSteadyRate;
  std::string appBurstRateText;
  DataRate appBurstRate;
  std::string appTrafficModel;
  double appBurstProb = 0.0;
  double appInitialSendDelaySeconds = 0.0;
  std::string appStateInterval;
  std::string appSteadyTime;
  std::string appBurstTime;
  uint32_t appPacketSize = 1500;
};

struct TrafficTemplateConfig
{
  std::string templateId;
  TrafficAppConfig app;
};

struct ClientTrafficConfig
{
  uint32_t clientIndex = 0;
  std::string templateId;
  TrafficAppConfig app;
};

struct TrafficProfileConfig
{
  std::string directory;
  std::string scenarioId;
  std::vector<TrafficTemplateConfig> templates;
  std::vector<ClientTrafficConfig> clients;
};

TrafficAppConfig
BuildDefaultTrafficAppConfig (const std::string &defaultSteadyRateText,
                              const std::string &defaultBurstRateText,
                              const std::string &defaultTrafficModel,
                              double defaultBurstProb,
                              double defaultInitialSendDelaySeconds,
                              const std::string &defaultStateInterval,
                              const std::string &defaultSteadyTime,
                              const std::string &defaultBurstTime,
                              uint32_t defaultPacketSize)
{
  TrafficAppConfig config;
  config.appSteadyRateText = defaultSteadyRateText;
  config.appSteadyRate = DataRate (defaultSteadyRateText);
  config.appBurstRateText = defaultBurstRateText;
  config.appBurstRate = DataRate (defaultBurstRateText);
  config.appTrafficModel = defaultTrafficModel;
  config.appBurstProb = defaultBurstProb;
  config.appInitialSendDelaySeconds = defaultInitialSendDelaySeconds;
  config.appStateInterval = defaultStateInterval;
  config.appSteadyTime = defaultSteadyTime;
  config.appBurstTime = defaultBurstTime;
  config.appPacketSize = defaultPacketSize;
  return config;
}

TrafficAppConfig
ResolveTrafficAppConfig (const CsvNamedRow &row,
                         const std::string &path,
                         const TrafficAppConfig &defaults,
                         bool requirePriority,
                         bool requireRates)
{
  TrafficAppConfig config = defaults;

  const std::string priorityText = GetAnyCsvValueOrEmpty (row, {"priority"});
  if (!priorityText.empty ())
    {
      config.priority =
          ParseUint32 (priorityText, "priority", path, row.lineNumber);
    }
  else if (requirePriority)
    {
      NS_ABORT_MSG ("Missing required column \"priority\" at "
                    << path << ":" << row.lineNumber);
    }

  const std::string steadyRateText =
      GetAnyCsvValueOrEmpty (row,
                             {"appSteadyRate", "steady_rate", "avg_rate"});
  if (!steadyRateText.empty ())
    {
      config.appSteadyRateText = steadyRateText;
      config.appSteadyRate = DataRate (steadyRateText);
    }
  else if (requireRates)
    {
      NS_ABORT_MSG ("Missing required app steady rate at "
                    << path << ":" << row.lineNumber);
    }

  const std::string burstRateText =
      GetAnyCsvValueOrEmpty (row,
                             {"appBurstRate", "burst_rate", "peak_rate"});
  if (!burstRateText.empty ())
    {
      config.appBurstRateText = burstRateText;
      config.appBurstRate = DataRate (burstRateText);
    }
  else if (requireRates)
    {
      NS_ABORT_MSG ("Missing required app burst rate at "
                    << path << ":" << row.lineNumber);
    }

  const std::string trafficModelText =
      GetAnyCsvValueOrEmpty (row, {"appTrafficModel", "traffic_model"});
  if (!trafficModelText.empty ())
    {
      config.appTrafficModel = trafficModelText;
    }

  const std::string burstProbText =
      GetAnyCsvValueOrEmpty (row, {"appBurstProb", "burst_prob"});
  if (!burstProbText.empty ())
    {
      config.appBurstProb =
          ParseDouble (burstProbText, "appBurstProb", path, row.lineNumber);
    }

  const std::string initialSendDelayText =
      GetAnyCsvValueOrEmpty (row,
                             {"appInitialSendDelay", "initial_send_delay_s"});
  if (!initialSendDelayText.empty ())
    {
      config.appInitialSendDelaySeconds =
          ParseDouble (initialSendDelayText,
                       "appInitialSendDelay",
                       path,
                       row.lineNumber);
    }

  const std::string stateIntervalText =
      GetAnyCsvValueOrEmpty (row, {"appStateInterval", "state_interval"});
  if (!stateIntervalText.empty ())
    {
      config.appStateInterval = stateIntervalText;
    }

  const std::string steadyTimeText =
      GetAnyCsvValueOrEmpty (row, {"appSteadyTime", "steady_time"});
  if (!steadyTimeText.empty ())
    {
      config.appSteadyTime = steadyTimeText;
    }

  const std::string burstTimeText =
      GetAnyCsvValueOrEmpty (row, {"appBurstTime", "burst_time"});
  if (!burstTimeText.empty ())
    {
      config.appBurstTime = burstTimeText;
    }

  const std::string packetSizeText =
      GetAnyCsvValueOrEmpty (row, {"appPacketSize", "packet_size"});
  if (!packetSizeText.empty ())
    {
      config.appPacketSize =
          ParseUint32 (packetSizeText, "appPacketSize", path, row.lineNumber);
    }

  return config;
}

ClientTrafficConfig
MakeClientTrafficConfig (uint32_t clientIndex,
                         const std::string &templateId,
                         const TrafficAppConfig &app)
{
  ClientTrafficConfig client;
  client.clientIndex = clientIndex;
  client.templateId = templateId;
  client.app = app;
  return client;
}

std::vector<TrafficTemplateConfig>
LoadTrafficTemplates (const std::string &path,
                      const TrafficAppConfig &defaultApp)
{
  const std::vector<CsvNamedRow> rows = ReadNamedCsv (path);
  std::vector<TrafficTemplateConfig> templates;
  std::map<std::string, bool> seenTemplateIds;

  for (const CsvNamedRow &row : rows)
    {
      TrafficTemplateConfig traffic;
      traffic.templateId =
          GetAnyRequiredCsvValue (row, path, {"template_id", "templateId"});
      NS_ABORT_MSG_IF (seenTemplateIds[traffic.templateId],
                       "Duplicate template_id \"" << traffic.templateId
                                                  << "\" in " << path << ":"
                                                  << row.lineNumber);
      seenTemplateIds[traffic.templateId] = true;
      traffic.app = ResolveTrafficAppConfig (row,
                                             path,
                                             defaultApp,
                                             true,
                                             true);
      templates.push_back (traffic);
    }

  NS_ABORT_MSG_IF (templates.empty (), "No traffic templates loaded from " << path);
  return templates;
}

std::map<std::string, const TrafficTemplateConfig *>
BuildTemplateMap (const std::vector<TrafficTemplateConfig> &templates)
{
  std::map<std::string, const TrafficTemplateConfig *> templateById;
  for (const TrafficTemplateConfig &traffic : templates)
    {
      templateById[traffic.templateId] = &traffic;
    }
  return templateById;
}

std::vector<ClientTrafficConfig>
LoadClientTrafficFromGroups (
    const std::string &path,
    const std::map<std::string, const TrafficTemplateConfig *> &templateById)
{
  const std::vector<CsvNamedRow> rows = ReadNamedCsv (path);
  std::vector<ClientTrafficConfig> clients;

  for (const CsvNamedRow &row : rows)
    {
      const std::string templateId =
          GetAnyRequiredCsvValue (row, path, {"template_id", "templateId"});
      const auto templateIt = templateById.find (templateId);
      NS_ABORT_MSG_IF (templateIt == templateById.end (),
                       "Unknown template_id \"" << templateId << "\" in "
                                                 << path << ":" << row.lineNumber);
      const uint32_t count =
          ParseUint32 (GetCsvValue (row, "count", path),
                       "count",
                       path,
                       row.lineNumber);
      for (uint32_t i = 0; i < count; ++i)
        {
          clients.push_back (MakeClientTrafficConfig (clients.size (),
                                                      templateIt->second->templateId,
                                                      templateIt->second->app));
        }
    }

  NS_ABORT_MSG_IF (clients.empty (), "No client assignments loaded from " << path);
  return clients;
}

std::vector<ClientTrafficConfig>
LoadClientTrafficFromManualList (
    const std::string &path,
    const std::map<std::string, const TrafficTemplateConfig *> &templateById,
    const TrafficAppConfig &defaultApp)
{
  const std::vector<CsvNamedRow> rows = ReadNamedCsv (path);
  uint32_t maxClientId = 0;

  for (const CsvNamedRow &row : rows)
    {
      const uint32_t clientId =
          ParseUint32 (GetCsvValue (row, "client_id", path),
                       "client_id",
                       path,
                       row.lineNumber);
      maxClientId = std::max (maxClientId, clientId);
    }

  NS_ABORT_MSG_IF (maxClientId == 0, "No valid client_id found in " << path);

  std::vector<ClientTrafficConfig> clients (maxClientId);
  std::vector<bool> assigned (maxClientId, false);

  for (const CsvNamedRow &row : rows)
    {
      const uint32_t clientId =
          ParseUint32 (GetCsvValue (row, "client_id", path),
                       "client_id",
                       path,
                       row.lineNumber);
      NS_ABORT_MSG_IF (clientId == 0,
                       "client_id is 1-based and must be >= 1 in " << path << ":"
                                                                   << row.lineNumber);
      const uint32_t clientIndex = clientId - 1;
      NS_ABORT_MSG_IF (assigned[clientIndex],
                       "Duplicate client_id " << clientId << " in " << path << ":"
                                              << row.lineNumber);

      const std::string templateId =
          GetAnyCsvValueOrEmpty (row, {"template_id", "templateId"});
      TrafficAppConfig appConfig = defaultApp;
      std::string resolvedTemplateId = "manual";
      if (!templateId.empty ())
        {
          const auto templateIt = templateById.find (templateId);
          NS_ABORT_MSG_IF (templateIt == templateById.end (),
                           "Unknown template_id \"" << templateId << "\" in "
                                                     << path << ":" << row.lineNumber);
          appConfig = templateIt->second->app;
          resolvedTemplateId = templateIt->second->templateId;
        }

      appConfig = ResolveTrafficAppConfig (row,
                                           path,
                                           appConfig,
                                           templateId.empty (),
                                           templateId.empty ());
      clients[clientIndex] = MakeClientTrafficConfig (clientIndex,
                                                      resolvedTemplateId,
                                                      appConfig);
      assigned[clientIndex] = true;
    }

  for (uint32_t i = 0; i < assigned.size (); ++i)
    {
      NS_ABORT_MSG_IF (!assigned[i],
                       "Missing client_id " << (i + 1)
                                             << " in manual client list " << path);
    }

  return clients;
}

TrafficProfileConfig
LoadTrafficProfileConfig (const std::string &directory,
                          const TrafficAppConfig &defaultApp)
{
  TrafficProfileConfig config;
  config.directory = directory;
  config.scenarioId = GetBaseName (directory);

  const std::string templatesPath = JoinPath (directory, "templates.csv");
  const std::string groupsPath = JoinPath (directory, "groups.csv");
  const std::string clientsPath = JoinPath (directory, "clients.csv");

  config.templates = LoadTrafficTemplates (templatesPath, defaultApp);
  const std::map<std::string, const TrafficTemplateConfig *> templateById =
      BuildTemplateMap (config.templates);

  const bool hasGroups = FileExists (groupsPath);
  const bool hasClients = FileExists (clientsPath);
  NS_ABORT_MSG_IF (hasGroups == hasClients,
                   "Traffic profile directory " << directory
                                                << " must contain exactly one of "
                                                   "groups.csv or clients.csv");
  if (hasGroups)
    {
      config.clients = LoadClientTrafficFromGroups (groupsPath, templateById);
    }
  else
    {
      config.clients =
          LoadClientTrafficFromManualList (clientsPath, templateById, defaultApp);
    }

  return config;
}

double
ComputeMean (const std::vector<double> &samples)
{
  if (samples.empty ())
    {
      return 0.0;
    }

  double sum = 0.0;
  for (double sample : samples)
    {
      sum += sample;
    }
  return sum / static_cast<double> (samples.size ());
}

double
ComputePercentile (std::vector<double> samples, double percentile)
{
  if (samples.empty ())
    {
      return 0.0;
    }

  std::sort (samples.begin (), samples.end ());
  if (samples.size () == 1)
    {
      return samples.front ();
    }

  const double clampedPercentile = std::max (0.0, std::min (percentile, 100.0));
  const double rank = (clampedPercentile / 100.0) *
                      static_cast<double> (samples.size () - 1);
  const size_t lowerIndex = static_cast<size_t> (std::floor (rank));
  const size_t upperIndex = static_cast<size_t> (std::ceil (rank));
  if (lowerIndex == upperIndex)
    {
      return samples[lowerIndex];
    }

  const double fraction = rank - static_cast<double> (lowerIndex);
  return (samples[lowerIndex] * (1.0 - fraction)) +
         (samples[upperIndex] * fraction);
}

struct DistributionSummary
{
  double mean = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double p999 = 0.0;
  double max = 0.0;
};

DistributionSummary
SummarizeDistribution (const std::vector<double> &samples)
{
  DistributionSummary summary;
  if (samples.empty ())
    {
      return summary;
    }

  summary.mean = ComputeMean (samples);
  summary.p50 = ComputePercentile (samples, 50.0);
  summary.p95 = ComputePercentile (samples, 95.0);
  summary.p99 = ComputePercentile (samples, 99.0);
  summary.p999 = ComputePercentile (samples, 99.9);
  summary.max = *std::max_element (samples.begin (), samples.end ());
  return summary;
}

struct PriorityMetricsEntry
{
  bool isOverall = false;
  uint32_t priority = 0;
  uint32_t expectedFlows = 0;
  uint32_t activeFlows = 0;
  uint64_t rxBytes = 0;
  uint64_t rxPackets = 0;
  double throughputMbps = 0.0;
  DistributionSummary delay;
  DistributionSummary jitter;
  bool hasFlowThroughputRange = false;
  double minFlowThroughputMbps = 0.0;
  double maxFlowThroughputMbps = 0.0;
  bool usesPrioritySpecificDelaySla = false;
  double delaySlaThresholdMs = 0.0;
  uint64_t delaySlaViolationPackets = 0;
  double delaySlaViolationRatePct = 0.0;
  uint64_t nrPayloadBytes = 0;
  uint64_t wifiPayloadBytes = 0;
  double nrSharePct = 0.0;
  double wifiSharePct = 0.0;
};

struct PriorityMetricsReport
{
  bool enabled = false;
  double windowStartSeconds = 0.0;
  double windowStopSeconds = 0.0;
  double durationSeconds = 0.0;
  std::string scenarioId;
  std::string scheduler;
  uint64_t unknownPackets = 0;
  uint64_t outsideWindowPackets = 0;
  std::vector<PriorityMetricsEntry> entries;
};

struct ClientPriorityCount
{
  uint32_t priority = 0;
  uint32_t count = 0;
};

struct ClientStartJitterSummary
{
  bool enabled = false;
  double configuredMaxSeconds = 0.0;
  double minOffsetSeconds = 0.0;
  double maxOffsetSeconds = 0.0;
  int64_t stream = 0;
};

struct PacketSinkSummary
{
  bool available = false;
  uint64_t totalRxBytes = 0;
  uint32_t acceptedSockets = 0;
  double payloadStartSeconds = 0.0;
  double activeSeconds = 0.0;
  double throughputMbps = 0.0;
};

struct ClientConnectSummary
{
  uint32_t connected = 0;
  uint32_t total = 0;
  uint32_t inProgress = 0;
  uint32_t totalConnectFailures = 0;
};

struct BackboneDualTrafficSummary
{
  bool enabled = false;
  uint32_t nrActive = 0;
  uint32_t wifiActive = 0;
  uint32_t bothActive = 0;
  uint32_t totalFlows = 0;
  uint64_t nrPayloadBytes = 0;
  uint64_t wifiPayloadBytes = 0;
  double nrThroughputMbps = 0.0;
  double wifiThroughputMbps = 0.0;
  std::vector<uint32_t> missingNrNodeIds;
  std::vector<uint32_t> missingWifiNodeIds;
};

struct DualLinkPayloadSummary
{
  bool enabled = false;
  uint32_t nrActive = 0;
  uint32_t wifiActive = 0;
  uint32_t bothActive = 0;
  uint32_t totalFlows = 0;
  bool hasMinNrBytes = false;
  uint64_t minNrBytes = 0;
  bool hasMinWifiBytes = false;
  uint64_t minWifiBytes = 0;
  std::vector<uint32_t> missingNrNodeIds;
  std::vector<uint32_t> missingWifiNodeIds;
};

struct SimulationJsonReport
{
  std::string formatVersion = "pamptcp-summary-v2";
  std::string scenarioId;
  std::string trafficProfileDir;
  std::string reportPath;
  std::string mptcpScheduler;
  double simTimeSeconds = 0.0;
  double sinkStartSeconds = 0.0;
  double clientStartSeconds = 0.0;
  double statsStartSeconds = 0.0;
  double statsStopSeconds = 0.0;
  int64_t clientStartJitterStream = 0;
  ClientStartJitterSummary clientStartJitter;
  std::vector<ClientPriorityCount> clientCountsByPriority;
  PacketSinkSummary packetSink;
  PriorityMetricsReport priorityMetrics;
  ClientConnectSummary clientConnect;
  BackboneDualTrafficSummary backboneDualTraffic;
  DualLinkPayloadSummary dualLinkPayload;
  std::vector<std::string> warnings;
};

std::string
JsonEscape (const std::string &text)
{
  std::ostringstream oss;
  for (char c : text)
    {
      switch (c)
        {
        case '\"':
          oss << "\\\"";
          break;
        case '\\':
          oss << "\\\\";
          break;
        case '\b':
          oss << "\\b";
          break;
        case '\f':
          oss << "\\f";
          break;
        case '\n':
          oss << "\\n";
          break;
        case '\r':
          oss << "\\r";
          break;
        case '\t':
          oss << "\\t";
          break;
        default:
          if (static_cast<unsigned char> (c) < 0x20)
            {
              oss << "\\u"
                  << std::hex << std::setw (4) << std::setfill ('0')
                  << static_cast<int> (static_cast<unsigned char> (c))
                  << std::dec << std::setfill (' ');
            }
          else
            {
              oss << c;
            }
          break;
        }
    }
  return oss.str ();
}

void
WriteIndent (std::ostream &os, uint32_t level)
{
  for (uint32_t i = 0; i < level; ++i)
    {
      os << "  ";
    }
}

void
WriteJsonStringValue (std::ostream &os, const std::string &value)
{
  os << "\"" << JsonEscape (value) << "\"";
}

void
WriteUint32ArrayJson (std::ostream &os,
                      const std::vector<uint32_t> &values,
                      uint32_t indentLevel)
{
  os << "[\n";
  for (size_t i = 0; i < values.size (); ++i)
    {
      WriteIndent (os, indentLevel + 1);
      os << values[i];
      if (i + 1 != values.size ())
        {
          os << ",";
        }
      os << "\n";
    }
  WriteIndent (os, indentLevel);
  os << "]";
}

void
WriteDistributionSummaryJson (std::ostream &os,
                              const DistributionSummary &summary,
                              uint32_t indentLevel)
{
  os << "{\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"mean\": " << std::fixed << std::setprecision (6) << summary.mean << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"p50\": " << summary.p50 << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"p95\": " << summary.p95 << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"p99\": " << summary.p99 << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"p999\": " << summary.p999 << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"max\": " << summary.max << "\n";
  WriteIndent (os, indentLevel);
  os << "}";
}

void
WritePriorityMetricsEntryJson (std::ostream &os,
                               const PriorityMetricsEntry &entry,
                               uint32_t indentLevel)
{
  os << "{\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"scope\": ";
  WriteJsonStringValue (os, entry.isOverall ? "overall" : "priority");
  os << ",\n";
  if (!entry.isOverall)
    {
      WriteIndent (os, indentLevel + 1);
      os << "\"priority\": " << entry.priority << ",\n";
    }
  WriteIndent (os, indentLevel + 1);
  os << "\"expected_flows\": " << entry.expectedFlows << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"active_flows\": " << entry.activeFlows << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"rx_packets\": " << entry.rxPackets << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"rx_bytes\": " << entry.rxBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"throughput_mbps\": " << std::fixed << std::setprecision (6)
     << entry.throughputMbps << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"delay_ms\": ";
  WriteDistributionSummaryJson (os, entry.delay, indentLevel + 1);
  os << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"jitter_ms\": ";
  WriteDistributionSummaryJson (os, entry.jitter, indentLevel + 1);
  os << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"has_flow_throughput_range\": "
     << (entry.hasFlowThroughputRange ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"min_flow_throughput_mbps\": " << entry.minFlowThroughputMbps << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"max_flow_throughput_mbps\": " << entry.maxFlowThroughputMbps << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"uses_priority_specific_delay_sla\": "
     << (entry.usesPrioritySpecificDelaySla ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"delay_sla_threshold_ms\": " << entry.delaySlaThresholdMs << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"delay_sla_violation_packets\": " << entry.delaySlaViolationPackets << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"delay_sla_violation_rate_pct\": " << entry.delaySlaViolationRatePct << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"nr_payload_bytes\": " << entry.nrPayloadBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"wifi_payload_bytes\": " << entry.wifiPayloadBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"nr_share_pct\": " << entry.nrSharePct << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"wifi_share_pct\": " << entry.wifiSharePct << "\n";
  WriteIndent (os, indentLevel);
  os << "}";
}

void
WritePriorityMetricsReportJson (std::ostream &os,
                                const PriorityMetricsReport &report,
                                uint32_t indentLevel)
{
  os << "{\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"enabled\": " << (report.enabled ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"window_start_seconds\": " << std::fixed << std::setprecision (6)
     << report.windowStartSeconds << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"window_stop_seconds\": " << report.windowStopSeconds << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"duration_seconds\": " << report.durationSeconds << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"scenario_id\": ";
  WriteJsonStringValue (os, report.scenarioId);
  os << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"scheduler\": ";
  WriteJsonStringValue (os, report.scheduler);
  os << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"unknown_packets\": " << report.unknownPackets << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"outside_window_packets\": " << report.outsideWindowPackets << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"entries\": [\n";
  for (size_t i = 0; i < report.entries.size (); ++i)
    {
      WriteIndent (os, indentLevel + 2);
      WritePriorityMetricsEntryJson (os, report.entries[i], indentLevel + 2);
      if (i + 1 != report.entries.size ())
        {
          os << ",";
        }
      os << "\n";
    }
  WriteIndent (os, indentLevel + 1);
  os << "]\n";
  WriteIndent (os, indentLevel);
  os << "}";
}

void
WriteBackboneDualTrafficSummaryJson (std::ostream &os,
                                     const BackboneDualTrafficSummary &summary,
                                     uint32_t indentLevel)
{
  os << "{\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"enabled\": " << (summary.enabled ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"nr_active\": " << summary.nrActive << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"wifi_active\": " << summary.wifiActive << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"both_active\": " << summary.bothActive << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"total_flows\": " << summary.totalFlows << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"nr_payload_bytes\": " << summary.nrPayloadBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"wifi_payload_bytes\": " << summary.wifiPayloadBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"nr_throughput_mbps\": " << std::fixed << std::setprecision (6)
     << summary.nrThroughputMbps << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"wifi_throughput_mbps\": " << summary.wifiThroughputMbps << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"missing_nr_node_ids\": ";
  WriteUint32ArrayJson (os, summary.missingNrNodeIds, indentLevel + 1);
  os << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"missing_wifi_node_ids\": ";
  WriteUint32ArrayJson (os, summary.missingWifiNodeIds, indentLevel + 1);
  os << "\n";
  WriteIndent (os, indentLevel);
  os << "}";
}

void
WriteDualLinkPayloadSummaryJson (std::ostream &os,
                                 const DualLinkPayloadSummary &summary,
                                 uint32_t indentLevel)
{
  os << "{\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"enabled\": " << (summary.enabled ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"nr_active\": " << summary.nrActive << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"wifi_active\": " << summary.wifiActive << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"both_active\": " << summary.bothActive << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"total_flows\": " << summary.totalFlows << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"has_min_nr_bytes\": " << (summary.hasMinNrBytes ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"min_nr_bytes\": " << summary.minNrBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"has_min_wifi_bytes\": " << (summary.hasMinWifiBytes ? "true" : "false") << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"min_wifi_bytes\": " << summary.minWifiBytes << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"missing_nr_node_ids\": ";
  WriteUint32ArrayJson (os, summary.missingNrNodeIds, indentLevel + 1);
  os << ",\n";
  WriteIndent (os, indentLevel + 1);
  os << "\"missing_wifi_node_ids\": ";
  WriteUint32ArrayJson (os, summary.missingWifiNodeIds, indentLevel + 1);
  os << "\n";
  WriteIndent (os, indentLevel);
  os << "}";
}

void
WriteSimulationJsonReport (const SimulationJsonReport &report)
{
  std::ofstream os (report.reportPath.c_str (), std::ios::out | std::ios::trunc);
  NS_ABORT_MSG_IF (!os.is_open (), "Failed to open JSON summary report file: "
                                       << report.reportPath);

  os << "{\n";
  WriteIndent (os, 1);
  os << "\"format_version\": ";
  WriteJsonStringValue (os, report.formatVersion);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"scenario_id\": ";
  WriteJsonStringValue (os, report.scenarioId);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"traffic_profile_dir\": ";
  WriteJsonStringValue (os, report.trafficProfileDir);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"report_path\": ";
  WriteJsonStringValue (os, report.reportPath);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"mptcp_scheduler\": ";
  WriteJsonStringValue (os, report.mptcpScheduler);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"sim_time_seconds\": " << std::fixed << std::setprecision (6)
     << report.simTimeSeconds << ",\n";
  WriteIndent (os, 1);
  os << "\"sink_start_seconds\": " << report.sinkStartSeconds << ",\n";
  WriteIndent (os, 1);
  os << "\"client_start_seconds\": " << report.clientStartSeconds << ",\n";
  WriteIndent (os, 1);
  os << "\"stats_start_seconds\": " << report.statsStartSeconds << ",\n";
  WriteIndent (os, 1);
  os << "\"stats_stop_seconds\": " << report.statsStopSeconds << ",\n";
  WriteIndent (os, 1);
  os << "\"client_start_jitter_stream\": " << report.clientStartJitterStream << ",\n";
  WriteIndent (os, 1);
  os << "\"client_start_jitter\": {\n";
  WriteIndent (os, 2);
  os << "\"enabled\": " << (report.clientStartJitter.enabled ? "true" : "false") << ",\n";
  WriteIndent (os, 2);
  os << "\"configured_max_seconds\": " << report.clientStartJitter.configuredMaxSeconds << ",\n";
  WriteIndent (os, 2);
  os << "\"min_offset_seconds\": " << report.clientStartJitter.minOffsetSeconds << ",\n";
  WriteIndent (os, 2);
  os << "\"max_offset_seconds\": " << report.clientStartJitter.maxOffsetSeconds << ",\n";
  WriteIndent (os, 2);
  os << "\"stream\": " << report.clientStartJitter.stream << "\n";
  WriteIndent (os, 1);
  os << "},\n";
  WriteIndent (os, 1);
  os << "\"client_counts_by_priority\": [\n";
  for (size_t i = 0; i < report.clientCountsByPriority.size (); ++i)
    {
      const ClientPriorityCount &count = report.clientCountsByPriority[i];
      WriteIndent (os, 2);
      os << "{ \"priority\": " << count.priority
         << ", \"count\": " << count.count << " }";
      if (i + 1 != report.clientCountsByPriority.size ())
        {
          os << ",";
        }
      os << "\n";
    }
  WriteIndent (os, 1);
  os << "],\n";
  WriteIndent (os, 1);
  os << "\"packet_sink\": {\n";
  WriteIndent (os, 2);
  os << "\"available\": " << (report.packetSink.available ? "true" : "false") << ",\n";
  WriteIndent (os, 2);
  os << "\"total_rx_bytes\": " << report.packetSink.totalRxBytes << ",\n";
  WriteIndent (os, 2);
  os << "\"accepted_sockets\": " << report.packetSink.acceptedSockets << ",\n";
  WriteIndent (os, 2);
  os << "\"payload_start_seconds\": " << report.packetSink.payloadStartSeconds << ",\n";
  WriteIndent (os, 2);
  os << "\"active_seconds\": " << report.packetSink.activeSeconds << ",\n";
  WriteIndent (os, 2);
  os << "\"throughput_mbps\": " << report.packetSink.throughputMbps << "\n";
  WriteIndent (os, 1);
  os << "},\n";
  WriteIndent (os, 1);
  os << "\"priority_metrics\": ";
  WritePriorityMetricsReportJson (os, report.priorityMetrics, 1);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"client_connect\": {\n";
  WriteIndent (os, 2);
  os << "\"connected\": " << report.clientConnect.connected << ",\n";
  WriteIndent (os, 2);
  os << "\"total\": " << report.clientConnect.total << ",\n";
  WriteIndent (os, 2);
  os << "\"in_progress\": " << report.clientConnect.inProgress << ",\n";
  WriteIndent (os, 2);
  os << "\"total_connect_failures\": " << report.clientConnect.totalConnectFailures << "\n";
  WriteIndent (os, 1);
  os << "},\n";
  WriteIndent (os, 1);
  os << "\"backbone_dual_traffic\": ";
  WriteBackboneDualTrafficSummaryJson (os, report.backboneDualTraffic, 1);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"dual_link_payload\": ";
  WriteDualLinkPayloadSummaryJson (os, report.dualLinkPayload, 1);
  os << ",\n";
  WriteIndent (os, 1);
  os << "\"warnings\": [\n";
  for (size_t i = 0; i < report.warnings.size (); ++i)
    {
      WriteIndent (os, 2);
      WriteJsonStringValue (os, report.warnings[i]);
      if (i + 1 != report.warnings.size ())
        {
          os << ",";
        }
      os << "\n";
    }
  WriteIndent (os, 1);
  os << "]\n";
  os << "}\n";
}

struct ClientWindowStats
{
  uint64_t rxBytes = 0;
  uint64_t rxPackets = 0;
  uint64_t delaySlaViolationPackets = 0;
  std::vector<double> delaySamplesMs;
  std::vector<double> jitterSamplesMs;
  double lastDelayMs = 0.0;
  bool hasLastDelay = false;
};

struct PriorityAggregateStats
{
  uint32_t expectedFlows = 0;
  uint32_t activeFlows = 0;
  uint64_t rxBytes = 0;
  uint64_t rxPackets = 0;
  uint64_t delaySlaViolationPackets = 0;
  std::vector<double> delaySamplesMs;
  std::vector<double> jitterSamplesMs;
  double minFlowThroughputMbps = std::numeric_limits<double>::max ();
  double maxFlowThroughputMbps = 0.0;
};

class PriorityPacketSinkTracker
{
public:
  PriorityPacketSinkTracker (Time windowStart, Time windowStop)
    : m_windowStart (windowStart),
      m_windowStop (windowStop)
  {
  }

  void
  RegisterSource (Ipv4Address src,
                  uint32_t clientIndex,
                  uint32_t priority,
                  const std::string &templateId)
  {
    Binding binding;
    binding.clientIndex = clientIndex;
    binding.priority = priority;
    binding.templateId = templateId;
    m_bindingBySource[src] = binding;

    if (clientIndex >= m_clientStats.size ())
      {
        m_clientStats.resize (clientIndex + 1);
      }
  }

  void
  Rx (Ptr<const Packet> packet,
      const Address &from,
      const Address &to,
      const SeqTsSizeHeader &header)
  {
    (void) to;
    (void) packet;

    if (!InetSocketAddress::IsMatchingType (from))
      {
        return;
      }

    const Ipv4Address src = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
    const auto bindingIt = m_bindingBySource.find (src);
    if (bindingIt == m_bindingBySource.end ())
      {
        ++m_unknownPackets;
        return;
      }

    const Time now = Simulator::Now ();
    if (now < m_windowStart || now > m_windowStop)
      {
        ++m_outsideWindowPackets;
        return;
      }

    ClientWindowStats &stats = m_clientStats[bindingIt->second.clientIndex];
    const double delayMs = (now - header.GetTs ()).GetSeconds () * 1000.0;
    stats.rxBytes += header.GetSize ();
    stats.rxPackets += 1;
    if (delayMs > GetDelaySlaThresholdMs (bindingIt->second.priority))
      {
        stats.delaySlaViolationPackets += 1;
      }
    stats.delaySamplesMs.push_back (delayMs);

    if (stats.hasLastDelay)
      {
        stats.jitterSamplesMs.push_back (std::fabs (delayMs - stats.lastDelayMs));
      }
    stats.lastDelayMs = delayMs;
    stats.hasLastDelay = true;
  }

  PriorityMetricsReport
  BuildSummary (const std::vector<ClientTrafficConfig> &clients,
                const std::string &scenarioId,
                double windowSeconds,
                const std::string &mptcpScheduler) const
  {
    PriorityMetricsReport report;
    report.enabled = true;
    report.windowStartSeconds = m_windowStart.GetSeconds ();
    report.windowStopSeconds = m_windowStop.GetSeconds ();
    report.durationSeconds = windowSeconds;
    report.scenarioId = scenarioId;
    report.scheduler = mptcpScheduler;

    std::map<uint32_t, PriorityAggregateStats> aggregates;
    PriorityAggregateStats overall;

    for (const ClientTrafficConfig &client : clients)
      {
        aggregates[client.app.priority].expectedFlows += 1;
        overall.expectedFlows += 1;
      }

    for (uint32_t i = 0; i < clients.size (); ++i)
      {
        const ClientTrafficConfig &client = clients[i];
        const ClientWindowStats &stats = m_clientStats[i];
        PriorityAggregateStats &aggregate = aggregates[client.app.priority];

        if (stats.rxPackets == 0)
          {
            continue;
          }

        const double flowThroughputMbps =
            (windowSeconds > 0.0)
                ? (static_cast<double> (stats.rxBytes) * 8.0 / windowSeconds / 1e6)
                : 0.0;
        aggregate.activeFlows += 1;
        aggregate.rxBytes += stats.rxBytes;
        aggregate.rxPackets += stats.rxPackets;
        aggregate.delaySlaViolationPackets += stats.delaySlaViolationPackets;
        aggregate.delaySamplesMs.insert (aggregate.delaySamplesMs.end (),
                                         stats.delaySamplesMs.begin (),
                                         stats.delaySamplesMs.end ());
        aggregate.jitterSamplesMs.insert (aggregate.jitterSamplesMs.end (),
                                          stats.jitterSamplesMs.begin (),
                                          stats.jitterSamplesMs.end ());
        aggregate.minFlowThroughputMbps =
            std::min (aggregate.minFlowThroughputMbps, flowThroughputMbps);
        aggregate.maxFlowThroughputMbps =
            std::max (aggregate.maxFlowThroughputMbps, flowThroughputMbps);

        overall.activeFlows += 1;
        overall.rxBytes += stats.rxBytes;
        overall.rxPackets += stats.rxPackets;
        overall.delaySlaViolationPackets += stats.delaySlaViolationPackets;
        overall.delaySamplesMs.insert (overall.delaySamplesMs.end (),
                                       stats.delaySamplesMs.begin (),
                                       stats.delaySamplesMs.end ());
        overall.jitterSamplesMs.insert (overall.jitterSamplesMs.end (),
                                        stats.jitterSamplesMs.begin (),
                                        stats.jitterSamplesMs.end ());
        overall.minFlowThroughputMbps =
            std::min (overall.minFlowThroughputMbps, flowThroughputMbps);
        overall.maxFlowThroughputMbps =
            std::max (overall.maxFlowThroughputMbps, flowThroughputMbps);
      }

    auto AppendAggregate = [&] (bool isOverall,
                                uint32_t priority,
                                const PriorityAggregateStats &aggregate) {
      PriorityMetricsEntry entry;
      entry.isOverall = isOverall;
      entry.priority = priority;
      entry.expectedFlows = aggregate.expectedFlows;
      entry.activeFlows = aggregate.activeFlows;
      entry.rxBytes = aggregate.rxBytes;
      entry.rxPackets = aggregate.rxPackets;
      entry.throughputMbps =
          (windowSeconds > 0.0)
              ? (static_cast<double> (aggregate.rxBytes) * 8.0 / windowSeconds / 1e6)
              : 0.0;
      entry.delay = SummarizeDistribution (aggregate.delaySamplesMs);
      entry.jitter = SummarizeDistribution (aggregate.jitterSamplesMs);
      entry.hasFlowThroughputRange = (aggregate.activeFlows > 0);
      entry.usesPrioritySpecificDelaySla = isOverall;
      if (!isOverall)
        {
          entry.delaySlaThresholdMs = GetDelaySlaThresholdMs (priority);
        }
      entry.delaySlaViolationPackets = aggregate.delaySlaViolationPackets;
      entry.delaySlaViolationRatePct =
          (aggregate.rxPackets > 0)
              ? (100.0 * static_cast<double> (aggregate.delaySlaViolationPackets) /
                 static_cast<double> (aggregate.rxPackets))
              : 0.0;
      if (entry.hasFlowThroughputRange)
        {
          entry.minFlowThroughputMbps = aggregate.minFlowThroughputMbps;
          entry.maxFlowThroughputMbps = aggregate.maxFlowThroughputMbps;
        }
      report.entries.push_back (entry);
    };

    AppendAggregate (true, 0, overall);
    for (const auto &entry : aggregates)
      {
        AppendAggregate (false, entry.first, entry.second);
      }

    report.unknownPackets = m_unknownPackets;
    report.outsideWindowPackets = m_outsideWindowPackets;
    return report;
  }

private:
  struct Binding
  {
    uint32_t clientIndex = 0;
    uint32_t priority = 0;
    std::string templateId;
  };

  std::map<Ipv4Address, Binding> m_bindingBySource;
  std::vector<ClientWindowStats> m_clientStats;
  Time m_windowStart = Seconds (0);
  Time m_windowStop = Seconds (0);
  uint64_t m_unknownPackets = 0;
  uint64_t m_outsideWindowPackets = 0;
};

void
PrintPriorityMetricsReport (const PriorityMetricsReport &report,
                            std::ostream *reportStream)
{
  {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision (3)
        << "[120-demo] Priority metrics window: start="
        << report.windowStartSeconds
        << "s stop=" << report.windowStopSeconds
        << "s duration=" << report.durationSeconds
        << "s scenario=" << report.scenarioId
        << " scheduler=" << report.scheduler;
    EmitReportLine (oss.str (), reportStream);
  }

  for (const PriorityMetricsEntry &entry : report.entries)
    {
      std::ostringstream oss;
      oss << std::fixed << std::setprecision (3)
          << "[120-demo] Priority metrics";
      if (!entry.isOverall)
        {
          oss << " priority=" << entry.priority;
        }
      oss << " activeFlows=" << entry.activeFlows << "/"
          << entry.expectedFlows
          << " rxPackets=" << entry.rxPackets
          << " rxBytes=" << entry.rxBytes
          << " throughputMbps=" << entry.throughputMbps
          << " meanDelayMs=" << entry.delay.mean
          << " p50DelayMs=" << entry.delay.p50
          << " p95DelayMs=" << entry.delay.p95
          << " p99DelayMs=" << entry.delay.p99
          << " p999DelayMs=" << entry.delay.p999
          << " maxDelayMs=" << entry.delay.max
          << " meanJitterMs=" << entry.jitter.mean
          << " p95JitterMs=" << entry.jitter.p95
          << " p99JitterMs=" << entry.jitter.p99
          << " p999JitterMs=" << entry.jitter.p999
          << " maxJitterMs=" << entry.jitter.max;
      if (entry.hasFlowThroughputRange)
        {
          oss << " minFlowThroughputMbps="
              << entry.minFlowThroughputMbps
              << " maxFlowThroughputMbps="
              << entry.maxFlowThroughputMbps;
        }
      if (entry.usesPrioritySpecificDelaySla)
        {
          oss << " slaThresholdMs=priority-specific";
        }
      else
        {
          oss << " slaThresholdMs=" << entry.delaySlaThresholdMs;
        }
      oss << " slaViolationPackets=" << entry.delaySlaViolationPackets
          << " slaViolationRatePct=" << entry.delaySlaViolationRatePct
          << " nrPayloadBytes=" << entry.nrPayloadBytes
          << " wifiPayloadBytes=" << entry.wifiPayloadBytes
          << " nrSharePct=" << entry.nrSharePct
          << " wifiSharePct=" << entry.wifiSharePct;
      EmitReportLine (oss.str (), reportStream);
    }

  if (report.unknownPackets > 0 || report.outsideWindowPackets > 0)
    {
      std::ostringstream oss;
      oss << "[120-demo] Priority metrics note: unknownPackets="
          << report.unknownPackets
          << " outsideWindowPackets=" << report.outsideWindowPackets;
      EmitReportLine (oss.str (), reportStream);
    }
}

class BackboneTcpPayloadTracker;

void
ApplyPriorityPathSplitMetrics (PriorityMetricsReport &report,
                               const std::vector<ClientTrafficConfig> &clients,
                               const NodeContainer &ueNodes,
                               const Ipv4InterfaceContainer &ueNrIf,
                               const std::map<uint32_t, WifiClientConfig> &wifiCfgByNodeId,
                               const BackboneTcpPayloadTracker &backboneTracker);

class PacketSinkRxTracker
{
public:
  void
  Rx (Ptr<const Packet> packet, const Address &from, const Address &to)
  {
    (void) to;

    if (!InetSocketAddress::IsMatchingType (from))
      {
        return;
      }

    const Ipv4Address src = InetSocketAddress::ConvertFrom (from).GetIpv4 ();
    m_bytesBySrc[src] += packet->GetSize ();
    m_packetsBySrc[src] += 1;
  }

  uint64_t
  GetBytes (Ipv4Address src) const
  {
    const auto it = m_bytesBySrc.find (src);
    return (it == m_bytesBySrc.end ()) ? 0 : it->second;
  }

  uint64_t
  GetPackets (Ipv4Address src) const
  {
    const auto it = m_packetsBySrc.find (src);
    return (it == m_packetsBySrc.end ()) ? 0 : it->second;
  }

private:
  std::map<Ipv4Address, uint64_t> m_bytesBySrc;
  std::map<Ipv4Address, uint64_t> m_packetsBySrc;
};

class BackboneTcpPayloadTracker
{
public:
  BackboneTcpPayloadTracker (Time windowStart, Time windowStop)
    : m_windowStart (windowStart),
      m_windowStop (windowStop)
  {
  }

  void
  Rx (Ptr<const Packet> packet)
  {
    const Time now = Simulator::Now ();
    if (now < m_windowStart || now > m_windowStop)
      {
        return;
      }

    Ptr<Packet> copy = packet->Copy ();

    EthernetTrailer trailer;
    copy->RemoveTrailer (trailer);

    EthernetHeader ethernet (false);
    copy->RemoveHeader (ethernet);

    uint16_t protocol = ethernet.GetLengthType ();
    if (protocol <= 1500)
      {
        if (copy->GetSize () < protocol)
          {
            return;
          }
        const uint32_t padlen = copy->GetSize () - protocol;
        if (padlen > 0)
          {
            copy->RemoveAtEnd (padlen);
          }

        LlcSnapHeader llc;
        copy->RemoveHeader (llc);
        protocol = llc.GetType ();
      }

    static const uint16_t kIpv4Protocol = 0x0800;
    if (protocol != kIpv4Protocol)
      {
        return;
      }

    Ipv4Header ipv4;
    copy->RemoveHeader (ipv4);
    if (ipv4.GetProtocol () != 6)
      {
        return;
      }

    const Ipv4Address src = ipv4.GetSource ();

    TcpHeader tcp;
    copy->RemoveHeader (tcp);
    const uint32_t payloadBytes = copy->GetSize ();
    if (payloadBytes == 0)
      {
        return;
      }

    m_payloadBytesBySrc[src] += payloadBytes;
    m_packetsBySrc[src] += 1;
  }

  uint64_t
  GetPayloadBytes (Ipv4Address src) const
  {
    const auto it = m_payloadBytesBySrc.find (src);
    return (it == m_payloadBytesBySrc.end ()) ? 0 : it->second;
  }

  uint64_t
  GetPayloadPackets (Ipv4Address src) const
  {
    const auto it = m_packetsBySrc.find (src);
    return (it == m_packetsBySrc.end ()) ? 0 : it->second;
  }

private:
  std::map<Ipv4Address, uint64_t> m_payloadBytesBySrc;
  std::map<Ipv4Address, uint64_t> m_packetsBySrc;
  Time m_windowStart = Seconds (0);
  Time m_windowStop = Seconds (0);
};

void
ApplyPriorityPathSplitMetrics (PriorityMetricsReport &report,
                               const std::vector<ClientTrafficConfig> &clients,
                               const NodeContainer &ueNodes,
                               const Ipv4InterfaceContainer &ueNrIf,
                               const std::map<uint32_t, WifiClientConfig> &wifiCfgByNodeId,
                               const BackboneTcpPayloadTracker &backboneTracker)
{
  struct PathSplitAggregate
  {
    uint64_t nrPayloadBytes = 0;
    uint64_t wifiPayloadBytes = 0;
  };

  std::map<uint32_t, PathSplitAggregate> aggregates;
  PathSplitAggregate overall;

  for (uint32_t i = 0; i < ueNodes.GetN (); ++i)
    {
      const ClientTrafficConfig &client = clients[i];
      const auto wifiIt = wifiCfgByNodeId.find (ueNodes.Get (i)->GetId ());
      if (wifiIt == wifiCfgByNodeId.end ())
        {
          continue;
        }

      const uint64_t nrBytes =
          backboneTracker.GetPayloadBytes (ueNrIf.GetAddress (i));
      const uint64_t wifiBytes =
          backboneTracker.GetPayloadBytes (wifiIt->second.addr);

      aggregates[client.app.priority].nrPayloadBytes += nrBytes;
      aggregates[client.app.priority].wifiPayloadBytes += wifiBytes;
      overall.nrPayloadBytes += nrBytes;
      overall.wifiPayloadBytes += wifiBytes;
    }

  for (PriorityMetricsEntry &entry : report.entries)
    {
      uint64_t nrBytes = 0;
      uint64_t wifiBytes = 0;
      if (entry.isOverall)
        {
          nrBytes = overall.nrPayloadBytes;
          wifiBytes = overall.wifiPayloadBytes;
        }
      else
        {
          const auto it = aggregates.find (entry.priority);
          if (it != aggregates.end ())
            {
              nrBytes = it->second.nrPayloadBytes;
              wifiBytes = it->second.wifiPayloadBytes;
            }
        }

      entry.nrPayloadBytes = nrBytes;
      entry.wifiPayloadBytes = wifiBytes;
      const uint64_t totalBytes = nrBytes + wifiBytes;
      if (totalBytes > 0)
        {
          entry.nrSharePct =
              100.0 * static_cast<double> (nrBytes) / static_cast<double> (totalBytes);
          entry.wifiSharePct =
              100.0 * static_cast<double> (wifiBytes) / static_cast<double> (totalBytes);
        }
    }
}

} // namespace

int
main (int argc, char *argv[])
{
  uint32_t numClients = 120;
  double simTime = 20.0;
  double sinkStart = 1.0;
  double clientStart = 2.0;
  double clientStartJitter = 0.3;
  int64_t clientStartJitterStream = 1;
  uint16_t port = 5000;
  uint32_t numAps = 4;
  std::string appSteadyRate = "50.9Kbps";
  std::string appBurstRate = "92.79Mbps";
  uint32_t appPacketSize = 1500;
  std::string appTrafficModel = "duration";
  double appBurstProb = 0.0005;
  double appInitialSendDelay = 0.2;
  std::string appStateInterval =
      "ns3::NormalRandomVariable[Mean=1.0|Variance=0.01|Bound=2.0]";
  std::string appSteadyTime =
      "ns3::ConstantRandomVariable[Constant=1.0]";
  std::string appBurstTime =
      "ns3::ConstantRandomVariable[Constant=0.0001]";
  int64_t appStreamBase = 100;
  uint32_t wifiAssocStaggerMs = 20;
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
  bool checkBackboneDualTraffic = false;
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
  uint16_t nrNumerology = 1;
  std::string nrTddPattern = "DL|F|UL|UL|UL|";
  bool nrUseEesmT2 = true;
  bool nrFixedMcsUl = true;
  bool nrFixedMcsDl = false;
  uint16_t nrStartingMcsUl = 25;
  uint16_t nrStartingMcsDl = 25;
  std::string nrAmcModel = "ErrorModel";
  double nrGnbTxPower = 40.0;
  double nrUeTxPower = 23.0;

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
  cmd.AddValue ("wifiAssocStaggerMs",
                "Additional WaitBeaconTimeout per STA to stagger WiFi association (ms)",
                wifiAssocStaggerMs);
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
                                                 << "dBm ueAnt=1x2 gnbAnt=2x2");
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
                      wifiAssocStaggerMs,
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
                     "ActiveProbing", BooleanValue (false),
                     "WaitBeaconTimeout",
                     TimeValue (MilliSeconds (120 + i * wifiAssocStaggerMs)));
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

  CsmaHelper csma;
  csma.SetChannelAttribute ("DataRate", StringValue ("10Gbps"));
  csma.SetChannelAttribute ("Delay", StringValue ("0ms"));

  NodeContainer backboneNodes;
  backboneNodes.Add (server);
  backboneNodes.Add (pgw);
  backboneNodes.Add (apNodes);

  NetDeviceContainer backboneDevs = csma.Install (backboneNodes);
  Ipv4AddressHelper backboneAddr;
  backboneAddr.SetBase ("172.16.0.0", "255.255.255.0");
  Ipv4InterfaceContainer backboneIf = backboneAddr.Assign (backboneDevs);

  const Ipv4Address serverBackboneIp = backboneIf.GetAddress (0);
  const Ipv4Address pgwBackboneIp = backboneIf.GetAddress (1);
  std::vector<Ipv4Address> apBackboneIps;
  apBackboneIps.reserve (numAps);
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      apBackboneIps.push_back (backboneIf.GetAddress (2 + apIndex));
    }

  const std::string serverBackboneIfName = GetIfName (server, serverBackboneIp);
  BackboneTcpPayloadTracker backboneTcpTracker (Seconds (statsStart),
                                               Seconds (statsStop));
  if (checkBackboneDualTraffic || enablePriorityFlowMetrics)
    {
      const bool connected =
          backboneDevs.Get (0)->TraceConnectWithoutContext ("MacRx",
                                                            MakeCallback (&BackboneTcpPayloadTracker::Rx,
                                                                          &backboneTcpTracker));
      NS_ABORT_MSG_UNLESS (connected, "Failed to connect server backbone MacRx trace");
    }

  // Server routes to UE + all WiFi subnets.
  RunIpAt (server, 0.40,
           "route add 7.0.0.0/8 via " + ToString (pgwBackboneIp) + " dev " +
               serverBackboneIfName);
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      RunIpAt (server,
               0.41 + (apIndex * 0.01),
               "route add " + wifiSubnetCidrs[apIndex] + " via " +
                   ToString (apBackboneIps[apIndex]) + " dev " +
                   serverBackboneIfName);
    }

  // APs advertise WiFi subnets as directly connected (helps routing clarity).
  for (uint32_t apIndex = 0; apIndex < numAps; ++apIndex)
    {
      RunIpAt (apNodes.Get (apIndex),
               0.40,
               "route add " + wifiSubnetCidrs[apIndex] + " dev " +
                   GetIfName (apNodes.Get (apIndex),
                              apWifiIfs[apIndex].GetAddress (0)));
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
  Ptr<PacketSink> sink = DynamicCast<PacketSink> (sinkApps.Get (0));
  if (sink)
    {
      const uint64_t totalBytes = sink->GetTotalRx ();
      const uint32_t acceptedSockets =
          static_cast<uint32_t> (sink->GetAcceptedSockets ().size ());
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
