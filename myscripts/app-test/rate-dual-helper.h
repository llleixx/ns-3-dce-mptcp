#ifndef RATE_DUAL_HELPER_H
#define RATE_DUAL_HELPER_H

#include <stdint.h>
#include <string>
#include "ns3/object-factory.h"
#include "ns3/address.h"
#include "ns3/attribute.h"
#include "ns3/net-device.h"
#include "ns3/node-container.h"
#include "ns3/application-container.h"

namespace ns3 {

/**
 * \brief A helper to make it easier to instantiate an ns3::RateDualModeApplication
 * on a set of nodes.
 */
class RateDualHelper
{
public:
  /**
   * Create a RateDualHelper to make it easier to work with RateDualModeApplications
   *
   * \param protocol the name of the protocol to use to send traffic
   * \param address the address of the receiver
   */
  RateDualHelper (std::string protocol, Address address);

  /**
   * Helper function used to set the underlying application attributes.
   *
   * \param name the name of the application attribute to set
   * \param value the value of the application attribute to set
   */
  void SetAttribute (std::string name, const AttributeValue &value);

  /**
   * Install an ns3::RateDualModeApplication on each node of the input container
   * configured with all the attributes set with SetAttribute.
   *
   * \param c NodeContainer of the set of nodes on which an RateDualModeApplication 
   * will be installed.
   * \returns Container of Ptr to the applications installed.
   */
  ApplicationContainer Install (NodeContainer c) const;

  /**
   * Install an ns3::RateDualModeApplication on the node configured with all the 
   * attributes set with SetAttribute.
   *
   * \param node The node on which an RateDualModeApplication will be installed.
   * \returns Container of Ptr to the applications installed.
   */
  ApplicationContainer Install (Ptr<Node> node) const;

  /**
   * Install an ns3::RateDualModeApplication on the node configured with all the 
   * attributes set with SetAttribute.
   *
   * \param nodeName The node on which an RateDualModeApplication will be installed.
   * \returns Container of Ptr to the applications installed.
   */
  ApplicationContainer Install (std::string nodeName) const;

  /**
   * Assign a fixed random variable stream number to the random variables
   * used by this model.
   *
   * \param stream first stream index to use
   * \param c the application container
   * \return the number of stream indices assigned by this helper
   */
  int64_t AssignStreams (ApplicationContainer c, int64_t stream);

private:
  Ptr<Application> InstallPriv (Ptr<Node> node) const; //
  ObjectFactory m_factory;                             //
};

} // namespace ns3

#endif /* RATE_DUAL_HELPER_H */
