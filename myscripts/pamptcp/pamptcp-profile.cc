/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "pamptcp-profile.h"

#include "ns3/core-module.h"
#include "ns3/csv-reader.h"

#include <exception>
#include <fstream>
#include <initializer_list>
#include <map>
#include <sstream>

namespace ns3 {
namespace pamptcp {
namespace {

struct CsvNamedRow
{
  uint32_t lineNumber = 0;
  std::map<std::string, std::string> values;
};

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
GetCsvValueOrEmptyInternal (const CsvNamedRow &row, const std::string &column)
{
  const auto it = row.values.find (column);
  return (it == row.values.end ()) ? "" : it->second;
}

std::string
GetCsvValue (const CsvNamedRow &row, const std::string &column, const std::string &path)
{
  const std::string value = GetCsvValueOrEmptyInternal (row, column);
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

std::string
GetAnyCsvValueOrEmpty (const CsvNamedRow &row,
                       std::initializer_list<std::string> columns)
{
  for (const std::string &column : columns)
    {
      const std::string value = GetCsvValueOrEmptyInternal (row, column);
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

  return config;
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

} // namespace

uint8_t
PriorityToDscp (uint32_t priority)
{
  if (priority == 1)
    {
      return 46;
    }
  if (priority == 2)
    {
      return 26;
    }
  return 0;
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

TrafficAppConfig
BuildDefaultTrafficAppConfig (const std::string &defaultSteadyRateText,
                              const std::string &defaultBurstRateText,
                              const std::string &defaultTrafficModel,
                              double defaultBurstProb,
                              double defaultInitialSendDelaySeconds,
                              const std::string &defaultStateInterval,
                              const std::string &defaultSteadyTime,
                              const std::string &defaultBurstTime)
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

} // namespace pamptcp
} // namespace ns3
