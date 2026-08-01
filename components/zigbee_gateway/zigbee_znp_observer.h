#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace zigbee_gateway {

enum class ZnpObservationType : uint8_t {
  NONE = 0,
  SYS_VERSION = 1,
  UTIL_DEVICE_INFO = 2,
  ZDO_EXT_NETWORK_INFO = 3,
  ZDO_STATE_CHANGE = 4,
};

struct ZnpObservation {
  ZnpObservationType type{ZnpObservationType::NONE};

  uint8_t major{0};
  uint8_t minor{0};
  uint8_t maintenance{0};
  uint32_t revision{0};

  std::array<uint8_t, 8> active_ieee_lsb{};
  uint8_t device_capabilities{0};
  uint8_t device_state{0};

  uint16_t pan_id{0};
  std::array<uint8_t, 8> extended_pan_id_lsb{};
  std::array<uint8_t, 8> parent_ieee_lsb{};
  uint8_t channel{0};
};

enum class ZnpObservedRole : uint8_t {
  UNKNOWN = 0,
  COORDINATOR = 1,
  ROUTER = 2,
  END_DEVICE = 3,
};

inline ZnpObservedRole znp_observed_role(uint8_t device_capabilities,
                                         uint8_t device_state) {
  // A definitive running state is stronger evidence than the DeviceType
  // field, which is a capability mask rather than the configured role.
  switch (device_state) {
    case 6:
      return ZnpObservedRole::END_DEVICE;
    case 7:
      return ZnpObservedRole::ROUTER;
    case 8:
    case 9:
      return ZnpObservedRole::COORDINATOR;
    default:
      break;
  }

  // A single capability can identify a specialized image. Multi-capability
  // images such as standard ZNP commonly report 0x07 and remain ambiguous
  // until NV or a definitive device state identifies their configured role.
  switch (device_capabilities & 0x07) {
    case 0x01:
      return ZnpObservedRole::COORDINATOR;
    case 0x02:
      return ZnpObservedRole::ROUTER;
    case 0x04:
      return ZnpObservedRole::END_DEVICE;
    default:
      return ZnpObservedRole::UNKNOWN;
  }
}

inline const char *znp_observed_role_name(ZnpObservedRole role) {
  switch (role) {
    case ZnpObservedRole::COORDINATOR:
      return "Coordinator";
    case ZnpObservedRole::ROUTER:
      return "Router";
    case ZnpObservedRole::END_DEVICE:
      return "End Device";
    case ZnpObservedRole::UNKNOWN:
      return "Unknown";
  }
  return "Unknown";
}

inline bool znp_frame_fcs_valid(uint8_t length, uint8_t cmd0, uint8_t cmd1,
                                const uint8_t *payload, uint8_t fcs) {
  uint8_t expected = length ^ cmd0 ^ cmd1;
  for (uint16_t index = 0; index < length; index++)
    expected ^= payload[index];
  return expected == fcs;
}

inline bool znp_device_state_is_on_network(uint8_t state) {
  // Z-Stack device states: END_DEVICE=6, ROUTER=7, ZB_COORD=9.
  return state == 6 || state == 7 || state == 9;
}

/// Decode only ZNP frames whose layouts are used for passive gateway state.
///
/// The caller must first validate the UNPI FCS. This function never performs
/// UART I/O and never alters the byte stream; it interprets the already
/// forwarded response/indication observed by ESPHome's UART debug callback.
inline bool decode_znp_observation(uint8_t cmd0, uint8_t cmd1,
                                   const uint8_t *payload, uint8_t length,
                                   ZnpObservation *observation) {
  if (payload == nullptr || observation == nullptr)
    return false;
  *observation = ZnpObservation{};

  // SYS_VERSION SRSP:
  // transportRev, product, major, minor, maintenance, revision_le32.
  if (cmd0 == 0x61 && cmd1 == 0x02 && length >= 9) {
    observation->type = ZnpObservationType::SYS_VERSION;
    observation->major = payload[2];
    observation->minor = payload[3];
    observation->maintenance = payload[4];
    observation->revision = static_cast<uint32_t>(payload[5]) |
                            (static_cast<uint32_t>(payload[6]) << 8) |
                            (static_cast<uint32_t>(payload[7]) << 16) |
                            (static_cast<uint32_t>(payload[8]) << 24);
    return true;
  }

  // UTIL_GET_DEVICE_INFO SRSP:
  // status, IEEE_le[8], shortAddr_le16, deviceTypeCapabilities, deviceState,
  // associatedDeviceCount, associatedDeviceList[].
  if (cmd0 == 0x67 && cmd1 == 0x00 && length >= 14 && payload[0] == 0x00) {
    observation->type = ZnpObservationType::UTIL_DEVICE_INFO;
    for (size_t index = 0; index < observation->active_ieee_lsb.size(); index++)
      observation->active_ieee_lsb[index] = payload[1 + index];
    observation->device_capabilities = payload[11];
    observation->device_state = payload[12];
    return true;
  }

  // ZDO_EXT_NWK_INFO SRSP:
  // shortAddr_le16, deviceState, panId_le16, parentShort_le16,
  // extendedPanId_le[8], parentIEEE_le[8], channel.
  if (cmd0 == 0x65 && cmd1 == 0x50 && length == 24) {
    observation->type = ZnpObservationType::ZDO_EXT_NETWORK_INFO;
    observation->device_state = payload[2];
    observation->pan_id =
        static_cast<uint16_t>(payload[3]) | (static_cast<uint16_t>(payload[4]) << 8);
    for (size_t index = 0; index < observation->extended_pan_id_lsb.size(); index++)
      observation->extended_pan_id_lsb[index] = payload[7 + index];
    for (size_t index = 0; index < observation->parent_ieee_lsb.size(); index++)
      observation->parent_ieee_lsb[index] = payload[15 + index];
    observation->channel = payload[23];
    return true;
  }

  // ZDO_STATE_CHANGE_IND AREQ: one Z-Stack device-state byte.
  if (cmd0 == 0x45 && cmd1 == 0xC0 && length == 1) {
    observation->type = ZnpObservationType::ZDO_STATE_CHANGE;
    observation->device_state = payload[0];
    return true;
  }

  return false;
}

}  // namespace zigbee_gateway
}  // namespace esphome
