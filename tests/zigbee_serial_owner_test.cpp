#include <cassert>

#include "components/zigbee_gateway/zigbee_serial_owner.h"

using esphome::zigbee_gateway::ZigbeeSerialOwner;
using esphome::zigbee_gateway::zigbee_serial_owner_allows_passive_znp_observation;

int main() {
  assert(!zigbee_serial_owner_allows_passive_znp_observation(
      ZigbeeSerialOwner::NONE));
  assert(!zigbee_serial_owner_allows_passive_znp_observation(
      ZigbeeSerialOwner::LOCAL));
  assert(zigbee_serial_owner_allows_passive_znp_observation(
      ZigbeeSerialOwner::TCP_NORMAL));
  assert(!zigbee_serial_owner_allows_passive_znp_observation(
      ZigbeeSerialOwner::TCP_MAINTENANCE));
  assert(zigbee_serial_owner_allows_passive_znp_observation(
      ZigbeeSerialOwner::USB_BRIDGE));
  return 0;
}
