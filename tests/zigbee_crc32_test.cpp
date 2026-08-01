#include <cassert>
#include <cstdint>

#include "components/zigbee_gateway/zigbee_crc32.h"

using esphome::zigbee_gateway::zigbee_crc32;
using esphome::zigbee_gateway::zigbee_crc32_update;

int main() {
  const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
  assert(zigbee_crc32(check, sizeof(check)) == 0xCBF43926UL);

  uint32_t crc = zigbee_crc32_update(0xFFFFFFFFUL, check, 4);
  crc = zigbee_crc32_update(crc, check + 4, sizeof(check) - 4);
  assert((crc ^ 0xFFFFFFFFUL) == 0xCBF43926UL);

  assert(zigbee_crc32(nullptr, 0) == 0);
  return 0;
}
