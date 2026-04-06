/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#include "pamptcp-report.h"

#include "ns3/applications-module.h"
#include "ns3/core-module.h"
#include "ns3/ethernet-header.h"
#include "ns3/ethernet-trailer.h"
#include "ns3/inet-socket-address.h"
#include "ns3/internet-module.h"
#include "ns3/llc-snap-header.h"
#include "ns3/network-module.h"
#include "ns3/tcp-header.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ns3 {
namespace pamptcp {
namespace {

std::string
JoinPath (const std::string &directory, const std::string &filename)
{
  if (directory.empty () || directory.back () == '/')
    {
      return directory + filename;
    }
  return directory + "/" + filename;
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

} // namespace

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

PriorityPacketSinkTracker::PriorityPacketSinkTracker (Time windowStart, Time windowStop)
  : m_windowStart (windowStart),
    m_windowStop (windowStop)
{
}

void
PriorityPacketSinkTracker::RegisterSource (Ipv4Address src,
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
PriorityPacketSinkTracker::Rx (Ptr<const Packet> packet,
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
PriorityPacketSinkTracker::BuildSummary (const std::vector<ClientTrafficConfig> &clients,
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

void
PacketSinkRxTracker::Rx (Ptr<const Packet> packet, const Address &from, const Address &to)
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
PacketSinkRxTracker::GetBytes (Ipv4Address src) const
{
  const auto it = m_bytesBySrc.find (src);
  return (it == m_bytesBySrc.end ()) ? 0 : it->second;
}

uint64_t
PacketSinkRxTracker::GetPackets (Ipv4Address src) const
{
  const auto it = m_packetsBySrc.find (src);
  return (it == m_packetsBySrc.end ()) ? 0 : it->second;
}

BackboneTcpPayloadTracker::BackboneTcpPayloadTracker (Time windowStart, Time windowStop)
  : m_windowStart (windowStart),
    m_windowStop (windowStop)
{
}

void
BackboneTcpPayloadTracker::Rx (Ptr<const Packet> packet)
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
BackboneTcpPayloadTracker::GetPayloadBytes (Ipv4Address src) const
{
  const auto it = m_payloadBytesBySrc.find (src);
  return (it == m_payloadBytesBySrc.end ()) ? 0 : it->second;
}

uint64_t
BackboneTcpPayloadTracker::GetPayloadPackets (Ipv4Address src) const
{
  const auto it = m_packetsBySrc.find (src);
  return (it == m_packetsBySrc.end ()) ? 0 : it->second;
}

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

} // namespace pamptcp
} // namespace ns3
