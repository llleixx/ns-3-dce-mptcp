#include "rate-dual-helper.h"
#include "rate-dual-application.h"
#include "ns3/string.h"
#include "ns3/names.h"

namespace ns3 {

RateDualHelper::RateDualHelper (std::string protocol, Address address)
{
  m_factory.SetTypeId ("ns3::RateDualModeApplication"); //
  m_factory.Set ("Protocol", StringValue (protocol));   //
  m_factory.Set ("Remote", AddressValue (address));     //
}

void 
RateDualHelper::SetAttribute (std::string name, const AttributeValue &value)
{
  m_factory.Set (name, value); //
}

ApplicationContainer
RateDualHelper::Install (Ptr<Node> node) const
{
  return ApplicationContainer (InstallPriv (node)); //
}

ApplicationContainer
RateDualHelper::Install (std::string nodeName) const
{
  Ptr<Node> node = Names::Find<Node> (nodeName); //
  return ApplicationContainer (InstallPriv (node)); //
}

ApplicationContainer
RateDualHelper::Install (NodeContainer c) const
{
  ApplicationContainer apps;
  for (NodeContainer::Iterator i = c.Begin (); i != c.End (); ++i) //
    {
      apps.Add (InstallPriv (*i)); //
    }

  return apps; //
}

Ptr<Application>
RateDualHelper::InstallPriv (Ptr<Node> node) const
{
  Ptr<Application> app = m_factory.Create<Application> (); //
  node->AddApplication (app); //

  return app; //
}

int64_t 
RateDualHelper::AssignStreams (ApplicationContainer c, int64_t stream)
{
  int64_t currentStream = stream; //
  Ptr<Node> node;
  for (ApplicationContainer::Iterator i = c.Begin (); i != c.End (); ++i) //
    {
      Ptr<RateDualModeApplication> rateDualApp = DynamicCast<RateDualModeApplication> (*i);
      if (rateDualApp)
        {
          currentStream += rateDualApp->AssignStreams (currentStream);
        }
    }
  return (currentStream - stream); //
}

} // namespace ns3
