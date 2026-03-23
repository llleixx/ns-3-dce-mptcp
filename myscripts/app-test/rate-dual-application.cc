#include "rate-dual-application.h"
#include "ns3/log.h"
#include "ns3/address.h"
#include "ns3/inet-socket-address.h"
#include "ns3/packet.h"
#include "ns3/socket.h"
#include "ns3/simulator.h"
#include "ns3/double.h"
#include "ns3/uinteger.h"
#include "ns3/string.h"
#include "ns3/pointer.h"

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
    .AddAttribute ("Protocol", "The type of protocol to use.",
                   TypeIdValue (Socket::GetTypeId ()),
                   MakeTypeIdAccessor (&RateDualModeApplication::m_tid),
                   MakeTypeIdChecker ())
    .AddAttribute ("PacketSize", "The size of packets sent in payload",
                   UintegerValue (1000), 
                   MakeUintegerAccessor (&RateDualModeApplication::m_packetSize),
                   MakeUintegerChecker<uint32_t> (1))
    .AddAttribute ("AvgRate", "Documented average data rate",
                   DataRateValue (DataRate ("50.9Kbps")), 
                   MakeDataRateAccessor (&RateDualModeApplication::m_avgRate),
                   MakeDataRateChecker ())
    .AddAttribute ("PeakRate", "Documented peak data rate",
                   DataRateValue (DataRate ("92.79Mbps")), 
                   MakeDataRateAccessor (&RateDualModeApplication::m_peakRate),
                   MakeDataRateChecker ())
    .AddAttribute ("BurstProb", "Probability of entering burst state per evaluation",
                   DoubleValue (0.0005), 
                   MakeDoubleAccessor (&RateDualModeApplication::m_burstProb),
                   MakeDoubleChecker<double> (0.0, 1.0))
    .AddAttribute ("StateInterval", "Random variable for time between state evaluations",
                   StringValue ("ns3::NormalRandomVariable[Mean=1.0|Variance=0.01|Bound=2.0]"), 
                   MakePointerAccessor (&RateDualModeApplication::m_stateIntervalRng),
                   MakePointerChecker<RandomVariableStream> ())
  ;
  return tid;
}

RateDualModeApplication::RateDualModeApplication ()
{
  m_socket = 0;
  m_sendEvent = EventId ();
  m_stateEvent = EventId ();
  m_uniformRng = CreateObject<UniformRandomVariable> ();
}

RateDualModeApplication::~RateDualModeApplication() {}

bool
RateDualModeApplication::IsConnected () const
{
  return m_connected;
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

void RateDualModeApplication::DoDispose (void)
{
  m_socket = 0;
  Application::DoDispose ();
}

void RateDualModeApplication::StartApplication (void)
{
  if (!m_socket)
    {
      m_socket = Socket::CreateSocket (GetNode (), m_tid);
      m_socket->SetConnectCallback (
          MakeCallback (&RateDualModeApplication::ConnectionSucceeded, this),
          MakeCallback (&RateDualModeApplication::ConnectionFailed, this));
      m_socket->Bind ();
      m_connectInProgress = true;
      const int ret = m_socket->Connect (m_peer);
      if (ret == -1)
        {
          m_lastErrno = m_socket->GetErrno ();
        }
    }

  double avgBps = m_avgRate.GetBitRate ();
  double peakBps = m_peakRate.GetBitRate ();
  double baseBps = (avgBps - (m_burstProb * peakBps)) / (1.0 - m_burstProb);

  if (baseBps < 0) {
      m_baseRate = DataRate (avgBps); 
  } else {
      m_baseRate = DataRate (baseBps);
  }

  m_currentRate = m_baseRate;
  // Defer scheduling until the TCP connection is established.
}

void RateDualModeApplication::StopApplication (void)
{
  if (m_sendEvent.IsRunning ()) Simulator::Cancel (m_sendEvent);
  if (m_stateEvent.IsRunning ()) Simulator::Cancel (m_stateEvent);
  if (m_socket) m_socket->Close ();
}

void
RateDualModeApplication::ConnectionSucceeded (Ptr<Socket> socket)
{
  m_connected = true;
  m_connectInProgress = false;
  m_lastErrno = Socket::ERROR_NOTERROR;
  UpdateState ();
}

void
RateDualModeApplication::ConnectionFailed (Ptr<Socket> socket)
{
  m_connected = false;
  m_connectInProgress = false;
  m_lastErrno = socket ? socket->GetErrno () : Socket::ERROR_NOTERROR;
  m_connectFailures++;
}

void RateDualModeApplication::UpdateState (void)
{
  bool isBurst = (m_uniformRng->GetValue () < m_burstProb);
  DataRate newRate = isBurst ? m_peakRate : m_baseRate;

  if (newRate != m_currentRate) 
    {
      m_currentRate = newRate;
      if (m_sendEvent.IsRunning ()) Simulator::Cancel (m_sendEvent);
      ScheduleNextTx ();
    }
  else if (!m_sendEvent.IsRunning ())
    {
      ScheduleNextTx ();
    }

  double nextIntervalSeconds = std::max(0.001, m_stateIntervalRng->GetValue());
  m_stateEvent = Simulator::Schedule (Seconds (nextIntervalSeconds), &RateDualModeApplication::UpdateState, this);
}

void RateDualModeApplication::SendPacket (void)
{
  Ptr<Packet> packet = Create<Packet> (m_packetSize);
  m_socket->Send (packet);
  ScheduleNextTx ();
}

void RateDualModeApplication::ScheduleNextTx (void)
{
  if (m_currentRate.GetBitRate () == 0) return;
  Time nextTime = Seconds (m_packetSize * 8.0 / m_currentRate.GetBitRate ());
  m_sendEvent = Simulator::Schedule (nextTime, &RateDualModeApplication::SendPacket, this);
}

int64_t RateDualModeApplication::AssignStreams (int64_t stream)
{
  m_uniformRng->SetStream (stream);
  m_stateIntervalRng->SetStream (stream + 1);
  return 2;
}

} // namespace ns3
