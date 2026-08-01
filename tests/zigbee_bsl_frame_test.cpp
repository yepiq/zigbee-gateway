#include <array>
#include <cassert>
#include <cstdint>

#include "components/zigbee_gateway/zigbee_bsl_frame.h"

using esphome::zigbee_gateway::zigbee_bsl_build_frame;
using esphome::zigbee_gateway::zigbee_bsl_build_download_crc_payload;
using esphome::zigbee_gateway::zigbee_bsl_build_send_data_payload;
using esphome::zigbee_gateway::zigbee_bsl_decode_u32_be;
using esphome::zigbee_gateway::zigbee_bsl_encode_u32_be;
using esphome::zigbee_gateway::zigbee_bsl_parse_command_response;
using esphome::zigbee_gateway::ZigbeeBslCommandResponse;
using esphome::zigbee_gateway::ZigbeeBslCommandResponseParser;

int main() {
  const uint8_t ack[] = {0x00, 0xCC};
  const uint8_t nak[] = {0x00, 0x33};
  const uint8_t lone_ack[] = {0xCC};
  const uint8_t false_positive[] = {0x12, 0xCC};
  const uint8_t reversed_ack[] = {0xCC, 0x00};
  assert(zigbee_bsl_parse_command_response(ack, sizeof(ack)) ==
         ZigbeeBslCommandResponse::ACK);
  assert(zigbee_bsl_parse_command_response(nak, sizeof(nak)) ==
         ZigbeeBslCommandResponse::NAK);
  assert(zigbee_bsl_parse_command_response(lone_ack, sizeof(lone_ack)) ==
         ZigbeeBslCommandResponse::INVALID);
  assert(zigbee_bsl_parse_command_response(false_positive,
                                           sizeof(false_positive)) ==
         ZigbeeBslCommandResponse::INVALID);
  assert(zigbee_bsl_parse_command_response(reversed_ack,
                                           sizeof(reversed_ack)) ==
         ZigbeeBslCommandResponse::INVALID);
  assert(zigbee_bsl_parse_command_response(nullptr, 0) ==
         ZigbeeBslCommandResponse::INVALID);

  ZigbeeBslCommandResponseParser leading_byte_parser;
  assert(leading_byte_parser.push(0x01) == ZigbeeBslCommandResponse::INVALID);
  assert(leading_byte_parser.push(0x00) == ZigbeeBslCommandResponse::INVALID);
  assert(leading_byte_parser.push(0xCC) == ZigbeeBslCommandResponse::ACK);

  ZigbeeBslCommandResponseParser lone_ack_parser;
  assert(lone_ack_parser.push(0xCC) == ZigbeeBslCommandResponse::INVALID);

  ZigbeeBslCommandResponseParser malformed_parser;
  assert(malformed_parser.push(0x00) == ZigbeeBslCommandResponse::INVALID);
  assert(malformed_parser.push(0x12) == ZigbeeBslCommandResponse::INVALID);
  assert(malformed_parser.push(0xCC) == ZigbeeBslCommandResponse::INVALID);

  ZigbeeBslCommandResponseParser repeated_zero_parser;
  assert(repeated_zero_parser.push(0x00) == ZigbeeBslCommandResponse::INVALID);
  assert(repeated_zero_parser.push(0x00) == ZigbeeBslCommandResponse::INVALID);
  assert(repeated_zero_parser.push(0x33) == ZigbeeBslCommandResponse::NAK);

  uint8_t encoded[4]{};
  zigbee_bsl_encode_u32_be(0x1234ABCDUL, encoded);
  assert((std::array<uint8_t, 4>{encoded[0], encoded[1], encoded[2],
                                 encoded[3]}) ==
         (std::array<uint8_t, 4>{0x12, 0x34, 0xAB, 0xCD}));
  assert(zigbee_bsl_decode_u32_be(encoded) == 0x1234ABCDUL);

  const uint8_t erase_payload[] = {0x2C};
  uint8_t erase_frame[3]{};
  assert(zigbee_bsl_build_frame(erase_payload, sizeof(erase_payload),
                                erase_frame, sizeof(erase_frame)) == 3);
  assert((std::array<uint8_t, 3>{erase_frame[0], erase_frame[1],
                                 erase_frame[2]}) ==
         (std::array<uint8_t, 3>{0x03, 0x2C, 0x2C}));

  const uint8_t reset_payload[] = {0x25};
  uint8_t reset_frame[3]{};
  assert(zigbee_bsl_build_frame(reset_payload, sizeof(reset_payload),
                                reset_frame, sizeof(reset_frame)) == 3);
  assert((std::array<uint8_t, 3>{reset_frame[0], reset_frame[1],
                                 reset_frame[2]}) ==
         (std::array<uint8_t, 3>{0x03, 0x25, 0x25}));

  uint8_t download_payload[9]{};
  download_payload[0] = 0x21;
  zigbee_bsl_encode_u32_be(0, &download_payload[1]);
  zigbee_bsl_encode_u32_be(0x000B0000UL, &download_payload[5]);
  uint8_t download_frame[11]{};
  assert(zigbee_bsl_build_frame(download_payload, sizeof(download_payload),
                                download_frame, sizeof(download_frame)) == 11);
  assert(download_frame[0] == 0x0B);
  assert(download_frame[1] == 0x2C);
  assert(download_frame[2] == 0x21);
  assert(download_frame[7] == 0x00);
  assert(download_frame[8] == 0x0B);
  assert(download_frame[9] == 0x00);
  assert(download_frame[10] == 0x00);

  uint8_t download_crc_payload[13]{};
  assert(zigbee_bsl_build_download_crc_payload(
             0, 0x000B0000UL, 0x673D9A56UL, download_crc_payload,
             sizeof(download_crc_payload)) == sizeof(download_crc_payload));
  assert(download_crc_payload[0] == 0x2F);
  assert(download_crc_payload[5] == 0x00);
  assert(download_crc_payload[6] == 0x0B);
  assert(download_crc_payload[9] == 0x67);
  assert(download_crc_payload[10] == 0x3D);
  assert(download_crc_payload[11] == 0x9A);
  assert(download_crc_payload[12] == 0x56);

  std::array<uint8_t, 253> send_payload{};
  std::array<uint8_t, 252> send_data{};
  for (size_t index = 0; index < send_data.size(); index++)
    send_data[index] = static_cast<uint8_t>(index);
  assert(zigbee_bsl_build_send_data_payload(
             send_data.data(), send_data.size(), send_payload.data(),
             send_payload.size()) == send_payload.size());
  assert(send_payload[0] == 0x24);
  assert(send_payload[1] == 0x00);
  assert(send_payload[252] == 0xFB);
  assert(zigbee_bsl_build_send_data_payload(
             send_data.data(), send_data.size(), send_payload.data(),
             send_payload.size() - 1) == 0);
  std::array<uint8_t, 253> oversized_data{};
  assert(zigbee_bsl_build_send_data_payload(
             oversized_data.data(), oversized_data.size(), send_payload.data(),
             send_payload.size()) == 0);

  assert(zigbee_bsl_build_frame(nullptr, 0, erase_frame,
                                sizeof(erase_frame)) == 0);
  return 0;
}
