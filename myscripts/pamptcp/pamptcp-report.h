/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#ifndef PAMPTCP_REPORT_H
#define PAMPTCP_REPORT_H

#include "pamptcp-profile.h"

#include "ns3/ipv4-address.h"
#include "ns3/nstime.h"
#include "ns3/ptr.h"

#include <cstdint>
#include <iosfwd>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace ns3 {

class Address;
class Packet;
class SeqTsSizeHeader;
class NodeContainer;
class Ipv4InterfaceContainer;

namespace pamptcp {

std::string BuildSummaryReportPath (const std::string &directory,
                                    const std::string &mptcpScheduler,
                                    const std::string &pathMode,
                                    int64_t clientStartJitterStream,
                                    double simTimeSeconds);

std::vector<uint32_t> ParseNodeIdList (const std::string &csvList);

void EmitReportLine (const std::string &line, std::ostream *reportStream);

struct DistributionSummary
{
  double mean = 0.0;
  double p50 = 0.0;
  double p95 = 0.0;
  double p99 = 0.0;
  double p999 = 0.0;
  double max = 0.0;
};

struct PriorityMetricsEntry
{
  bool isOverall = false;
  uint32_t priority = 0;
  uint32_t expectedFlows = 0;
  uint32_t activeFlows = 0;
  uint32_t delayActiveFlows = 0;
  uint64_t rxBytes = 0;
  uint64_t rxPackets = 0;
  uint64_t delayRxPackets = 0;
  double throughputMbps = 0.0;
  DistributionSummary delay;
  DistributionSummary overallDelay;
  DistributionSummary jitter;
  bool hasFlowThroughputRange = false;
  double minFlowThroughputMbps = 0.0;
  double maxFlowThroughputMbps = 0.0;
  bool delayExcludesPeakFlows = true;
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

class PriorityPacketSinkTracker
{
public:
  PriorityPacketSinkTracker (Time windowStart, Time windowStop);

  void RegisterSource (Ipv4Address src,
                       uint32_t clientIndex,
                       uint32_t priority,
                       const std::string &templateId);

  void Rx (Ptr<const Packet> packet,
           const Address &from,
           const Address &to,
           const SeqTsSizeHeader &header);

  PriorityMetricsReport BuildSummary (const std::vector<ClientTrafficConfig> &clients,
                                      const std::string &scenarioId,
                                      double windowSeconds,
                                      const std::string &mptcpScheduler) const;

private:
  struct Binding
  {
    uint32_t clientIndex = 0;
    uint32_t priority = 0;
    std::string templateId;
  };

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
    struct DelayAggregateStats
    {
      uint32_t activeFlows = 0;
      uint64_t rxPackets = 0;
      uint64_t delaySlaViolationPackets = 0;
      std::vector<double> delaySamplesMs;
      std::vector<double> jitterSamplesMs;
    };

    uint32_t expectedFlows = 0;
    uint32_t activeFlows = 0;
    uint64_t rxBytes = 0;
    uint64_t rxPackets = 0;
    DelayAggregateStats steadyDelay;
    DelayAggregateStats overallDelay;
    double minFlowThroughputMbps = std::numeric_limits<double>::max ();
    double maxFlowThroughputMbps = 0.0;
  };

  std::map<Ipv4Address, Binding> m_bindingBySource;
  std::vector<ClientWindowStats> m_clientStats;
  Time m_windowStart;
  Time m_windowStop;
  uint64_t m_unknownPackets = 0;
  uint64_t m_outsideWindowPackets = 0;
};

class PacketSinkRxTracker
{
public:
  void Rx (Ptr<const Packet> packet, const Address &from, const Address &to);
  uint64_t GetBytes (Ipv4Address src) const;
  uint64_t GetPackets (Ipv4Address src) const;

private:
  std::map<Ipv4Address, uint64_t> m_bytesBySrc;
  std::map<Ipv4Address, uint64_t> m_packetsBySrc;
};

class BackboneTcpPayloadTracker
{
public:
  BackboneTcpPayloadTracker (Time windowStart, Time windowStop);

  void Rx (Ptr<const Packet> packet);
  uint64_t GetPayloadBytes (Ipv4Address src) const;
  uint64_t GetPayloadPackets (Ipv4Address src) const;

private:
  std::map<Ipv4Address, uint64_t> m_payloadBytesBySrc;
  std::map<Ipv4Address, uint64_t> m_packetsBySrc;
  Time m_windowStart;
  Time m_windowStop;
};

void PrintPriorityMetricsReport (const PriorityMetricsReport &report,
                                 std::ostream *reportStream);

void ApplyPriorityPathSplitMetrics (
    PriorityMetricsReport &report,
    const std::vector<ClientTrafficConfig> &clients,
    const NodeContainer &ueNodes,
    const Ipv4InterfaceContainer &ueNrIf,
    const std::map<uint32_t, WifiClientConfig> &wifiCfgByNodeId,
    const BackboneTcpPayloadTracker &backboneTracker);

void WriteSimulationJsonReport (const SimulationJsonReport &report);

} // namespace pamptcp
} // namespace ns3

#endif /* PAMPTCP_REPORT_H */
