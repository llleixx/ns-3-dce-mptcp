#include "rate-dual-application.h"
#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/inet6-socket-address.h"
#include "ns3/boolean.h"
#include "ns3/packet.h"
#include "ns3/packet-socket-address.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/tcp-socket-factory.h"
#include "ns3/double.h"
#include "ns3/enum.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/pointer.h"
#include "ns3/trace-source-accessor.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <netinet/in.h>

#include "../../model/linux/linux-socket-impl.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("RateDualModeApplication");
NS_OBJECT_ENSURE_REGISTERED (RateDualModeApplication);

TypeId
RateDualModeApplication::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::RateDualModeApplication")
    .SetParent<Application> ()
    .SetGroupName("Applications")
    .AddConstructor<RateDualModeApplication> ()
    .AddAttribute ("Remote", "The address of the destination",
                   AddressValue (),
                   MakeAddressAccessor (&RateDualModeApplication::m_peer),
                   MakeAddressChecker ())
    .AddAttribute ("Local",
                   "The address on which to bind the socket. If not set, it is generated automatically.",
                   AddressValue (),
                   MakeAddressAccessor (&RateDualModeApplication::m_local),
                   MakeAddressChecker ())
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (TcpSocketFactory::GetTypeId ()),
                   MakeTypeIdAccessor (&RateDualModeApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddAttribute ("IpTos",
                   "IPv4 TOS byte to apply on the socket (DSCP is the upper 6 bits).",
                   UintegerValue (0),
                   MakeUintegerAccessor (&RateDualModeApplication::m_ipTos),
                   MakeUintegerChecker<uint8_t> ())
    .AddAttribute ("SteadyRate",
                   "Data rate used in the steady state.",
                   DataRateValue (DataRate ("50.9Kbps")), 
                   MakeDataRateAccessor (&RateDualModeApplication::m_steadyRate),
                   MakeDataRateChecker ())
    .AddAttribute ("BurstRate", "Data rate used in the burst state.",
                   DataRateValue (DataRate ("92.79Mbps")), 
                   MakeDataRateAccessor (&RateDualModeApplication::m_burstRate),
                   MakeDataRateChecker ())
    .AddAttribute ("BurstProb",
                   "LegacyProbability mode only: probability of entering burst state "
                   "at each state evaluation.",
                   DoubleValue (0.0005), 
                   MakeDoubleAccessor (&RateDualModeApplication::m_burstProb),
                   MakeDoubleChecker<double> (0.0, 1.0))
    .AddAttribute ("TrafficModel",
                   "Traffic model: Duration alternates steady/burst states using explicit time distributions; "
                   "LegacyProbability keeps the older BurstProb/StateInterval behavior.",
                   EnumValue (TRAFFIC_MODEL_DURATION),
                   MakeEnumAccessor (&RateDualModeApplication::m_trafficModel),
                   MakeEnumChecker (TRAFFIC_MODEL_DURATION, "Duration",
                                    TRAFFIC_MODEL_LEGACY_PROBABILITY, "LegacyProbability"))
    .AddAttribute ("SteadyTime",
                   "Duration distribution of the steady state when TrafficModel=Duration.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=1.0]"),
                   MakePointerAccessor (&RateDualModeApplication::m_steadyTime),
                   MakePointerChecker<RandomVariableStream> ())
    .AddAttribute ("BurstTime",
                   "Duration distribution of the burst state when TrafficModel=Duration.",
                   StringValue ("ns3::ConstantRandomVariable[Constant=0.0001]"),
                   MakePointerAccessor (&RateDualModeApplication::m_burstTime),
                   MakePointerChecker<RandomVariableStream> ())
    .AddAttribute ("StateInterval",
                   "LegacyProbability mode only: random variable for time between state evaluations.",
                   StringValue ("ns3::NormalRandomVariable[Mean=1.0|Variance=0.01|Bound=2.0]"), 
                   MakePointerAccessor (&RateDualModeApplication::m_stateIntervalRng),
                   MakePointerChecker<RandomVariableStream> ())
    .AddAttribute ("InitialSendDelay",
                   "Delay payload transmission after TCP connect succeeds to allow MPTCP subflows to come up",
                   TimeValue (Seconds (0.0)),
                   MakeTimeAccessor (&RateDualModeApplication::m_initialSendDelay),
                   MakeTimeChecker ())
    .AddAttribute ("EnableSeqTsSizeHeader",
                   "Enable use of SeqTsSizeHeader for sequence number and timestamp",
                   BooleanValue (false),
                   MakeBooleanAccessor (&RateDualModeApplication::m_enableSeqTsSizeHeader),
                   MakeBooleanChecker ())
    .AddTraceSource ("Tx", "A new packet is sent",
                     MakeTraceSourceAccessor (&RateDualModeApplication::m_txTrace),
                     "ns3::Packet::TracedCallback")
    .AddTraceSource ("TxWithAddresses", "A new packet is sent",
                     MakeTraceSourceAccessor (&RateDualModeApplication::m_txTraceWithAddresses),
                     "ns3::Packet::TwoAddressTracedCallback")
    .AddTraceSource ("TxWithSeqTsSize", "A new packet is created with SeqTsSizeHeader",
                     MakeTraceSourceAccessor (&RateDualModeApplication::m_txTraceWithSeqTsSize),
                     "ns3::PacketSink::SeqTsSizeCallback")
  ;
  return tid;
}

RateDualModeApplication::RateDualModeApplication ()
{
  m_socket = 0;
  m_sendEvent = EventId ();
  m_stateEvent = EventId ();
  m_uniformRng = CreateObject<UniformRandomVariable> ();
  m_unsentPacket = 0;
}

RateDualModeApplication::~RateDualModeApplication() {}

bool
RateDualModeApplication::IsConnected () const
{
  return m_connected;
}

bool
RateDualModeApplication::HasConnected () const
{
  return m_hasConnected;
}

uint32_t
RateDualModeApplication::GetConnectFailures () const
{
  return m_connectFailures;
}

Socket::SocketErrno
RateDualModeApplication::GetLastErrno () const
{
  return m_lastErrno;
}

bool
RateDualModeApplication::IsConnectInProgress () const
{
  return m_connectInProgress;
}

bool
RateDualModeApplication::HasSentPayload () const
{
  return m_hasSentPayload;
}

Time
RateDualModeApplication::GetFirstPayloadTxTime () const
{
  return m_firstPayloadTxTime;
}

void RateDualModeApplication::DoDispose (void)
{
  CancelEvents ();
  m_unsentPacket = 0;
  m_socket = 0;
  Application::DoDispose ();
}

void
RateDualModeApplication::CancelEvents (void)
{
  Simulator::Cancel (m_sendEvent);
  Simulator::Cancel (m_stateEvent);
  m_unsentPacket = 0;
  m_pendingBurstBytes = 0;
  m_sendCreditBytes = 0.0;
}

Time
RateDualModeApplication::SampleStateDuration (Ptr<RandomVariableStream> rng,
                                              const char *attributeName) const
{
  NS_ABORT_MSG_IF (rng == 0, attributeName << " random variable is not configured");
  const double seconds = rng->GetValue ();
  NS_ABORT_MSG_IF (seconds < 0.0,
                   attributeName << " returned a negative duration");
  return Seconds (seconds);
}

void
RateDualModeApplication::StartApplication (void)
{
  m_running = true;
  CancelEvents ();

  const uint64_t steadyBps = m_steadyRate.GetBitRate ();
  const uint64_t burstBps = m_burstRate.GetBitRate ();

  NS_ABORT_MSG_IF (burstBps < steadyBps,
                   "BurstRate must be greater than or equal to SteadyRate");

  if (m_trafficModel == TRAFFIC_MODEL_DURATION)
    {
      NS_ABORT_MSG_IF (m_steadyTime == 0, "SteadyTime random variable is not configured");
      NS_ABORT_MSG_IF (m_burstTime == 0, "BurstTime random variable is not configured");
      m_inBurstState = false;
    }
  else
    {
      NS_ABORT_MSG_IF (m_stateIntervalRng == 0, "StateInterval random variable is not configured");
    }
  m_currentRate = m_steadyRate;

  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      NS_ABORT_MSG_IF (m_socket == 0, "Failed to create socket");
      if (m_socket->GetSocketType () != Socket::NS3_SOCK_STREAM &&
          m_socket->GetSocketType () != Socket::NS3_SOCK_SEQPACKET)
        {
          NS_FATAL_ERROR ("RateDualModeApplication requires a stream/seqpacket socket. "
                          "Use a TCP-like SocketFactory.");
        }
      int ret = -1;

      if (!m_local.IsInvalid ())
        {
          NS_ABORT_MSG_IF ((Inet6SocketAddress::IsMatchingType (m_peer) &&
                            InetSocketAddress::IsMatchingType (m_local)) ||
                           (InetSocketAddress::IsMatchingType (m_peer) &&
                            Inet6SocketAddress::IsMatchingType (m_local)),
                           "Incompatible peer and local address IP version");
          ret = m_socket->Bind (m_local);
        }
      else
        {
          if (Inet6SocketAddress::IsMatchingType (m_peer))
            {
              ret = m_socket->Bind6 ();
            }
          else if (InetSocketAddress::IsMatchingType (m_peer) ||
                   PacketSocketAddress::IsMatchingType (m_peer))
            {
              ret = m_socket->Bind ();
            }
        }

      if (ret == -1)
        {
          NS_FATAL_ERROR ("Failed to bind socket");
        }

      m_socket->SetIpTos (m_ipTos);
      if (Ptr<LinuxSocketImpl> linuxSocket = DynamicCast<LinuxSocketImpl> (m_socket))
        {
          const uint8_t tos = m_ipTos;
          linuxSocket->Setsockopt (SOL_IP, IP_TOS, &tos, sizeof (tos));
        }

      m_socket->SetConnectCallback (
          MakeCallback (&RateDualModeApplication::ConnectionSucceeded, this),
          MakeCallback (&RateDualModeApplication::ConnectionFailed, this));
      m_socket->SetSendCallback (
          MakeCallback (&RateDualModeApplication::HandleSendReady, this));
      m_socket->ShutdownRecv ();
    }

  if (!m_connected && !m_connectInProgress)
    {
      m_connectInProgress = true;
      const int ret = m_socket->Connect (m_peer);
      if (ret == -1)
        {
          m_lastErrno = m_socket->GetErrno ();
        }
    }
  else if (m_connected)
    {
      m_stateEvent = Simulator::Schedule (m_initialSendDelay,
                                          &RateDualModeApplication::StartTraffic,
                                          this);
    }
}

void
RateDualModeApplication::StopApplication (void)
{
  m_running = false;
  CancelEvents ();
  m_connected = false;
  m_connectInProgress = false;
  if (m_socket)
    {
      m_socket->Close ();
      m_socket = 0;
    }
}

void
RateDualModeApplication::StartTraffic (void)
{
  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  if (m_trafficModel == TRAFFIC_MODEL_DURATION)
    {
      m_inBurstState = false;
      m_currentRate = m_steadyRate;
      if (!m_sendEvent.IsRunning ())
        {
          ScheduleNextTx ();
        }
      ScheduleNextStateChange (SampleStateDuration (m_steadyTime, "SteadyTime"));
      return;
    }

  UpdateLegacyState ();
}

void
RateDualModeApplication::ScheduleNextStateChange (Time delay)
{
  const Time boundedDelay = std::max (NanoSeconds (1), delay);
  m_stateEvent = Simulator::Schedule (boundedDelay,
                                      &RateDualModeApplication::AdvanceTrafficState,
                                      this);
}

void
RateDualModeApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  (void) socket;
  if (!m_running)
    {
      return;
    }
  m_hasConnected = true;
  m_connected = true;
  m_connectInProgress = false;
  m_lastErrno = Socket::ERROR_NOTERROR;
  m_stateEvent = Simulator::Schedule (m_initialSendDelay,
                                      &RateDualModeApplication::StartTraffic,
                                      this);
}

void
RateDualModeApplication::ConnectionFailed (Ptr<Socket> socket)
{
  m_connected = false;
  m_connectInProgress = false;
  m_lastErrno = socket ? socket->GetErrno () : Socket::ERROR_NOTERROR;
  m_connectFailures++;
}

void
RateDualModeApplication::HandleSendReady (Ptr<Socket> socket, uint32_t txSpace)
{
  (void) txSpace;
  if (!m_running || !m_connected || socket != m_socket)
    {
      return;
    }
  if (!m_unsentPacket && m_pendingBurstBytes == 0)
    {
      return;
    }
  DrainPendingPackets ();
}

void
RateDualModeApplication::AdvanceTrafficState (void)
{
  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  if (m_trafficModel == TRAFFIC_MODEL_LEGACY_PROBABILITY)
    {
      UpdateLegacyState ();
      return;
    }

  m_inBurstState = !m_inBurstState;
  const DataRate newRate = m_inBurstState ? m_burstRate : m_steadyRate;
  if (newRate != m_currentRate)
    {
      m_currentRate = newRate;
      Simulator::Cancel (m_sendEvent);
    }

  if (!m_sendEvent.IsRunning ())
    {
      ScheduleNextTx ();
    }

  ScheduleNextStateChange (m_inBurstState
                               ? SampleStateDuration (m_burstTime, "BurstTime")
                               : SampleStateDuration (m_steadyTime, "SteadyTime"));
}

void
RateDualModeApplication::UpdateLegacyState (void)
{
  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  const bool isBurst = (m_uniformRng->GetValue () < m_burstProb);
  const DataRate newRate = isBurst ? m_burstRate : m_steadyRate;

  if (newRate != m_currentRate)
    {
      m_currentRate = newRate;
      Simulator::Cancel (m_sendEvent);
    }

  if (!m_sendEvent.IsRunning ())
    {
      ScheduleNextTx ();
    }

  const double nextIntervalSeconds = std::max (0.001, m_stateIntervalRng->GetValue ());
  ScheduleNextStateChange (Seconds (nextIntervalSeconds));
}

void
RateDualModeApplication::SendPacket (void)
{
  NS_ASSERT (m_sendEvent.IsExpired ());

  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  SendBurst ();
  ScheduleNextTx ();
}

void
RateDualModeApplication::SendBurst (void)
{
  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  m_pendingBurstBytes += ComputeBurstBytesForCurrentRate ();
  DrainPendingPackets ();
}

void
RateDualModeApplication::DrainPendingPackets (void)
{
  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  if (m_unsentPacket)
    {
      TrySendPacket (true);
      if (m_unsentPacket)
        {
          return;
        }
    }

  while (m_pendingBurstBytes > 0 && !m_unsentPacket)
    {
      TrySendPacket (false);
    }
}

void
RateDualModeApplication::ScheduleNextTx (void)
{
  if (!m_running || !m_connected || m_currentRate.GetBitRate () == 0)
    {
      return;
    }

  m_sendEvent = Simulator::Schedule (Seconds (1.0),
                                     &RateDualModeApplication::SendPacket,
                                     this);
}

uint64_t
RateDualModeApplication::ComputeBurstBytesForCurrentRate (void)
{
  const double generatedBytes =
      static_cast<double> (m_currentRate.GetBitRate ()) / 8.0;
  m_sendCreditBytes += generatedBytes;

  const uint64_t bytes = static_cast<uint64_t> (std::floor (m_sendCreditBytes));
  m_sendCreditBytes -= static_cast<double> (bytes);
  return bytes;
}

void
RateDualModeApplication::TrySendPacket (bool resendOnly)
{
  if (!m_running || !m_connected || !m_socket)
    {
      return;
    }

  Ptr<Packet> packet = m_unsentPacket;
  if (!packet)
    {
      if (resendOnly)
        {
          return;
        }
      NS_ABORT_MSG_IF (m_pendingBurstBytes == 0,
                       "TrySendPacket(false) requires positive pending burst bytes");

      const uint32_t nextBytes =
          static_cast<uint32_t> (std::min<uint64_t> (
              m_pendingBurstBytes,
              static_cast<uint64_t> (std::numeric_limits<uint32_t>::max ())));
      if (m_enableSeqTsSizeHeader)
        {
          Address from;
          Address to;
          m_socket->GetSockName (from);
          m_socket->GetPeerName (to);

          SeqTsSizeHeader header;
          header.SetSeq (m_seq++);
          header.SetSize (nextBytes);
          NS_ABORT_IF (nextBytes < header.GetSerializedSize ());

          packet = Create<Packet> (nextBytes - header.GetSerializedSize ());
          // Trace before adding the header, like OnOff/BulkSend.
          m_txTraceWithSeqTsSize (packet, from, to, header);
          packet->AddHeader (header);
        }
      else
        {
          packet = Create<Packet> (nextBytes);
        }
    }

  const uint32_t size = packet->GetSize ();
  const int actual = m_socket->Send (packet);
  if (actual == static_cast<int> (size))
    {
      m_lastErrno = Socket::ERROR_NOTERROR;
      m_unsentPacket = 0;
      m_pendingBurstBytes -= size;
      RecordSuccessfulSend (packet);
      return;
    }

  if (actual == -1)
    {
      m_lastErrno = m_socket->GetErrno ();
      m_unsentPacket = packet;
      return;
    }

  if (actual > 0 && static_cast<uint32_t> (actual) < size)
    {
      m_lastErrno = Socket::ERROR_NOTERROR;
      Ptr<Packet> sent = packet->CreateFragment (0, actual);
      Ptr<Packet> unsent = packet->CreateFragment (actual, size - static_cast<uint32_t> (actual));
      m_unsentPacket = unsent;
      m_pendingBurstBytes -= static_cast<uint32_t> (actual);
      RecordSuccessfulSend (sent);
      return;
    }

  NS_FATAL_ERROR ("Unexpected return value from m_socket->Send ()");
}

void
RateDualModeApplication::RecordSuccessfulSend (Ptr<const Packet> packet)
{
  if (!m_hasSentPayload)
    {
      m_hasSentPayload = true;
      m_firstPayloadTxTime = Simulator::Now ();
    }

  m_txTrace (packet);

  Address localAddress;
  Address peerAddress;
  if (m_socket->GetSockName (localAddress) == 0 &&
      m_socket->GetPeerName (peerAddress) == 0)
    {
      m_txTraceWithAddresses (packet, localAddress, peerAddress);
    }
}

int64_t
RateDualModeApplication::AssignStreams (int64_t stream)
{
  int64_t currentStream = stream;
  if (m_uniformRng)
    {
      m_uniformRng->SetStream (currentStream);
      ++currentStream;
    }
  if (m_steadyTime)
    {
      m_steadyTime->SetStream (currentStream);
      ++currentStream;
    }
  if (m_burstTime)
    {
      m_burstTime->SetStream (currentStream);
      ++currentStream;
    }
  if (m_stateIntervalRng)
    {
      m_stateIntervalRng->SetStream (currentStream);
      ++currentStream;
    }
  return (currentStream - stream);
}

} // namespace ns3
