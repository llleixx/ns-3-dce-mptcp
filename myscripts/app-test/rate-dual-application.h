#ifndef RATE_DUAL_APPLICATION_H
#define RATE_DUAL_APPLICATION_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/data-rate.h"
#include "ns3/socket.h"

namespace ns3 {

class RateDualModeApplication : public Application 
{
public:
  static TypeId GetTypeId (void);
  RateDualModeApplication ();
  virtual ~RateDualModeApplication ();

  // 暴露给 Helper 进行随机数流分配的接口
  int64_t AssignStreams (int64_t stream);

  bool IsConnected () const;
  uint32_t GetConnectFailures () const;
  Socket::SocketErrno GetLastErrno () const;
  bool IsConnectInProgress () const;

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void ConnectionSucceeded (Ptr<Socket> socket);
  void ConnectionFailed (Ptr<Socket> socket);

  void UpdateState (void);
  void SendPacket (void);
  void ScheduleNextTx (void);

  Address         m_peer;
  TypeId          m_tid;
  uint32_t        m_packetSize;
  DataRate        m_avgRate;
  DataRate        m_peakRate;
  double          m_burstProb;
  
  Ptr<RandomVariableStream> m_stateIntervalRng; 

  Ptr<Socket>     m_socket;
  EventId         m_sendEvent;
  EventId         m_stateEvent;
  DataRate        m_baseRate;
  DataRate        m_currentRate;

  Ptr<UniformRandomVariable> m_uniformRng;

  bool m_connected {false};
  bool m_connectInProgress {false};
  uint32_t m_connectFailures {0};
  Socket::SocketErrno m_lastErrno {Socket::ERROR_NOTERROR};
};

} // namespace ns3

#endif /* RATE_DUAL_APPLICATION_H */
