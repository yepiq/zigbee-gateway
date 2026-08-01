#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include "components/zigbee_gateway/zigbee_znp_observer.h"

using namespace esphome::zigbee_gateway;

static uint8_t frame_fcs(uint8_t cmd0, uint8_t cmd1, const uint8_t *payload, uint8_t length) {
  uint8_t fcs = length ^ cmd0 ^ cmd1;
  for (uint8_t index = 0; index < length; index++)
    fcs ^= payload[index];
  return fcs;
}

int main() {
  ZnpObservation observation{};

  const uint8_t version[] = {
      0x02, 0x01, 0x03, 0x30, 0x00, 0x78, 0x56, 0x34, 0x12,
  };
  assert(znp_frame_fcs_valid(sizeof(version), 0x61, 0x02, version,
                             frame_fcs(0x61, 0x02, version, sizeof(version))));
  assert(decode_znp_observation(0x61, 0x02, version, sizeof(version), &observation));
  assert(observation.type == ZnpObservationType::SYS_VERSION);
  assert(observation.major == 3);
  assert(observation.minor == 0x30);
  assert(observation.maintenance == 0);
  assert(observation.revision == 0x12345678);
  assert(!znp_frame_fcs_valid(sizeof(version), 0x61, 0x02, version, 0x00));
  assert(!decode_znp_observation(0x61, 0x02, version, 8, &observation));

  const uint8_t device_info[] = {
      0x00,                                      // status
      0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00,  // IEEE LSB first
      0x00, 0x00,                                // short address
      0x07,                                      // coordinator/router/end-device capabilities
      0x00,                                      // DEV_HOLD; role is not active yet
      0x00,                                      // associated device count
  };
  assert(decode_znp_observation(0x67, 0x00, device_info, sizeof(device_info), &observation));
  assert(observation.type == ZnpObservationType::UTIL_DEVICE_INFO);
  assert(observation.active_ieee_lsb[0] == 0x77);
  assert(observation.active_ieee_lsb[7] == 0x00);
  assert(observation.device_capabilities == 7);
  assert(observation.device_state == 0);
  assert(!znp_device_state_is_on_network(observation.device_state));
  assert(znp_observed_role(observation.device_capabilities,
                           observation.device_state) ==
         ZnpObservedRole::UNKNOWN);

  assert(znp_observed_role(0x07, 0x09) == ZnpObservedRole::COORDINATOR);
  assert(znp_observed_role(0x07, 0x08) == ZnpObservedRole::COORDINATOR);
  assert(znp_observed_role(0x07, 0x07) == ZnpObservedRole::ROUTER);
  assert(znp_observed_role(0x07, 0x06) == ZnpObservedRole::END_DEVICE);
  assert(znp_observed_role(0x01, 0x00) == ZnpObservedRole::COORDINATOR);
  assert(znp_observed_role(0x02, 0x00) == ZnpObservedRole::ROUTER);
  assert(znp_observed_role(0x04, 0x00) == ZnpObservedRole::END_DEVICE);
  assert(znp_observed_role(0x07, 0x0A) == ZnpObservedRole::UNKNOWN);
  assert(std::strcmp(znp_observed_role_name(ZnpObservedRole::COORDINATOR),
                     "Coordinator") == 0);

  auto failed_device_info = std::array<uint8_t, sizeof(device_info)>{};
  for (size_t index = 0; index < failed_device_info.size(); index++)
    failed_device_info[index] = device_info[index];
  failed_device_info[0] = 1;
  assert(!decode_znp_observation(0x67, 0x00, failed_device_info.data(),
                                 failed_device_info.size(), &observation));

  const uint8_t ext_network_info[] = {
      0x00, 0x00,  // short address
      0x09,        // ZB_COORD
      0x62, 0x1A,  // PAN ID 0x1A62
      0x00, 0x00,  // parent short address
      0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,  // extended PAN LSB first
      0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,  // parent IEEE LSB first
      0x0F,                                                // channel 15
  };
  assert(decode_znp_observation(0x65, 0x50, ext_network_info,
                                 sizeof(ext_network_info), &observation));
  assert(observation.type == ZnpObservationType::ZDO_EXT_NETWORK_INFO);
  assert(observation.pan_id == 0x1A62);
  assert(observation.channel == 15);
  assert(observation.extended_pan_id_lsb[0] == 8);
  assert(observation.extended_pan_id_lsb[7] == 1);
  assert(observation.parent_ieee_lsb[0] == 0x88);
  assert(observation.parent_ieee_lsb[7] == 0x11);
  assert(!decode_znp_observation(0x65, 0x50, ext_network_info,
                                 sizeof(ext_network_info) - 1, &observation));

  const uint8_t state_change[] = {0x07};
  assert(decode_znp_observation(0x45, 0xC0, state_change,
                                 sizeof(state_change), &observation));
  assert(observation.type == ZnpObservationType::ZDO_STATE_CHANGE);
  assert(znp_device_state_is_on_network(observation.device_state));
  assert(!znp_device_state_is_on_network(8));
  assert(!decode_znp_observation(0x45, 0xC0, state_change, 0, &observation));

  assert(!decode_znp_observation(0x61, 0x01, version, sizeof(version), &observation));
  return 0;
}
