/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */

#ifndef PAMPTCP_PROFILE_H
#define PAMPTCP_PROFILE_H

#include "ns3/data-rate.h"
#include "ns3/ipv4-address.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ns3 {
namespace pamptcp {

struct WifiClientConfig
{
  Ipv4Address addr;
  Ipv4Address gateway;
  std::string subnetCidr;
};

uint8_t PriorityToDscp (uint32_t priority);
uint8_t DscpToIpTos (uint8_t dscp);
double GetDelaySlaThresholdMs (uint32_t priority);

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

TrafficAppConfig BuildDefaultTrafficAppConfig (const std::string &defaultSteadyRateText,
                                               const std::string &defaultBurstRateText,
                                               const std::string &defaultTrafficModel,
                                               double defaultBurstProb,
                                               double defaultInitialSendDelaySeconds,
                                               const std::string &defaultStateInterval,
                                               const std::string &defaultSteadyTime,
                                               const std::string &defaultBurstTime);

ClientTrafficConfig MakeClientTrafficConfig (uint32_t clientIndex,
                                             const std::string &templateId,
                                             const TrafficAppConfig &app);

TrafficProfileConfig LoadTrafficProfileConfig (const std::string &directory,
                                              const TrafficAppConfig &defaultApp);

} // namespace pamptcp
} // namespace ns3

#endif /* PAMPTCP_PROFILE_H */
