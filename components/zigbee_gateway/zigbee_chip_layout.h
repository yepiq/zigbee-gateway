#pragma once

#include <cstdint>

namespace esphome {
namespace zigbee_gateway {

enum class ChipFamily : uint8_t {
  UNKNOWN = 0,
  CC13X2_CC26X2 = 1,
  CC13X2X7_CC26X2X7 = 2,
};

/// Flash geometry and Koenkk Z-Stack NVOCMP placement for one detected family.
///
/// `flash_size_unit_bytes` is the unit represented by FLASH_SIZE_REG[0].
/// It is 4 KiB on x2 and 8 KiB on x2x7 and is used to derive total flash and
/// the end-of-flash CCFG addresses. It is deliberately separate from
/// `nv_page_size`: the supported NVOCMP builds use 8 KiB pages on both
/// families.
struct ZigbeeChipLayout {
  const char *name;
  uint32_t flash_size_unit_bytes;
  uint32_t nv_base;
  uint32_t nv_size;
  uint32_t nv_page_size;
};

// Koenkk's patched x2 coordinator/router layout:
//   FLASH_SIZE register unit: 0x1000
//   NVOCMP: 0x50000..0x56000, three 0x2000-byte pages
inline constexpr ZigbeeChipLayout CHIP_LAYOUT_X2{
    "cc13x2_cc26x2",
    0x00001000u,
    0x00050000u,
    0x00006000u,
    0x00002000u,
};

// Koenkk's patched x2x7 coordinator/router layout:
//   FLASH_SIZE register unit: 0x2000
//   NVOCMP: 0xA6000..0xAE000, four 0x2000-byte pages
inline constexpr ZigbeeChipLayout CHIP_LAYOUT_X2X7{
    "cc13x2x7_cc26x2x7",
    0x00002000u,
    0x000A6000u,
    0x00008000u,
    0x00002000u,
};

static_assert(CHIP_LAYOUT_X2.nv_base % CHIP_LAYOUT_X2.nv_page_size == 0,
              "x2 NV base must be page-aligned");
static_assert(CHIP_LAYOUT_X2.nv_size % CHIP_LAYOUT_X2.nv_page_size == 0,
              "x2 NV size must contain complete pages");
static_assert(CHIP_LAYOUT_X2X7.nv_base % CHIP_LAYOUT_X2X7.nv_page_size == 0,
              "x2x7 NV base must be page-aligned");
static_assert(CHIP_LAYOUT_X2X7.nv_size % CHIP_LAYOUT_X2X7.nv_page_size == 0,
              "x2x7 NV size must contain complete pages");

inline constexpr const ZigbeeChipLayout *chip_layout_for_family(ChipFamily family) {
  switch (family) {
    case ChipFamily::CC13X2_CC26X2:
      return &CHIP_LAYOUT_X2;
    case ChipFamily::CC13X2X7_CC26X2X7:
      return &CHIP_LAYOUT_X2X7;
    case ChipFamily::UNKNOWN:
      return nullptr;
  }
  return nullptr;
}

inline constexpr uint32_t nv_page_count(const ZigbeeChipLayout &layout) {
  return layout.nv_size / layout.nv_page_size;
}

}  // namespace zigbee_gateway
}  // namespace esphome
