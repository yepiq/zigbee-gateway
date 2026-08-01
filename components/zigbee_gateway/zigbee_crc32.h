#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::zigbee_gateway {

inline uint32_t zigbee_crc32_update(uint32_t crc, const uint8_t *data,
                                    size_t length) {
  for (size_t index = 0; index < length; index++) {
    crc ^= data[index];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
  }
  return crc;
}

inline uint32_t zigbee_crc32(const uint8_t *data, size_t length) {
  return zigbee_crc32_update(0xFFFFFFFFUL, data, length) ^ 0xFFFFFFFFUL;
}

}  // namespace esphome::zigbee_gateway
