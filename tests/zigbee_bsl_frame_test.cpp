#include <array>
#include <cassert>
#include <cstdint>

#include "components/zigbee_gateway/zigbee_bsl_frame.h"

using esphome::zigbee_gateway::zigbee_bsl_build_frame;
using esphome::zigbee_gateway::zigbee_bsl_decode_u32_be;
using esphome::zigbee_gateway::zigbee_bsl_encode_u32_be;

int main() {
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

  assert(zigbee_bsl_build_frame(nullptr, 0, erase_frame,
                                sizeof(erase_frame)) == 0);
  return 0;
}
