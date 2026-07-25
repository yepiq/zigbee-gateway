#include <cassert>
#include <cstring>

#include "components/zigbee_gateway/zigbee_transport_mode.h"

using esphome::zigbee_gateway::ZIGBEE_TRANSPORT_MODE_COUNT;
using esphome::zigbee_gateway::ZigbeeTransportMode;
using esphome::zigbee_gateway::zigbee_transport_mode_from_index;
using esphome::zigbee_gateway::zigbee_transport_mode_name;
using esphome::zigbee_gateway::zigbee_transport_uses_direct_pin;
using esphome::zigbee_gateway::zigbee_transport_uses_software_bridge;
using esphome::zigbee_gateway::zigbee_transport_uses_tcp;

int main() {
  assert(ZIGBEE_TRANSPORT_MODE_COUNT == 3);

  ZigbeeTransportMode mode = ZigbeeTransportMode::TCP;
  assert(zigbee_transport_mode_from_index(0, &mode));
  assert(mode == ZigbeeTransportMode::TCP);
  assert(std::strcmp(zigbee_transport_mode_name(mode), "TCP") == 0);
  assert(zigbee_transport_uses_tcp(mode));

  assert(zigbee_transport_mode_from_index(1, &mode));
  assert(mode == ZigbeeTransportMode::USB_BRIDGED);
  assert(std::strcmp(zigbee_transport_mode_name(mode), "USB Bridged") == 0);
  assert(zigbee_transport_uses_software_bridge(mode));

  assert(zigbee_transport_mode_from_index(2, &mode));
  assert(mode == ZigbeeTransportMode::USB_DIRECT);
  assert(std::strcmp(zigbee_transport_mode_name(mode), "USB Direct") == 0);
  assert(zigbee_transport_uses_direct_pin(mode));

  assert(!zigbee_transport_mode_from_index(3, &mode));
  assert(!zigbee_transport_mode_from_index(0, nullptr));
  return 0;
}
