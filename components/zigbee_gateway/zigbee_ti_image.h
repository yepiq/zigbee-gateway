#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome::zigbee_gateway {

// CC13x2/CC26x2 CCFG occupies the final 88 bytes of flash. These fields decide
// whether the newly installed image can boot and whether a later remote update
// can re-enter the ROM serial bootloader.
static constexpr size_t ZIGBEE_TI_CCFG_SIZE = 88;
static constexpr size_t ZIGBEE_TI_CCFG_MODE_CONF_OFFSET = 12;
static constexpr size_t ZIGBEE_TI_CCFG_BL_CONFIG_OFFSET = 48;
static constexpr size_t ZIGBEE_TI_CCFG_ERASE_CONF_OFFSET = 52;
static constexpr size_t ZIGBEE_TI_CCFG_IMAGE_VALID_OFFSET = 68;
static constexpr size_t ZIGBEE_TI_CCFG_PROTECTION_OFFSET = 72;
static constexpr size_t ZIGBEE_TI_CCFG_PROTECTION_WORDS = 4;
static constexpr uint8_t ZIGBEE_TI_CCFG_ENABLE_VALUE = 0xC5;

inline bool zigbee_ti_image_size_is_compatible(size_t image_size,
                                                uint32_t radio_size) {
  return image_size != 0 && image_size == radio_size &&
         (image_size & 0x03) == 0;
}

inline uint32_t zigbee_ti_decode_u32_le(const uint8_t in[4]) {
  return static_cast<uint32_t>(in[0]) |
         (static_cast<uint32_t>(in[1]) << 8) |
         (static_cast<uint32_t>(in[2]) << 16) |
         (static_cast<uint32_t>(in[3]) << 24);
}

struct ZigbeeTiCcfg {
  uint32_t mode_conf{0};
  uint32_t bl_config{0};
  uint32_t erase_conf{0};
  uint32_t image_valid{0};
  uint32_t protection[ZIGBEE_TI_CCFG_PROTECTION_WORDS]{};

  uint8_t bootloader_enable{0};
  bool backdoor_active_high{false};
  uint8_t backdoor_dio{0};
  uint8_t backdoor_enable{0};
};

enum class ZigbeeTiCcfgError : uint8_t {
  NONE,
  INVALID_SIZE,
  BOOTLOADER_DISABLED,
  BACKDOOR_DISABLED,
  BACKDOOR_DIO_MISMATCH,
  BACKDOOR_LEVEL_MISMATCH,
  BANK_ERASE_DISABLED,
  INVALID_IMAGE_VECTOR,
  FLASH_PROTECTED,
};

inline const char *zigbee_ti_ccfg_error_name(ZigbeeTiCcfgError error) {
  switch (error) {
    case ZigbeeTiCcfgError::NONE:
      return "none";
    case ZigbeeTiCcfgError::INVALID_SIZE:
      return "image is too small to contain TI CCFG";
    case ZigbeeTiCcfgError::BOOTLOADER_DISABLED:
      return "ROM serial bootloader is disabled";
    case ZigbeeTiCcfgError::BACKDOOR_DISABLED:
      return "ROM serial bootloader backdoor is disabled";
    case ZigbeeTiCcfgError::BACKDOOR_DIO_MISMATCH:
      return "bootloader backdoor DIO does not match the gateway wiring";
    case ZigbeeTiCcfgError::BACKDOOR_LEVEL_MISMATCH:
      return "bootloader backdoor level does not match the gateway wiring";
    case ZigbeeTiCcfgError::BANK_ERASE_DISABLED:
      return "ROM serial bootloader bank erase is disabled";
    case ZigbeeTiCcfgError::INVALID_IMAGE_VECTOR:
      return "image vector address does not match the programmed range";
    case ZigbeeTiCcfgError::FLASH_PROTECTED:
      return "image enables flash write protection";
  }
  return "unknown TI CCFG error";
}

inline bool zigbee_ti_parse_ccfg(const uint8_t *image_tail, size_t length,
                                 ZigbeeTiCcfg *ccfg) {
  if (image_tail == nullptr || ccfg == nullptr ||
      length != ZIGBEE_TI_CCFG_SIZE)
    return false;

  ccfg->mode_conf = zigbee_ti_decode_u32_le(
      &image_tail[ZIGBEE_TI_CCFG_MODE_CONF_OFFSET]);
  ccfg->bl_config = zigbee_ti_decode_u32_le(
      &image_tail[ZIGBEE_TI_CCFG_BL_CONFIG_OFFSET]);
  ccfg->erase_conf = zigbee_ti_decode_u32_le(
      &image_tail[ZIGBEE_TI_CCFG_ERASE_CONF_OFFSET]);
  ccfg->image_valid = zigbee_ti_decode_u32_le(
      &image_tail[ZIGBEE_TI_CCFG_IMAGE_VALID_OFFSET]);
  for (size_t index = 0; index < ZIGBEE_TI_CCFG_PROTECTION_WORDS; index++) {
    ccfg->protection[index] = zigbee_ti_decode_u32_le(
        &image_tail[ZIGBEE_TI_CCFG_PROTECTION_OFFSET + index * 4]);
  }

  ccfg->bootloader_enable =
      static_cast<uint8_t>((ccfg->bl_config >> 24) & 0xFF);
  ccfg->backdoor_active_high = ((ccfg->bl_config >> 16) & 0x01) != 0;
  ccfg->backdoor_dio =
      static_cast<uint8_t>((ccfg->bl_config >> 8) & 0xFF);
  ccfg->backdoor_enable = static_cast<uint8_t>(ccfg->bl_config & 0xFF);
  return true;
}

inline ZigbeeTiCcfgError zigbee_ti_validate_ccfg(
    const uint8_t *image_tail, size_t length, uint8_t expected_backdoor_dio,
    bool expected_active_high, uint32_t expected_image_vector,
    ZigbeeTiCcfg *parsed = nullptr) {
  ZigbeeTiCcfg ccfg{};
  if (!zigbee_ti_parse_ccfg(image_tail, length, &ccfg))
    return ZigbeeTiCcfgError::INVALID_SIZE;
  if (parsed != nullptr)
    *parsed = ccfg;

  if (ccfg.bootloader_enable != ZIGBEE_TI_CCFG_ENABLE_VALUE)
    return ZigbeeTiCcfgError::BOOTLOADER_DISABLED;
  if (ccfg.backdoor_enable != ZIGBEE_TI_CCFG_ENABLE_VALUE)
    return ZigbeeTiCcfgError::BACKDOOR_DISABLED;
  if (ccfg.backdoor_dio != expected_backdoor_dio)
    return ZigbeeTiCcfgError::BACKDOOR_DIO_MISMATCH;
  if (ccfg.backdoor_active_high != expected_active_high)
    return ZigbeeTiCcfgError::BACKDOOR_LEVEL_MISMATCH;
  if ((ccfg.erase_conf & 0x01) == 0)
    return ZigbeeTiCcfgError::BANK_ERASE_DISABLED;
  if (ccfg.image_valid != expected_image_vector)
    return ZigbeeTiCcfgError::INVALID_IMAGE_VECTOR;
  for (uint32_t word : ccfg.protection) {
    if (word != 0xFFFFFFFFUL)
      return ZigbeeTiCcfgError::FLASH_PROTECTED;
  }
  return ZigbeeTiCcfgError::NONE;
}

}  // namespace esphome::zigbee_gateway
