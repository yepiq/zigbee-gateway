#pragma once

#include <cstdint>

namespace esphome {
namespace zigbee_gateway {

enum class ZigbeeSerialOwner : uint8_t {
  NONE = 0,
  LOCAL = 1,
  TCP_NORMAL = 2,
  TCP_MAINTENANCE = 3,
  USB_BRIDGE = 4,
};

/// Passive ZNP decoding is safe only for transparent application streams.
///
/// Local diagnostics publish their own results, while TCP maintenance may
/// carry opaque BSL firmware data that must never be interpreted as ZNP.
inline bool zigbee_serial_owner_allows_passive_znp_observation(
    ZigbeeSerialOwner owner) {
  return owner == ZigbeeSerialOwner::TCP_NORMAL ||
         owner == ZigbeeSerialOwner::USB_BRIDGE;
}

}  // namespace zigbee_gateway
}  // namespace esphome
