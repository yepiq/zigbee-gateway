#include <array>
#include <cassert>
#include <cstdint>

#include "components/zigbee_gateway/zigbee_ti_image.h"

using esphome::zigbee_gateway::zigbee_ti_ccfg_error_name;
using esphome::zigbee_gateway::zigbee_ti_decode_u32_le;
using esphome::zigbee_gateway::zigbee_ti_image_size_is_compatible;
using esphome::zigbee_gateway::zigbee_ti_parse_ccfg;
using esphome::zigbee_gateway::zigbee_ti_validate_ccfg;
using esphome::zigbee_gateway::ZigbeeTiCcfg;
using esphome::zigbee_gateway::ZigbeeTiCcfgError;
using esphome::zigbee_gateway::ZIGBEE_TI_CCFG_SIZE;

static std::array<uint8_t, ZIGBEE_TI_CCFG_SIZE> valid_ccfg() {
  std::array<uint8_t, ZIGBEE_TI_CCFG_SIZE> data{};
  data.fill(0xFF);

  // MODE_CONF=0xFFB9C1FF, BL_CONFIG=0xC5FE0FC5, ERASE_CONF=0xFFFFFFFF.
  const uint8_t mode_conf[] = {0xFF, 0xC1, 0xB9, 0xFF};
  const uint8_t bl_config[] = {0xC5, 0x0F, 0xFE, 0xC5};
  for (size_t index = 0; index < 4; index++) {
    data[12 + index] = mode_conf[index];
    data[48 + index] = bl_config[index];
    data[68 + index] = 0x00;
  }
  return data;
}

int main() {
  assert(zigbee_ti_image_size_is_compatible(720896, 720896));
  assert(!zigbee_ti_image_size_is_compatible(0, 0));
  assert(!zigbee_ti_image_size_is_compatible(720896, 352256));
  assert(!zigbee_ti_image_size_is_compatible(720895, 720895));

  const uint8_t little_endian[] = {0xC5, 0x0F, 0xFE, 0xC5};
  assert(zigbee_ti_decode_u32_le(little_endian) == 0xC5FE0FC5UL);

  auto data = valid_ccfg();
  ZigbeeTiCcfg parsed{};
  assert(zigbee_ti_validate_ccfg(data.data(), data.size(), 15, false, 0,
                                 &parsed) == ZigbeeTiCcfgError::NONE);
  assert(parsed.mode_conf == 0xFFB9C1FFUL);
  assert(parsed.bl_config == 0xC5FE0FC5UL);
  assert(parsed.bootloader_enable == 0xC5);
  assert(parsed.backdoor_enable == 0xC5);
  assert(parsed.backdoor_dio == 15);
  assert(!parsed.backdoor_active_high);
  assert(parsed.image_valid == 0);
  assert(parsed.protection[0] == 0xFFFFFFFFUL);

  // MODE_CONF legitimately differs among the curated CC2652P7 images. It is
  // useful diagnostic data, but not part of the remote-recovery contract.
  auto candidate = data;
  candidate[12] = 0x3A;
  candidate[13] = 0xC1;
  candidate[14] = 0xB9;
  candidate[15] = 0xF3;
  assert(zigbee_ti_validate_ccfg(candidate.data(), candidate.size(), 15,
                                 false, 0) == ZigbeeTiCcfgError::NONE);

  assert(!zigbee_ti_parse_ccfg(nullptr, data.size(), &parsed));
  assert(!zigbee_ti_parse_ccfg(data.data(), data.size() - 1, &parsed));
  assert(zigbee_ti_validate_ccfg(data.data(), data.size() - 1, 15, false, 0) ==
         ZigbeeTiCcfgError::INVALID_SIZE);

  candidate = data;
  candidate[51] = 0x00;
  assert(zigbee_ti_validate_ccfg(candidate.data(), candidate.size(), 15,
                                 false, 0) ==
         ZigbeeTiCcfgError::BOOTLOADER_DISABLED);

  candidate = data;
  candidate[48] = 0x00;
  assert(zigbee_ti_validate_ccfg(candidate.data(), candidate.size(), 15,
                                 false, 0) ==
         ZigbeeTiCcfgError::BACKDOOR_DISABLED);

  assert(zigbee_ti_validate_ccfg(data.data(), data.size(), 14, false, 0) ==
         ZigbeeTiCcfgError::BACKDOOR_DIO_MISMATCH);
  assert(zigbee_ti_validate_ccfg(data.data(), data.size(), 15, true, 0) ==
         ZigbeeTiCcfgError::BACKDOOR_LEVEL_MISMATCH);

  candidate = data;
  candidate[52] &= 0xFE;
  assert(zigbee_ti_validate_ccfg(candidate.data(), candidate.size(), 15,
                                 false, 0) ==
         ZigbeeTiCcfgError::BANK_ERASE_DISABLED);

  candidate = data;
  candidate[68] = 0x04;
  assert(zigbee_ti_validate_ccfg(candidate.data(), candidate.size(), 15,
                                 false, 0) ==
         ZigbeeTiCcfgError::INVALID_IMAGE_VECTOR);

  candidate = data;
  candidate[72] &= 0xFE;
  assert(zigbee_ti_validate_ccfg(candidate.data(), candidate.size(), 15,
                                 false, 0) ==
         ZigbeeTiCcfgError::FLASH_PROTECTED);

  assert(zigbee_ti_ccfg_error_name(ZigbeeTiCcfgError::NONE) != nullptr);
  return 0;
}
