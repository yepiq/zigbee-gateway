#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace zigbee_gateway {

enum class ZigbeeTransportMode : uint8_t {
  TCP = 0,
  USB_BRIDGED = 1,
  USB_DIRECT = 2,
};

static constexpr size_t ZIGBEE_TRANSPORT_MODE_COUNT = 3;

inline const char *zigbee_transport_mode_name(ZigbeeTransportMode mode) {
  switch (mode) {
    case ZigbeeTransportMode::TCP:
      return "TCP";
    case ZigbeeTransportMode::USB_BRIDGED:
      return "USB Bridged";
    case ZigbeeTransportMode::USB_DIRECT:
      return "USB Direct";
  }
  return "TCP";
}

inline bool zigbee_transport_mode_from_index(size_t index,
                                             ZigbeeTransportMode *mode) {
  if (mode == nullptr || index >= ZIGBEE_TRANSPORT_MODE_COUNT)
    return false;
  *mode = static_cast<ZigbeeTransportMode>(index);
  return true;
}

inline bool zigbee_transport_uses_tcp(ZigbeeTransportMode mode) {
  return mode == ZigbeeTransportMode::TCP;
}

inline bool zigbee_transport_uses_software_bridge(ZigbeeTransportMode mode) {
  return mode == ZigbeeTransportMode::USB_BRIDGED;
}

inline bool zigbee_transport_uses_direct_pin(ZigbeeTransportMode mode) {
  return mode == ZigbeeTransportMode::USB_DIRECT;
}

}  // namespace zigbee_gateway
}  // namespace esphome
