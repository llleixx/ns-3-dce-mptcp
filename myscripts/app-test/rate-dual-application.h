#ifndef RATE_DUAL_APPLICATION_H
#define RATE_DUAL_APPLICATION_H

#include "ns3/application.h"
#include "ns3/event-id.h"
#include "ns3/ptr.h"
#include "ns3/ipv4-address.h"
#include "ns3/random-variable-stream.h"
#include "ns3/data-rate.h"
#include "ns3/seq-ts-size-header.h"
#include "ns3/socket.h"
#include "ns3/nstime.h"
#include "ns3/traced-callback.h"

namespace ns3 {

class RateDualModeApplication : public Application 
{
public:
  enum TrafficModel
  {
    TRAFFIC_MODEL_DURATION = 0,
    TRAFFIC_MODEL_LEGACY_PROBABILITY = 1
  };

  static TypeId GetTypeId (void);
  RateDualModeApplication ();
  virtual ~RateDualModeApplication ();

  // 暴露给 Helper 进行随机数流分配的接口
  int64_t AssignStreams (int64_t stream);

  bool IsConnected () const;
  bool HasConnected () const;
  uint32_t GetConnectFailures () const;
  Socket::SocketErrno GetLastErrno () const;
  bool IsConnectInProgress () const;
  bool HasSentPayload () const;
  Time GetFirstPayloadTxTime () const;

protected:
  virtual void DoDispose (void);

private:
  virtual void StartApplication (void);
  virtual void StopApplication (void);

  void CancelEvents (void);
  Time SampleStateDuration (Ptr<RandomVariableStream> rng,
                            const char *attributeName) const;
  void StartTraffic (void);
  void ScheduleNextStateChange (Time delay);
  void ConnectionSucceeded (Ptr<Socket> socket);
  void ConnectionFailed (Ptr<Socket> socket);
  void HandleSendReady (Ptr<Socket> socket, uint32_t txSpace);

  void AdvanceTrafficState (void);
  void UpdateLegacyState (void);
  void SendPacket (void);
  void ScheduleNextTx (void);
  void TrySendPacket (bool resendOnly);
  void RecordSuccessfulSend (Ptr<const Packet> packet);

  Address         m_peer;
  Address         m_local;
  TypeId          m_tid;
  uint32_t        m_packetSize;
  uint8_t         m_ipTos {0};
  DataRate        m_steadyRate;
  DataRate        m_burstRate;
  double          m_burstProb;
  TrafficModel    m_trafficModel {TRAFFIC_MODEL_DURATION};
  Ptr<RandomVariableStream> m_steadyTime;
  Ptr<RandomVariableStream> m_burstTime;
  
  Ptr<RandomVariableStream> m_stateIntervalRng; 

  Ptr<Socket>     m_socket;
  EventId         m_sendEvent;
  EventId         m_stateEvent;
  DataRate        m_currentRate;
  Time            m_initialSendDelay;

  Ptr<UniformRandomVariable> m_uniformRng;
  Ptr<Packet>     m_unsentPacket;

  TracedCallback<Ptr<const Packet> > m_txTrace;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &> m_txTraceWithAddresses;
  TracedCallback<Ptr<const Packet>, const Address &, const Address &, const SeqTsSizeHeader &>
      m_txTraceWithSeqTsSize;

  bool m_running {false};
  bool m_hasConnected {false};
  bool m_connected {false};
  bool m_connectInProgress {false};
  bool m_inBurstState {false};
  uint32_t m_connectFailures {0};
  Socket::SocketErrno m_lastErrno {Socket::ERROR_NOTERROR};
  bool m_hasSentPayload {false};
  Time m_firstPayloadTxTime {Time::Min ()};
  bool m_enableSeqTsSizeHeader {false};
  uint32_t m_seq {0};
};

} // namespace ns3

#endif /* RATE_DUAL_APPLICATION_H */
