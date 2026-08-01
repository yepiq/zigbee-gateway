#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::zigbee_gateway {

enum class ZigbeeBslCommandResponse : uint8_t {
  INVALID,
  ACK,
  NAK,
};

class ZigbeeBslCommandResponseParser {
 public:
  ZigbeeBslCommandResponse push(uint8_t byte) {
    if (this->zero_seen_) {
      if (byte == 0xCC) {
        this->zero_seen_ = false;
        return ZigbeeBslCommandResponse::ACK;
      }
      if (byte == 0x33) {
        this->zero_seen_ = false;
        return ZigbeeBslCommandResponse::NAK;
      }
    }
    this->zero_seen_ = byte == 0x00;
    return ZigbeeBslCommandResponse::INVALID;
  }

 protected:
  bool zero_seen_{false};
};

// TI ROM BSL command responses are exactly two bytes. Treating a lone 0xCC as
// an ACK can mistake stale payload data for command acceptance.
inline ZigbeeBslCommandResponse zigbee_bsl_parse_command_response(
    const uint8_t *response, size_t response_length) {
  if (response == nullptr || response_length != 2)
    return ZigbeeBslCommandResponse::INVALID;
  ZigbeeBslCommandResponseParser parser;
  parser.push(response[0]);
  return parser.push(response[1]);
}

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

inline size_t zigbee_bsl_build_download_crc_payload(uint32_t address,
                                                    uint32_t size,
                                                    uint32_t crc,
                                                    uint8_t *payload,
                                                    size_t capacity) {
  if (size == 0 || (size & 0x03) != 0 || payload == nullptr || capacity < 13)
    return 0;
  payload[0] = 0x2F;
  zigbee_bsl_encode_u32_be(address, &payload[1]);
  zigbee_bsl_encode_u32_be(size, &payload[5]);
  zigbee_bsl_encode_u32_be(crc, &payload[9]);
  return 13;
}

inline size_t zigbee_bsl_build_send_data_payload(const uint8_t *data,
                                                 size_t length,
                                                 uint8_t *payload,
                                                 size_t capacity) {
  if (data == nullptr || length == 0 || length > 252 || payload == nullptr ||
      capacity < length + 1)
    return 0;
  payload[0] = 0x24;
  for (size_t index = 0; index < length; index++)
    payload[index + 1] = data[index];
  return length + 1;
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
