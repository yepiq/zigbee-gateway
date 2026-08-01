#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::zigbee_gateway {

inline void zigbee_bsl_encode_u32_be(uint32_t value, uint8_t out[4]) {
  out[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
  out[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
  out[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
  out[3] = static_cast<uint8_t>(value & 0xFF);
}

inline uint32_t zigbee_bsl_decode_u32_be(const uint8_t in[4]) {
  return (static_cast<uint32_t>(in[0]) << 24) |
         (static_cast<uint32_t>(in[1]) << 16) |
         (static_cast<uint32_t>(in[2]) << 8) |
         static_cast<uint32_t>(in[3]);
}

// Build [size][checksum][payload]. Size includes the two-byte header and the
// checksum is the payload byte sum modulo 256, as required by the TI ROM BSL.
inline size_t zigbee_bsl_build_frame(const uint8_t *payload,
                                     size_t payload_length, uint8_t *frame,
                                     size_t frame_capacity) {
  if (payload == nullptr || payload_length == 0 || payload_length > 253 ||
      frame == nullptr || frame_capacity < payload_length + 2)
    return 0;

  frame[0] = static_cast<uint8_t>(payload_length + 2);
  uint32_t checksum = 0;
  for (size_t index = 0; index < payload_length; index++) {
    checksum += payload[index];
    frame[index + 2] = payload[index];
  }
  frame[1] = static_cast<uint8_t>(checksum & 0xFF);
  return payload_length + 2;
}

}  // namespace esphome::zigbee_gateway
